#include "search.h"
#include "tt.h"
#include "eval.h"
#include "nnue.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <algorithm>
#include <thread>

TranspositionTable TT;
std::atomic<bool> g_stop{false};
std::atomic<bool> g_searching{false};
int g_moveOverhead = 30;
bool g_silent = false;
std::mutex g_ioMutex;

typedef int16_t PieceToHist[12][64];

struct Stack {
    Move move;
    int piece;
    int staticEval;
    Move killers[2];
    Move excluded;
    PieceToHist *cont;
    bool inCheck;
};

static int LMR[64][64];
static int16_t mainHist[2][64 * 64];
static int16_t captHist[12][64][6];
static PieceToHist contHist[12][64];
static PieceToHist contDummy;
static Move counterMoves[12][64];
const int CORR_SIZE = 16384;
const int CORR_GRAIN = 256;
static int32_t pawnCorr[2][CORR_SIZE];
static int32_t npCorr[2][2][CORR_SIZE];
static Move pvTable[MAX_PLY + 2][MAX_PLY + 2];
static int pvLen[MAX_PLY + 2];

static uint64_t nodes;
static int selDepth;
static std::chrono::steady_clock::time_point startTime;
static int64_t softLimit, hardLimit, nodeLimit;
static bool useTime;
static int rootDepth;
static uint64_t rootNodes[4096];
static int rootScore = 0;

static const int SeeVal[13] = {100, 320, 330, 500, 950, 0, 100, 320, 330, 500, 950, 0, 0};

static inline int64_t elapsed_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
}

int evaluate(Position &pos) {
    int v = g_nnueLoaded ? nnue_evaluate(pos) : evaluate_hce(pos);
    // damp towards draw as 50-move counter grows
    v = v * (200 - pos.st().rule50) / 200;
    return std::clamp(v, -MATE_IN_MAX + 1, MATE_IN_MAX - 1);
}

void search_init() {
    for (int d = 0; d < 64; d++)
        for (int m = 0; m < 64; m++)
            LMR[d][m] = (d == 0 || m == 0) ? 0 : int(0.75 + std::log((double)d) * std::log((double)m) / 2.25);
    search_clear();
}

void search_clear() {
    memset(mainHist, 0, sizeof(mainHist));
    memset(captHist, 0, sizeof(captHist));
    memset(contHist, 0, sizeof(contHist));
    memset(contDummy, 0, sizeof(contDummy));
    memset(counterMoves, 0, sizeof(counterMoves));
    memset(pawnCorr, 0, sizeof(pawnCorr));
    memset(npCorr, 0, sizeof(npCorr));
    TT.clear();
}

static inline void update_hist(int16_t &h, int bonus) {
    bonus = std::clamp(bonus, -16000, 16000);
    h += bonus - h * std::abs(bonus) / 16384;
}

static inline int hist_bonus(int depth) { return std::min(2000, 250 * depth - 150); }

// pawn-structure correction history: learns (search score - static eval) per pawn/king structure

static inline int corrected_eval(const Position &pos, int raw) {
    const StateInfo &st = pos.st();
    int c = pawnCorr[pos.stm][st.pawnKey & (CORR_SIZE - 1)];
#ifdef USE_NPCORR
    c += (npCorr[pos.stm][WHITE][st.npKey[WHITE] & (CORR_SIZE - 1)] + npCorr[pos.stm][BLACK][st.npKey[BLACK] & (CORR_SIZE - 1)]) / 2;
#endif
    return std::clamp(raw + c / CORR_GRAIN, -MATE_IN_MAX + 1, MATE_IN_MAX - 1);
}

static inline void corr_entry_update(int32_t &e, int w, int diff) {
    e = (e * (256 - w) + diff * CORR_GRAIN * w) / 256;
    e = std::clamp(e, -64 * CORR_GRAIN, 64 * CORR_GRAIN);
}

static inline void update_corr(const Position &pos, int depth, int diff) {
    const StateInfo &st = pos.st();
    int w = std::min(depth + 1, 16);
    corr_entry_update(pawnCorr[pos.stm][st.pawnKey & (CORR_SIZE - 1)], w, diff);
#ifdef USE_NPCORR
    corr_entry_update(npCorr[pos.stm][WHITE][st.npKey[WHITE] & (CORR_SIZE - 1)], w, diff);
    corr_entry_update(npCorr[pos.stm][BLACK][st.npKey[BLACK] & (CORR_SIZE - 1)], w, diff);
#endif
}

// ---------------- move picker ----------------
enum Stage {
    ST_TT, ST_GEN_CAP, ST_GOOD_CAP, ST_KILLER1, ST_KILLER2, ST_COUNTER, ST_GEN_QUIET, ST_QUIET, ST_BAD_CAP, ST_DONE,
    QS_TT, QS_GEN, QS_CAP,
    EV_TT, EV_GEN, EV_ALL
};

struct MovePicker {
    Position &pos;
    Stack *ss;
    Move ttMove, k1, k2, counter;
    int stage;
    MoveList list;
    int idx = 0;
    Move bad[64];
    int nbad = 0, badIdx = 0;

    MovePicker(Position &p, Stack *s, Move tt, bool qs) : pos(p), ss(s) {
        ttMove = (tt && pos.pseudo_legal(tt)) ? tt : MOVE_NONE;
        if (pos.in_check()) stage = EV_TT;
        else if (qs) {
            stage = QS_TT;
            if (ttMove && !pos.is_tactical(ttMove)) ttMove = MOVE_NONE;
        } else stage = ST_TT;
        k1 = ss->killers[0];
        k2 = ss->killers[1];
        counter = MOVE_NONE;
        Move prev = (ss - 1)->move;
        if (prev && prev != MOVE_NULL) counter = counterMoves[(ss - 1)->piece][to_sq(prev)];
    }

    int capture_score(Move m) {
        int cap = pos.captured_piece(m);
        int capType = cap == NO_PIECE ? PAWN : piece_type(cap);
        int s = cap == NO_PIECE ? 0 : SeeVal[cap] * 8;
        if (move_type(m) == MT_PROMO) s += 8000;
        s += captHist[pos.piece_on(from_sq(m))][to_sq(m)][capType] / 8;
        return s;
    }
    int quiet_score(Move m) {
        int pc = pos.piece_on(from_sq(m));
        int to = to_sq(m);
        return mainHist[pos.stm][m & 4095] + (*(ss - 1)->cont)[pc][to] + (*(ss - 2)->cont)[pc][to] + (*(ss - 4)->cont)[pc][to] / 2;
    }
    void score_captures() {
        for (int i = 0; i < list.n; i++) list.scores[i] = capture_score(list.moves[i]);
    }
    void score_quiets() {
        for (int i = 0; i < list.n; i++) list.scores[i] = quiet_score(list.moves[i]);
    }
    Move pick_best() {
        int best = idx;
        for (int i = idx + 1; i < list.n; i++)
            if (list.scores[i] > list.scores[best]) best = i;
        std::swap(list.moves[idx], list.moves[best]);
        std::swap(list.scores[idx], list.scores[best]);
        return list.moves[idx++];
    }

    Move next(bool skipQuiets) {
        while (true) {
            switch (stage) {
            case ST_TT:
                stage++;
                if (ttMove) return ttMove;
                break;
            case ST_GEN_CAP:
                list.n = 0;
                idx = 0;
                pos.generate(list, GEN_CAPTURES);
                score_captures();
                stage++;
                break;
            case ST_GOOD_CAP:
                while (idx < list.n) {
                    Move m = pick_best();
                    if (m == ttMove) continue;
                    if (!pos.see_ge(m, -list.scores[idx - 1] / 64)) {
                        if (nbad < 64) bad[nbad++] = m;
                        continue;
                    }
                    return m;
                }
                stage++;
                break;
            case ST_KILLER1:
                stage++;
                if (!skipQuiets && k1 && k1 != ttMove && !pos.is_tactical(k1) && pos.pseudo_legal(k1)) return k1;
                break;
            case ST_KILLER2:
                stage++;
                if (!skipQuiets && k2 && k2 != ttMove && k2 != k1 && !pos.is_tactical(k2) && pos.pseudo_legal(k2)) return k2;
                break;
            case ST_COUNTER:
                stage++;
                if (!skipQuiets && counter && counter != ttMove && counter != k1 && counter != k2 && !pos.is_tactical(counter) &&
                    pos.pseudo_legal(counter))
                    return counter;
                break;
            case ST_GEN_QUIET:
                stage++;
                list.n = 0;
                idx = 0;
                if (!skipQuiets) {
                    pos.generate(list, GEN_QUIETS);
                    score_quiets();
                }
                break;
            case ST_QUIET:
                while (!skipQuiets && idx < list.n) {
                    Move m = pick_best();
                    if (m == ttMove || m == k1 || m == k2 || m == counter) continue;
                    return m;
                }
                stage++;
                break;
            case ST_BAD_CAP:
                while (badIdx < nbad) {
                    Move m = bad[badIdx++];
                    if (m != ttMove) return m;
                }
                stage = ST_DONE;
                break;
            case ST_DONE:
                return MOVE_NONE;
            case QS_TT:
                stage++;
                if (ttMove) return ttMove;
                break;
            case QS_GEN:
                list.n = 0;
                idx = 0;
                pos.generate(list, GEN_CAPTURES);
                score_captures();
                stage++;
                break;
            case QS_CAP:
                while (idx < list.n) {
                    Move m = pick_best();
                    if (m == ttMove) continue;
                    return m;
                }
                return MOVE_NONE;
            case EV_TT:
                stage++;
                if (ttMove) return ttMove;
                break;
            case EV_GEN:
                list.n = 0;
                idx = 0;
                pos.generate(list, GEN_ALL);
                for (int i = 0; i < list.n; i++) {
                    Move m = list.moves[i];
                    if (pos.is_tactical(m)) list.scores[i] = 1000000 + capture_score(m);
                    else list.scores[i] = quiet_score(m);
                }
                stage++;
                break;
            case EV_ALL:
                while (idx < list.n) {
                    Move m = pick_best();
                    if (m == ttMove) continue;
                    return m;
                }
                return MOVE_NONE;
            }
        }
    }
};

// ---------------- search ----------------

static inline void check_time() {
    if ((nodes & 1023) == 0) {
        if (useTime && elapsed_ms() >= hardLimit) g_stop = true;
        if (nodeLimit && nodes >= (uint64_t)nodeLimit) g_stop = true;
    }
}

static int qsearch(Position &pos, Stack *ss, int alpha, int beta, int ply, bool pvNode) {
    pvLen[ply] = ply;
    nodes++;
    check_time();
    if (g_stop) return 0;
    if (ply > selDepth) selDepth = ply;
    if (pos.is_draw(ply)) return 0;
    bool inCheck = pos.in_check();
    if (ply >= MAX_PLY - 1) return inCheck ? 0 : evaluate(pos);

    U64 key = pos.st().key;
    bool found;
    TTEntry *tte = TT.probe(key, found);
    int ttScore = found ? score_from_tt(tte->score, ply, pos.st().rule50) : VALUE_NONE;
    int ttBound = found ? tte->bound() : BOUND_NONE;
    Move ttMove = found ? tte->move : MOVE_NONE;
    if (!pvNode && found && ttScore != VALUE_NONE &&
        ((ttBound == BOUND_EXACT) || (ttBound == BOUND_LOWER && ttScore >= beta) || (ttBound == BOUND_UPPER && ttScore <= alpha)))
        return ttScore;

    int bestScore, staticEval, futilityBase, rawEval = VALUE_NONE;
    if (inCheck) {
        staticEval = VALUE_NONE;
        bestScore = -VALUE_INF;
        futilityBase = -VALUE_INF;
    } else {
        rawEval = (found && tte->eval != VALUE_NONE) ? tte->eval : evaluate(pos);
        staticEval = corrected_eval(pos, rawEval);
        bestScore = staticEval;
        if (ttScore != VALUE_NONE && ((ttBound == BOUND_LOWER && ttScore > bestScore) || (ttBound == BOUND_UPPER && ttScore < bestScore) || ttBound == BOUND_EXACT))
            bestScore = ttScore;
        if (bestScore >= beta) {
            if (!found) TT.store(tte, key, MOVE_NONE, score_to_tt(bestScore, ply), rawEval, -1, BOUND_LOWER, false);
            return bestScore;
        }
        if (bestScore > alpha) alpha = bestScore;
        futilityBase = staticEval + 180;
    }

    ss->inCheck = inCheck;
    MovePicker mp(pos, ss, ttMove, true);
    Move bestMove = MOVE_NONE;
    int moveCount = 0;
    Move m;
    while ((m = mp.next(false)) != MOVE_NONE) {
        if (!pos.is_legal(m)) continue;
        moveCount++;
        if (!inCheck && bestScore > -MATE_IN_MAX) {
            if (move_type(m) != MT_PROMO && futilityBase + SeeVal[pos.captured_piece(m)] <= alpha) {
                bestScore = std::max(bestScore, futilityBase + SeeVal[pos.captured_piece(m)]);
                continue;
            }
            if (!pos.see_ge(m, 0)) continue;
        }
        if (inCheck && bestScore > -MATE_IN_MAX && moveCount > 3 && !pos.is_tactical(m)) continue;
        ss->move = m;
        ss->piece = pos.piece_on(from_sq(m));
        ss->cont = &contHist[ss->piece][to_sq(m)];
        pos.make_move(m);
        int score = -qsearch(pos, ss + 1, -beta, -alpha, ply + 1, pvNode);
        pos.unmake_move(m);
        if (g_stop) return 0;
        if (score > bestScore) {
            bestScore = score;
            if (score > alpha) {
                bestMove = m;
                alpha = score;
                if (alpha >= beta) break;
            }
        }
    }
    if (inCheck && moveCount == 0) return -VALUE_MATE + ply;

    int bound = bestScore >= beta ? BOUND_LOWER : BOUND_UPPER;
    TT.store(tte, key, bestMove, score_to_tt(bestScore, ply), rawEval, 0, bound, pvNode);
    return bestScore;
}

static void update_quiet_histories(Position &pos, Stack *ss, Move m, int bonus) {
    int pc = pos.piece_on(from_sq(m));
    int to = to_sq(m);
    update_hist(mainHist[pos.stm][m & 4095], bonus);
    update_hist((*(ss - 1)->cont)[pc][to], bonus);
    update_hist((*(ss - 2)->cont)[pc][to], bonus);
    update_hist((*(ss - 4)->cont)[pc][to], bonus / 2);
}

static int search(Position &pos, Stack *ss, int alpha, int beta, int depth, int ply, bool cutNode) {
    bool pvNode = beta - alpha > 1;
    bool root = ply == 0;
    pvLen[ply] = ply;

    if (depth <= 0) return qsearch(pos, ss, alpha, beta, ply, pvNode);

    nodes++;
    check_time();
    if (g_stop) return 0;
    if (ply > selDepth) selDepth = ply;

    bool inCheck = pos.in_check();
    if (!root) {
        if (pos.is_draw(ply)) return 0;
        if (ply >= MAX_PLY - 1) return inCheck ? 0 : evaluate(pos);
        // mate distance pruning
        alpha = std::max(alpha, -VALUE_MATE + ply);
        beta = std::min(beta, VALUE_MATE - ply - 1);
        if (alpha >= beta) return alpha;
    }

    Move excluded = ss->excluded;
    U64 key = pos.st().key;
    bool found = false;
    TTEntry *tte = TT.probe(key, found);
    if (excluded) found = false; // don't use TT in singular verification
    int ttScore = found ? score_from_tt(tte->score, ply, pos.st().rule50) : VALUE_NONE;
    int ttBound = found ? tte->bound() : BOUND_NONE;
    int ttDepth = found ? tte->depth - 1 : -10;
    Move ttMove = root ? pvTable[0][0] : (found ? tte->move : MOVE_NONE);
    if (root && !ttMove && found) ttMove = tte->move;
    bool ttCapture = ttMove && pos.pseudo_legal(ttMove) && pos.is_tactical(ttMove);

    if (!pvNode && found && ttDepth >= depth && ttScore != VALUE_NONE && pos.st().rule50 < 90 &&
        ((ttBound == BOUND_EXACT) || (ttBound == BOUND_LOWER && ttScore >= beta) || (ttBound == BOUND_UPPER && ttScore <= alpha))) {
        // reward TT move on cutoff
        if (ttMove && ttScore >= beta && !pos.is_tactical(ttMove) && pos.pseudo_legal(ttMove))
            update_quiet_histories(pos, ss, ttMove, hist_bonus(depth));
        return ttScore;
    }

    int staticEval, eval, rawEval = VALUE_NONE;
    ss->inCheck = inCheck;
    if (inCheck) {
        staticEval = eval = VALUE_NONE;
        ss->staticEval = VALUE_NONE;
    } else if (excluded) {
        staticEval = eval = ss->staticEval;
    } else {
        rawEval = (found && tte->eval != VALUE_NONE) ? tte->eval : evaluate(pos);
        staticEval = corrected_eval(pos, rawEval);
        eval = staticEval;
        ss->staticEval = staticEval;
        if (ttScore != VALUE_NONE && ((ttBound == BOUND_LOWER && ttScore > eval) || (ttBound == BOUND_UPPER && ttScore < eval) || ttBound == BOUND_EXACT))
            eval = ttScore;
        if (!found) TT.store(tte, key, MOVE_NONE, VALUE_NONE, rawEval, -1, BOUND_NONE, false);
    }

    bool improving = false;
    if (!inCheck) {
        if ((ss - 2)->staticEval != VALUE_NONE) improving = staticEval > (ss - 2)->staticEval;
        else if ((ss - 4)->staticEval != VALUE_NONE) improving = staticEval > (ss - 4)->staticEval;
        else improving = true;
    }

    (ss + 1)->killers[0] = (ss + 1)->killers[1] = MOVE_NONE;
    (ss + 1)->excluded = MOVE_NONE;

    if (!pvNode && !inCheck && !excluded) {
        // reverse futility pruning
        if (depth <= 8 && eval - 75 * (depth - improving) >= beta && eval < MATE_IN_MAX && beta > -MATE_IN_MAX)
            return (eval + beta) / 2;
        // razoring
        if (depth <= 3 && eval + 200 + 150 * depth < alpha) {
            int v = qsearch(pos, ss, alpha, alpha + 1, ply, false);
            if (v <= alpha) return v;
        }
        // null move pruning
        if (depth >= 3 && eval >= beta && staticEval >= beta - 20 * depth + 150 && (ss - 1)->move != MOVE_NULL &&
            pos.has_non_pawn(pos.stm) && beta > -MATE_IN_MAX) {
            int R = 4 + depth / 3 + std::min((eval - beta) / 200, 3);
            ss->move = MOVE_NULL;
            ss->piece = 0;
            ss->cont = &contDummy;
            pos.make_null();
            int v = -search(pos, ss + 1, -beta, -beta + 1, depth - R, ply + 1, !cutNode);
            pos.unmake_null();
            if (g_stop) return 0;
            if (v >= beta) {
                if (v >= MATE_IN_MAX) v = beta;
                if (depth < 12) return v;
                // verification at high depth
                int w = search(pos, ss, beta - 1, beta, depth - R, ply, false);
                if (w >= beta) return v;
            }
        }
        // probcut
        int pcBeta = beta + 200 - 50 * improving;
        if (depth >= 5 && std::abs(beta) < MATE_IN_MAX && !(found && ttDepth >= depth - 3 && ttScore != VALUE_NONE && ttScore < pcBeta)) {
            MovePicker mp(pos, ss, ttCapture ? ttMove : MOVE_NONE, true);
            Move m;
            while ((m = mp.next(false)) != MOVE_NONE) {
                if (!pos.is_legal(m) || !pos.see_ge(m, pcBeta - staticEval)) continue;
                ss->move = m;
                ss->piece = pos.piece_on(from_sq(m));
                ss->cont = &contHist[ss->piece][to_sq(m)];
                pos.make_move(m);
                int v = -qsearch(pos, ss + 1, -pcBeta, -pcBeta + 1, ply + 1, false);
                if (v >= pcBeta) v = -search(pos, ss + 1, -pcBeta, -pcBeta + 1, depth - 4, ply + 1, !cutNode);
                pos.unmake_move(m);
                if (g_stop) return 0;
                if (v >= pcBeta) {
                    TT.store(tte, key, m, score_to_tt(v, ply), rawEval, depth - 3, BOUND_LOWER, false);
                    return v;
                }
            }
        }
    }

    // internal iterative reduction
    if (depth >= 4 && !ttMove && (pvNode || cutNode)) depth--;

    MovePicker mp(pos, ss, ttMove, false);
    Move bestMove = MOVE_NONE;
    int bestScore = -VALUE_INF;
    int moveCount = 0;
    Move quietsTried[64];
    int nQuiets = 0;
    Move capsTried[32];
    int nCaps = 0;
    bool skipQuiets = false;
    int origAlpha = alpha;
    Move m;

    while ((m = mp.next(skipQuiets)) != MOVE_NONE) {
        if (m == excluded) continue;
        if (!pos.is_legal(m)) continue;
        moveCount++;
        bool quiet = !pos.is_tactical(m);
        int pc = pos.piece_on(from_sq(m));
        int hist = quiet ? mainHist[pos.stm][m & 4095] + (*(ss - 1)->cont)[pc][to_sq(m)] + (*(ss - 2)->cont)[pc][to_sq(m)]
                         : captHist[pc][to_sq(m)][pos.captured_piece(m) == NO_PIECE ? 0 : piece_type(pos.captured_piece(m))];

        int r = LMR[std::min(depth, 63)][std::min(moveCount, 63)];

        if (!root && bestScore > -MATE_IN_MAX && pos.has_non_pawn(pos.stm)) {
            int lmrDepth = std::max(0, depth - 1 - r);
            if (quiet) {
                if (moveCount >= (3 + depth * depth) / (2 - improving)) skipQuiets = true;
                if (skipQuiets) continue;
                if (!inCheck && lmrDepth <= 8 && staticEval + 100 + 90 * lmrDepth <= alpha) {
                    skipQuiets = true;
                    continue;
                }
                if (lmrDepth <= 4 && hist < -3000 * depth) continue;
                if (!pos.see_ge(m, -25 * lmrDepth * lmrDepth)) continue;
            } else {
                if (depth <= 8 && !pos.see_ge(m, -90 * depth)) continue;
            }
        }

        // extensions
        int ext = 0;
        if (!root && depth >= 7 && m == ttMove && !excluded && ttDepth >= depth - 3 && (ttBound & BOUND_LOWER) &&
            std::abs(ttScore) < MATE_IN_MAX && ply < 2 * rootDepth) {
            int sBeta = ttScore - 2 * depth;
            ss->excluded = m;
            int v = search(pos, ss, sBeta - 1, sBeta, (depth - 1) / 2, ply, cutNode);
            ss->excluded = MOVE_NONE;
            if (g_stop) return 0;
            if (v < sBeta) {
                ext = 1;
                if (!pvNode && v < sBeta - 25) ext = 2;
            } else if (sBeta >= beta) {
                return sBeta;
            } else if (ttScore >= beta) {
                ext = -1;
            }
        }

        ss->move = m;
        ss->piece = pc;
        ss->cont = &contHist[pc][to_sq(m)];
        uint64_t nodesBefore = nodes;
        (void)0;
        pos.make_move(m);
        bool givesCheck = pos.in_check();
        if (givesCheck && ext == 0) ext = 1;
        int newDepth = depth - 1 + ext;
        int score;

        if (depth >= 2 && moveCount > 1 + (pvNode ? 1 : 0) && (quiet || !ttCapture || cutNode)) {
            int rr = r;
            rr += !pvNode;
            rr += cutNode;
            rr -= improving;
            rr += ttCapture && quiet;
            rr -= givesCheck;
            rr -= (m == mp.k1 || m == mp.k2 || m == mp.counter);
            if (quiet) rr -= hist / 8000;
            else rr = std::max(0, rr - 1);
            int reduced = std::clamp(newDepth - rr, 1, newDepth);
            score = -search(pos, ss + 1, -alpha - 1, -alpha, reduced, ply + 1, true);
            if (score > alpha && reduced < newDepth)
                score = -search(pos, ss + 1, -alpha - 1, -alpha, newDepth, ply + 1, !cutNode);
        } else if (!pvNode || moveCount > 1) {
            score = -search(pos, ss + 1, -alpha - 1, -alpha, newDepth, ply + 1, !cutNode);
        } else {
            score = alpha + 1; // force full-window search below
        }
        if (pvNode && (moveCount == 1 || score > alpha)) {
            score = -search(pos, ss + 1, -beta, -alpha, newDepth, ply + 1, false);
        }
        pos.unmake_move(m);
        if (root) rootNodes[m & 4095] += nodes - nodesBefore;
        if (g_stop) return 0;

        if (score > bestScore) {
            bestScore = score;
            if (score > alpha) {
                bestMove = m;
                if (pvNode) {
                    pvTable[ply][ply] = m;
                    if (root) rootScore = score;
                    for (int i = ply + 1; i < pvLen[ply + 1]; i++) pvTable[ply][i] = pvTable[ply + 1][i];
                    pvLen[ply] = std::max(pvLen[ply + 1], ply + 1);
                }
                if (score >= beta) break;
                alpha = score;
            }
        }
        if (m != bestMove) {
            if (quiet && nQuiets < 64) quietsTried[nQuiets++] = m;
            else if (!quiet && nCaps < 32) capsTried[nCaps++] = m;
        }
    }

    if (moveCount == 0) {
        if (excluded) return alpha;
        return inCheck ? -VALUE_MATE + ply : 0;
    }

    if (bestScore >= beta && bestMove) {
        int bonus = hist_bonus(depth + (bestScore > beta + 100));
        if (!pos.is_tactical(bestMove)) {
            if (ss->killers[0] != bestMove) {
                ss->killers[1] = ss->killers[0];
                ss->killers[0] = bestMove;
            }
            Move prev = (ss - 1)->move;
            if (prev && prev != MOVE_NULL) counterMoves[(ss - 1)->piece][to_sq(prev)] = bestMove;
            update_quiet_histories(pos, ss, bestMove, bonus);
            for (int i = 0; i < nQuiets; i++) update_quiet_histories(pos, ss, quietsTried[i], -bonus);
        } else {
            int pc = pos.piece_on(from_sq(bestMove));
            int cap = pos.captured_piece(bestMove);
            update_hist(captHist[pc][to_sq(bestMove)][cap == NO_PIECE ? 0 : piece_type(cap)], bonus);
        }
        for (int i = 0; i < nCaps; i++) {
            Move c = capsTried[i];
            int pc = pos.piece_on(from_sq(c));
            int cap = pos.captured_piece(c);
            update_hist(captHist[pc][to_sq(c)][cap == NO_PIECE ? 0 : piece_type(cap)], -bonus);
        }
    }

    if (!excluded) {
        int bound = bestScore >= beta ? BOUND_LOWER : (pvNode && bestScore > origAlpha) ? BOUND_EXACT : BOUND_UPPER;
        TT.store(tte, key, bestMove, score_to_tt(bestScore, ply), rawEval, depth, bound, pvNode);
        if (!inCheck && (!bestMove || !pos.is_tactical(bestMove)) && std::abs(bestScore) < MATE_IN_MAX &&
            !(bound == BOUND_LOWER && bestScore <= staticEval) && !(bound == BOUND_UPPER && bestScore >= staticEval))
            update_corr(pos, depth, bestScore - staticEval);
    }
    return bestScore;
}

// ---------------- root / iterative deepening ----------------

static std::string score_str(int s) {
    if (s >= MATE_IN_MAX) return "mate " + std::to_string((VALUE_MATE - s + 1) / 2);
    if (s <= -MATE_IN_MAX) return "mate " + std::to_string(-(VALUE_MATE + s) / 2);
    return "cp " + std::to_string(s);
}

static Move bestPv[MAX_PLY + 2];
static int bestPvLen = 0;
static Move lastPrintedMove = MOVE_NONE;

static void save_best_pv() {
    bestPvLen = pvLen[0];
    for (int i = 0; i < pvLen[0]; i++) bestPv[i] = pvTable[0][i];
}

static void print_pv_info(Position &pos, int depth, int score, const Move *pvm, int len) {
    if (g_silent || len <= 0) return;
    int64_t ms = elapsed_ms();
    uint64_t nps = ms > 0 ? nodes * 1000 / ms : nodes * 1000;
    std::string pv;
    for (int i = 0; i < len; i++) {
        pv += " ";
        pv += pos.move_to_uci(pvm[i]);
    }
    lastPrintedMove = pvm[0];
    std::lock_guard<std::mutex> lk(g_ioMutex);
    printf("info depth %d seldepth %d multipv 1 score %s nodes %llu nps %llu hashfull %d time %lld pv%s\n", depth, selDepth,
           score_str(score).c_str(), (unsigned long long)nodes, (unsigned long long)nps, TT.hashfull(), (long long)ms, pv.c_str());
    fflush(stdout);
}

static Stack stackBuf[MAX_PLY + 16];

SearchResult think(Position &pos, const SearchLimits &lim, bool printBest) {
    startTime = std::chrono::steady_clock::now();
    SearchResult res;
    nodes = 0;
    memset(rootNodes, 0, sizeof(rootNodes));
    selDepth = 0;
    TT.new_search();
    g_stop = false;

    // time allocation
    useTime = false;
    nodeLimit = lim.nodes;
    softLimit = hardLimit = INT64_MAX / 4;
    int us = pos.stm;
    if (lim.movetime > 0) {
        useTime = true;
        softLimit = hardLimit = std::max<int64_t>(1, lim.movetime - g_moveOverhead);
    } else if (lim.time[us] >= 0 && !lim.infinite) {
        useTime = true;
        int64_t t = lim.time[us], inc = lim.inc[us];
        int64_t avail = std::max<int64_t>(1, t - g_moveOverhead);
        int mtg = lim.movestogo > 0 ? std::min(lim.movestogo, 40) : 25;
        double soft = (double)avail / mtg + 0.75 * inc;
        double hard = std::min(soft * 5.0, avail * 0.25 + inc * 0.5);
        if (lim.movestogo == 1) hard = avail * 0.5;
        hard = std::min(hard, avail * 0.7);
        soft = std::min(soft, hard);
        softLimit = std::max<int64_t>(1, (int64_t)soft);
        hardLimit = std::max<int64_t>(1, (int64_t)hard);
    }

    memset(stackBuf, 0, sizeof(stackBuf));
    for (int i = 0; i < MAX_PLY + 16; i++) {
        stackBuf[i].cont = &contDummy;
        stackBuf[i].staticEval = VALUE_NONE;
    }
    Stack *ss = stackBuf + 6;

    MoveList legal;
    pos.generate_legal(legal);
    if (legal.n == 0) {
        if (printBest && !g_silent) {
            std::lock_guard<std::mutex> lk(g_ioMutex);
            printf("info depth 0 score %s\nbestmove 0000\n", pos.in_check() ? "mate 0" : "cp 0");
            fflush(stdout);
        }
        return res;
    }
    Move bestMove = legal.moves[0];
    int bestScore = 0;
    pvLen[0] = 0;
    pvTable[0][0] = MOVE_NONE;
    bestPvLen = 0;
    lastPrintedMove = MOVE_NONE;

    int maxDepth = lim.depth > 0 ? std::min(lim.depth, MAX_PLY - 4) : MAX_PLY - 4;
    int prevScore = 0;
    int stable = 0;
    Move prevBest = MOVE_NONE;
    int completedDepth = 0;

    for (int depth = 1; depth <= maxDepth; depth++) {
        rootDepth = depth;
        selDepth = 0;
        int alpha = -VALUE_INF, beta = VALUE_INF, delta = 20;
        if (depth >= 4) {
            alpha = std::max(prevScore - delta, -VALUE_INF);
            beta = std::min(prevScore + delta, VALUE_INF);
        }
        int score;
        while (true) {
            score = search(pos, ss, alpha, beta, depth, 0, false);
            if (g_stop) break;
            if (score <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = std::max(score - delta, -VALUE_INF);
            } else if (score >= beta) {
                beta = std::min(score + delta, VALUE_INF);
                if (pvLen[0] > 0) { bestMove = pvTable[0][0]; save_best_pv(); }
            } else break;
            delta += delta / 2;
            if (delta > 1000) { alpha = -VALUE_INF; beta = VALUE_INF; }
        }
        if (g_stop && depth > 1) {
            // partial iteration: keep bestMove from root PV if it was updated with a real score
            if (pvLen[0] > 0 && pvTable[0][0]) { bestMove = pvTable[0][0]; save_best_pv(); bestScore = rootScore; }
            break;
        }
        completedDepth = depth;
        if (pvLen[0] > 0) { bestMove = pvTable[0][0]; save_best_pv(); }
        bestScore = score;
        print_pv_info(pos, depth, score, bestPv, bestPvLen);

        if (bestMove == prevBest) stable++;
        else stable = 0;
        prevBest = bestMove;

        if (useTime && lim.movetime <= 0) {
            double f = 1.25 - 0.08 * std::min(stable, 6);
            if (score < prevScore - 25) f *= 1.25;
            if (score < prevScore - 60) f *= 1.2;
            if (legal.n == 1) f *= 0.2;
            double frac = nodes ? (double)rootNodes[bestMove & 4095] / (double)nodes : 0.5;
#ifndef NO_NODE_TM
            f *= std::clamp(1.6 - 1.2 * frac, 0.6, 1.5);
#else
            (void)frac;
#endif
            int64_t e = elapsed_ms();
            if (e >= softLimit * f) break;
        } else if (useTime) {
            if (elapsed_ms() >= hardLimit) break;
        }
        prevScore = score;
        if (g_stop) break;
        if (nodeLimit && nodes >= (uint64_t)nodeLimit) break;
    }

    // for infinite / ponder: wait until stop
    if (lim.infinite) {
        while (!g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (lastPrintedMove != bestMove && bestPvLen > 0 && bestPv[0] == bestMove) print_pv_info(pos, completedDepth + 1, bestScore, bestPv, bestPvLen);
    res.best = bestMove;
    res.score = bestScore;
    res.depth = completedDepth;
    res.nodes = nodes;
    if (printBest && !g_silent) {
        std::lock_guard<std::mutex> lk(g_ioMutex);
        printf("bestmove %s\n", pos.move_to_uci(bestMove).c_str());
        fflush(stdout);
    }
    return res;
}
