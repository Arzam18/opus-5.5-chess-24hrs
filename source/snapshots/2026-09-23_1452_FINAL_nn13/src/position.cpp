#include "position.h"
#include <sstream>
#include <cctype>
#include <algorithm>

namespace Zobrist {
U64 psq[12][64];
U64 castle[16];
U64 epFile[8];
U64 side;
static U64 rng_state = 0x9E3779B97F4A7C15ULL;
static U64 rnd() {
    // splitmix64
    U64 z = (rng_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
void init() {
    for (int p = 0; p < 12; p++)
        for (int s = 0; s < 64; s++) psq[p][s] = rnd();
    for (int i = 0; i < 16; i++) castle[i] = rnd();
    castle[0] = 0;
    for (int i = 0; i < 8; i++) epFile[i] = rnd();
    side = rnd();
}
} // namespace Zobrist

const int CastleMask[64] = {
    15 & ~WQ_CASTLE, 15, 15, 15, 15 & ~(WK_CASTLE | WQ_CASTLE), 15, 15, 15 & ~WK_CASTLE,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15 & ~BQ_CASTLE, 15, 15, 15, 15 & ~(BK_CASTLE | BQ_CASTLE), 15, 15, 15 & ~BK_CASTLE,
};

static const int SeeValue[13] = {100, 320, 330, 500, 950, 0, 100, 320, 330, 500, 950, 0, 0};

void Position::put_piece(int p, int s) {
    board[s] = p;
    byType[piece_type(p)] |= sqbb(s);
    byColor[piece_color(p)] |= sqbb(s);
}
void Position::remove_piece(int s) {
    int p = board[s];
    byType[piece_type(p)] ^= sqbb(s);
    byColor[piece_color(p)] ^= sqbb(s);
    board[s] = NO_PIECE;
}
void Position::move_piece(int from, int to) {
    int p = board[from];
    U64 ft = sqbb(from) | sqbb(to);
    byType[piece_type(p)] ^= ft;
    byColor[piece_color(p)] ^= ft;
    board[from] = NO_PIECE;
    board[to] = p;
}

U64 Position::attackers_to(int s, U64 occ) const {
    return (PawnAttacks[WHITE][s] & pieces(BLACK, PAWN)) | (PawnAttacks[BLACK][s] & pieces(WHITE, PAWN)) |
           (KnightAttacks[s] & byType[KNIGHT]) | (KingAttacks[s] & byType[KING]) |
           (bishop_attacks(s, occ) & (byType[BISHOP] | byType[QUEEN])) |
           (rook_attacks(s, occ) & (byType[ROOK] | byType[QUEEN]));
}

void Position::compute_check_info() {
    StateInfo &s = st();
    int us = stm, them = !stm;
    int ksq = king_sq(us);
    U64 occ = occupied();
    s.checkers = attackers_to(ksq, occ) & byColor[them];
    U64 snipers = ((rook_attacks(ksq, 0) & (byType[ROOK] | byType[QUEEN])) |
                   (bishop_attacks(ksq, 0) & (byType[BISHOP] | byType[QUEEN]))) & byColor[them];
    U64 pinned = 0;
    while (snipers) {
        int sq = pop_lsb(snipers);
        U64 b = BetweenBB[ksq][sq] & occ;
        if (b && !(b & (b - 1)) && (b & byColor[us])) pinned |= b;
    }
    s.pinned = pinned;
}

U64 Position::compute_key() const {
    U64 k = 0;
    for (int s = 0; s < 64; s++)
        if (board[s] != NO_PIECE) k ^= Zobrist::psq[board[s]][s];
    k ^= Zobrist::castle[st().castle];
    if (st().ep != NO_SQ) k ^= Zobrist::epFile[file_of(st().ep)];
    if (stm == BLACK) k ^= Zobrist::side;
    return k;
}
U64 Position::compute_pawn_key() const {
    U64 k = 0;
    for (int s = 0; s < 64; s++)
        if (board[s] != NO_PIECE && (piece_type(board[s]) == PAWN || piece_type(board[s]) == KING))
            k ^= Zobrist::psq[board[s]][s];
    return k;
}

void Position::set_fen(const std::string &fen) {
    for (int i = 0; i < 64; i++) board[i] = NO_PIECE;
    for (int i = 0; i < 6; i++) byType[i] = 0;
    byColor[0] = byColor[1] = 0;
    sp = 0;
    gamePly = 0;
    StateInfo &s = states[0];
    memset(&s, 0, sizeof(s));
    std::istringstream ss(fen);
    std::string placement, side, castle, ep;
    int r50 = 0, fullmove = 1;
    ss >> placement >> side >> castle >> ep;
    if (!(ss >> r50)) r50 = 0;
    if (!(ss >> fullmove)) fullmove = 1;
    int r = 7, f = 0;
    for (char c : placement) {
        if (c == '/') { r--; f = 0; continue; }
        if (isdigit((unsigned char)c)) { f += c - '0'; continue; }
        const char *pcs = "PNBRQKpnbrqk";
        const char *p = strchr(pcs, c);
        if (p && f < 8 && r >= 0) put_piece(int(p - pcs), make_sq(f, r));
        f++;
    }
    stm = (side == "b") ? BLACK : WHITE;
    s.castle = 0;
    for (char c : castle) {
        if (c == 'K') s.castle |= WK_CASTLE;
        if (c == 'Q') s.castle |= WQ_CASTLE;
        if (c == 'k') s.castle |= BK_CASTLE;
        if (c == 'q') s.castle |= BQ_CASTLE;
    }
    // sanity: rights require king/rook on home squares
    if (board[E1] != WK) s.castle &= ~(WK_CASTLE | WQ_CASTLE);
    if (board[H1] != WR) s.castle &= ~WK_CASTLE;
    if (board[A1] != WR) s.castle &= ~WQ_CASTLE;
    if (board[E8] != BK) s.castle &= ~(BK_CASTLE | BQ_CASTLE);
    if (board[H8] != BR) s.castle &= ~BK_CASTLE;
    if (board[A8] != BR) s.castle &= ~BQ_CASTLE;
    s.ep = NO_SQ;
    if (ep.size() == 2 && ep[0] >= 'a' && ep[0] <= 'h' && ep[1] >= '1' && ep[1] <= '8') {
        int sq = make_sq(ep[0] - 'a', ep[1] - '1');
        // only keep ep if a capture is possible
        if (PawnAttacks[!stm][sq] & pieces(stm, PAWN)) s.ep = sq;
    }
    s.rule50 = r50;
    s.pliesFromNull = 0;
    s.captured = NO_PIECE;
    s.move = MOVE_NONE;
    s.dirty.n = 0;
    s.accComputed = false;
    gamePly = std::max(2 * (fullmove - 1), 0) + (stm == BLACK);
    s.key = compute_key();
    s.pawnKey = compute_pawn_key();
    s.npKey[0] = s.npKey[1] = 0;
    for (int sq = 0; sq < 64; sq++)
        if (board[sq] != NO_PIECE && piece_type(board[sq]) != PAWN) s.npKey[piece_color(board[sq])] ^= Zobrist::psq[board[sq]][sq];
    compute_check_info();
}

std::string Position::fen() const {
    std::string out;
    const char *pcs = "PNBRQKpnbrqk";
    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            int p = board[make_sq(f, r)];
            if (p == NO_PIECE) { empty++; continue; }
            if (empty) { out += char('0' + empty); empty = 0; }
            out += pcs[p];
        }
        if (empty) out += char('0' + empty);
        if (r) out += '/';
    }
    out += stm == WHITE ? " w " : " b ";
    int c = st().castle;
    if (!c) out += '-';
    if (c & WK_CASTLE) out += 'K';
    if (c & WQ_CASTLE) out += 'Q';
    if (c & BK_CASTLE) out += 'k';
    if (c & BQ_CASTLE) out += 'q';
    out += ' ';
    if (st().ep == NO_SQ) out += '-';
    else { out += char('a' + file_of(st().ep)); out += char('1' + rank_of(st().ep)); }
    out += " " + std::to_string(st().rule50) + " " + std::to_string(gamePly / 2 + 1);
    return out;
}

static inline void add_dirty(DirtyPiece &d, int p, int from, int to) {
    d.piece[d.n] = p;
    d.from[d.n] = from;
    d.to[d.n] = to;
    d.n++;
}

void Position::make_move(Move m) {
    StateInfo &os = states[sp];
    StateInfo &ns = states[sp + 1];
    ns.key = os.key ^ Zobrist::side;
    ns.pawnKey = os.pawnKey;
    ns.castle = os.castle;
    ns.ep = NO_SQ;
    ns.rule50 = os.rule50 + 1;
    ns.pliesFromNull = os.pliesFromNull + 1;
    ns.move = m;
    ns.captured = NO_PIECE;
    ns.dirty.n = 0;
    ns.accComputed = false;
    if (os.ep != NO_SQ) ns.key ^= Zobrist::epFile[file_of(os.ep)];
    sp++;
    gamePly++;

    int us = stm, them = !stm;
    int from = from_sq(m), to = to_sq(m);
    int pc = board[from];
    int type = move_type(m);

    if (type == MT_CASTLE) {
        bool kside = to > from;
        int rfrom = kside ? from + 3 : from - 4;
        int rto = kside ? from + 1 : from - 1;
        int rook = board[rfrom];
        move_piece(from, to);
        move_piece(rfrom, rto);
        ns.key ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to] ^ Zobrist::psq[rook][rfrom] ^ Zobrist::psq[rook][rto];
        ns.pawnKey ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
        add_dirty(ns.dirty, pc, from, to);
        add_dirty(ns.dirty, rook, rfrom, rto);
    } else {
        if (type == MT_EP) {
            int capsq = to + (us == WHITE ? -8 : 8);
            int cap = board[capsq];
            remove_piece(capsq);
            ns.key ^= Zobrist::psq[cap][capsq];
            ns.pawnKey ^= Zobrist::psq[cap][capsq];
            ns.captured = cap;
            add_dirty(ns.dirty, cap, capsq, NO_SQ);
        } else if (board[to] != NO_PIECE) {
            int cap = board[to];
            remove_piece(to);
            ns.key ^= Zobrist::psq[cap][to];
            if (piece_type(cap) == PAWN) ns.pawnKey ^= Zobrist::psq[cap][to];
            ns.captured = cap;
            ns.rule50 = 0;
            add_dirty(ns.dirty, cap, to, NO_SQ);
        }
        move_piece(from, to);
        ns.key ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
        int pt = piece_type(pc);
        if (pt == PAWN || pt == KING) ns.pawnKey ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
        if (pt == PAWN) {
            ns.rule50 = 0;
            if ((to ^ from) == 16) {
                int epsq = (from + to) / 2;
                if (PawnAttacks[us][epsq] & pieces(them, PAWN)) {
                    ns.ep = epsq;
                    ns.key ^= Zobrist::epFile[file_of(epsq)];
                }
            } else if (type == MT_PROMO) {
                int promo = make_piece(us, promo_type(m));
                remove_piece(to);
                put_piece(promo, to);
                ns.key ^= Zobrist::psq[pc][to] ^ Zobrist::psq[promo][to];
                ns.pawnKey ^= Zobrist::psq[pc][to];
                add_dirty(ns.dirty, pc, from, NO_SQ);
                add_dirty(ns.dirty, promo, NO_SQ, to);
                goto done;
            }
        }
        add_dirty(ns.dirty, pc, from, to);
    }
done:
    ns.npKey[0] = os.npKey[0];
    ns.npKey[1] = os.npKey[1];
    for (int i = 0; i < ns.dirty.n; i++) {
        int p = ns.dirty.piece[i];
        if (piece_type(p) == PAWN) continue;
        if (ns.dirty.from[i] != NO_SQ) ns.npKey[piece_color(p)] ^= Zobrist::psq[p][ns.dirty.from[i]];
        if (ns.dirty.to[i] != NO_SQ) ns.npKey[piece_color(p)] ^= Zobrist::psq[p][ns.dirty.to[i]];
    }
    if (ns.castle && (CastleMask[from] & CastleMask[to]) != 15) {
        int nc = ns.castle & CastleMask[from] & CastleMask[to];
        ns.key ^= Zobrist::castle[ns.castle] ^ Zobrist::castle[nc];
        ns.castle = nc;
    }
    stm = them;
    compute_check_info();
}

void Position::unmake_move(Move m) {
    stm = !stm;
    int us = stm;
    int from = from_sq(m), to = to_sq(m);
    int type = move_type(m);
    StateInfo &s = states[sp];
    if (type == MT_CASTLE) {
        bool kside = to > from;
        int rfrom = kside ? from + 3 : from - 4;
        int rto = kside ? from + 1 : from - 1;
        move_piece(to, from);
        move_piece(rto, rfrom);
    } else {
        if (type == MT_PROMO) {
            remove_piece(to);
            put_piece(make_piece(us, PAWN), to);
        }
        move_piece(to, from);
        if (type == MT_EP) {
            int capsq = to + (us == WHITE ? -8 : 8);
            put_piece(s.captured, capsq);
        } else if (s.captured != NO_PIECE) {
            put_piece(s.captured, to);
        }
    }
    sp--;
    gamePly--;
}

void Position::make_null() {
    StateInfo &os = states[sp];
    StateInfo &ns = states[sp + 1];
    ns = os;
    ns.key ^= Zobrist::side;
    if (os.ep != NO_SQ) ns.key ^= Zobrist::epFile[file_of(os.ep)];
    ns.ep = NO_SQ;
    ns.rule50 = os.rule50 + 1;
    ns.pliesFromNull = 0;
    ns.captured = NO_PIECE;
    ns.move = MOVE_NULL;
    ns.dirty.n = 0;
    ns.accComputed = false;
    sp++;
    gamePly++;
    stm = !stm;
    compute_check_info();
}

void Position::unmake_null() {
    stm = !stm;
    sp--;
    gamePly--;
}

bool Position::is_draw(int ply) const {
    const StateInfo &s = st();
    if (s.rule50 >= 100) {
        if (!s.checkers) return true;
        // checkmate takes precedence: caller handles mate via move generation; treat as draw unless mated
        MoveList ml;
        Position &self = const_cast<Position &>(*this);
        self.generate_legal(ml);
        return ml.n > 0;
    }
    // insufficient material: K vs K, K+minor vs K
    if (!(byType[PAWN] | byType[ROOK] | byType[QUEEN]) && popcount(byType[KNIGHT] | byType[BISHOP]) <= 1) return true;
    int end = std::min(s.rule50, s.pliesFromNull);
    if (end > sp) end = sp;
    int reps = 0;
    for (int i = 4; i <= end; i += 2) {
        if (states[sp - i].key == s.key) {
            // repetition within search tree (after root) -> draw; before root need 2 earlier occurrences
            if (i <= ply) return true;
            reps++;
            if (reps >= 2) return true;
        }
    }
    return false;
}

bool Position::see_ge(Move m, int threshold) const {
    if (move_type(m) != MT_NORMAL) return 0 >= threshold;
    int from = from_sq(m), to = to_sq(m);
    int swap = SeeValue[board[to]] - threshold;
    if (swap < 0) return false;
    swap = SeeValue[board[from]] - swap;
    if (swap <= 0) return true;
    U64 occ = occupied() ^ sqbb(from) ^ sqbb(to);
    int side = stm;
    U64 attackers = attackers_to(to, occ);
    U64 bishops = byType[BISHOP] | byType[QUEEN];
    U64 rooks = byType[ROOK] | byType[QUEEN];
    int res = 1;
    while (true) {
        side = !side;
        attackers &= occ;
        U64 sa = attackers & byColor[side];
        if (!sa) break;
        res ^= 1;
        U64 bb;
        if ((bb = sa & byType[PAWN])) {
            if ((swap = SeeValue[WP] - swap) < res) break;
            occ ^= sqbb(lsb(bb));
            attackers |= bishop_attacks(to, occ) & bishops;
        } else if ((bb = sa & byType[KNIGHT])) {
            if ((swap = SeeValue[WN] - swap) < res) break;
            occ ^= sqbb(lsb(bb));
        } else if ((bb = sa & byType[BISHOP])) {
            if ((swap = SeeValue[WB] - swap) < res) break;
            occ ^= sqbb(lsb(bb));
            attackers |= bishop_attacks(to, occ) & bishops;
        } else if ((bb = sa & byType[ROOK])) {
            if ((swap = SeeValue[WR] - swap) < res) break;
            occ ^= sqbb(lsb(bb));
            attackers |= rook_attacks(to, occ) & rooks;
        } else if ((bb = sa & byType[QUEEN])) {
            if ((swap = SeeValue[WQ] - swap) < res) break;
            occ ^= sqbb(lsb(bb));
            attackers |= (bishop_attacks(to, occ) & bishops) | (rook_attacks(to, occ) & rooks);
        } else {
            // king: can only capture if opponent has no more attackers
            return (attackers & ~byColor[side]) ? res ^ 1 : res;
        }
    }
    return res;
}

// ---------------- move generation ----------------

static inline void add_promos(MoveList &l, int from, int to, GenType type) {
    if (type != GEN_QUIETS) l.add(make_promo(from, to, QUEEN));
    if (type != GEN_CAPTURES) {
        l.add(make_promo(from, to, KNIGHT));
        l.add(make_promo(from, to, ROOK));
        l.add(make_promo(from, to, BISHOP));
    }
}

void Position::generate(MoveList &list, GenType type) const {
    int us = stm, them = !stm;
    U64 occ = occupied();
    U64 own = byColor[us], enemy = byColor[them];
    U64 targets = type == GEN_CAPTURES ? enemy : type == GEN_QUIETS ? ~occ : ~own;

    // pawns
    U64 pawns = pieces(us, PAWN);
    U64 rank7 = us == WHITE ? RankBB[6] : RankBB[1];
    U64 rank3 = us == WHITE ? RankBB[2] : RankBB[5];
    int up = us == WHITE ? 8 : -8;
    U64 p7 = pawns & rank7, pn7 = pawns & ~rank7;
    if (type != GEN_CAPTURES) {
        U64 b1 = pawn_push(us, pn7) & ~occ;
        U64 b2 = pawn_push(us, b1 & rank3) & ~occ;
        while (b1) { int t = pop_lsb(b1); list.add(encode_move(t - up, t)); }
        while (b2) { int t = pop_lsb(b2); list.add(encode_move(t - up - up, t)); }
    }
    if (type != GEN_QUIETS) {
        U64 ce = (us == WHITE ? shift_east(pn7) << 8 : shift_east(pn7) >> 8) & enemy;
        U64 cw = (us == WHITE ? shift_west(pn7) << 8 : shift_west(pn7) >> 8) & enemy;
        while (ce) { int t = pop_lsb(ce); list.add(encode_move(t - up - 1, t)); }
        while (cw) { int t = pop_lsb(cw); list.add(encode_move(t - up + 1, t)); }
        if (st().ep != NO_SQ) {
            U64 b = PawnAttacks[them][st().ep] & pn7;
            while (b) list.add(encode_move(pop_lsb(b), st().ep, MT_EP));
        }
    }
    if (p7) {
        U64 pp = pawn_push(us, p7) & ~occ;
        while (pp) { int t = pop_lsb(pp); add_promos(list, t - up, t, type); }
        U64 ce = (us == WHITE ? shift_east(p7) << 8 : shift_east(p7) >> 8) & enemy;
        U64 cw = (us == WHITE ? shift_west(p7) << 8 : shift_west(p7) >> 8) & enemy;
        while (ce) { int t = pop_lsb(ce); add_promos(list, t - up - 1, t, type); }
        while (cw) { int t = pop_lsb(cw); add_promos(list, t - up + 1, t, type); }
    }
    // pieces
    for (int pt = KNIGHT; pt <= KING; pt++) {
        U64 b = pieces(us, pt);
        while (b) {
            int from = pop_lsb(b);
            U64 a = attacks_bb(pt, from, occ) & targets;
            while (a) list.add(encode_move(from, pop_lsb(a)));
        }
    }
    // castling
    if (type != GEN_CAPTURES && !st().checkers) {
        int c = st().castle;
        if (us == WHITE) {
            if ((c & WK_CASTLE) && !(occ & (sqbb(F1) | sqbb(G1))) && !attacked_by(F1, them, occ) && !attacked_by(G1, them, occ))
                list.add(encode_move(E1, G1, MT_CASTLE));
            if ((c & WQ_CASTLE) && !(occ & (sqbb(B1) | sqbb(C1) | sqbb(D1))) && !attacked_by(D1, them, occ) && !attacked_by(C1, them, occ))
                list.add(encode_move(E1, C1, MT_CASTLE));
        } else {
            if ((c & BK_CASTLE) && !(occ & (sqbb(F8) | sqbb(G8))) && !attacked_by(F8, them, occ) && !attacked_by(G8, them, occ))
                list.add(encode_move(E8, G8, MT_CASTLE));
            if ((c & BQ_CASTLE) && !(occ & (sqbb(B8) | sqbb(C8) | sqbb(D8))) && !attacked_by(D8, them, occ) && !attacked_by(C8, them, occ))
                list.add(encode_move(E8, C8, MT_CASTLE));
        }
    }
}

bool Position::is_legal(Move m) const {
    int us = stm, them = !stm;
    int from = from_sq(m), to = to_sq(m);
    int ksq = king_sq(us);
    int type = move_type(m);
    if (type == MT_CASTLE) return true; // verified at generation
    U64 occ = occupied();
    if (type == MT_EP) {
        int capsq = to + (us == WHITE ? -8 : 8);
        U64 o2 = (occ ^ sqbb(from) ^ sqbb(capsq)) | sqbb(to);
        U64 att = attackers_to(ksq, o2) & byColor[them] & ~sqbb(capsq);
        return !att;
    }
    if (from == ksq) {
        return !(attackers_to(to, occ ^ sqbb(from)) & byColor[them]);
    }
    U64 chk = st().checkers;
    if (chk) {
        if (chk & (chk - 1)) return false;
        if (!((BetweenBB[ksq][lsb(chk)] | chk) & sqbb(to))) return false;
    }
    if (st().pinned & sqbb(from)) return (LineBB[from][ksq] & sqbb(to)) != 0;
    return true;
}

bool Position::pseudo_legal(Move m) const {
    if (m == MOVE_NONE || m == MOVE_NULL) return false;
    int us = stm;
    int from = from_sq(m), to = to_sq(m);
    int pc = board[from];
    if (pc == NO_PIECE || piece_color(pc) != us) return false;
    int type = move_type(m);
    int pt = piece_type(pc);
    if (type == MT_CASTLE) {
        if (pt != KING) return false;
        MoveList l;
        generate(l, GEN_QUIETS);
        for (int i = 0; i < l.n; i++)
            if (l.moves[i] == m) return true;
        return false;
    }
    if (board[to] != NO_PIECE && piece_color(board[to]) == us) return false;
    U64 occ = occupied();
    if (pt == PAWN) {
        int up = us == WHITE ? 8 : -8;
        bool promoRank = relative_rank(us, to) == 7;
        if (type == MT_EP) return to == st().ep && (PawnAttacks[us][from] & sqbb(to));
        if ((type == MT_PROMO) != promoRank) return false;
        if (PawnAttacks[us][from] & sqbb(to)) return board[to] != NO_PIECE;
        if (to == from + up) return board[to] == NO_PIECE;
        if (to == from + 2 * up && relative_rank(us, from) == 1)
            return board[to] == NO_PIECE && board[from + up] == NO_PIECE;
        return false;
    }
    if (type != MT_NORMAL) return false;
    return (attacks_bb(pt, from, occ) & sqbb(to)) != 0;
}

void Position::generate_legal(MoveList &list) {
    MoveList all;
    generate(all, GEN_ALL);
    list.n = 0;
    for (int i = 0; i < all.n; i++)
        if (is_legal(all.moves[i])) list.add(all.moves[i]);
}

std::string Position::move_to_uci(Move m) const {
    if (m == MOVE_NONE) return "0000";
    std::string s;
    s += char('a' + file_of(from_sq(m)));
    s += char('1' + rank_of(from_sq(m)));
    s += char('a' + file_of(to_sq(m)));
    s += char('1' + rank_of(to_sq(m)));
    if (move_type(m) == MT_PROMO) s += "nbrq"[promo_type(m) - KNIGHT];
    return s;
}

Move Position::parse_uci(const std::string &str) {
    std::string s = str;
    for (auto &c : s) c = (char)tolower((unsigned char)c);
    MoveList l;
    generate_legal(l);
    for (int i = 0; i < l.n; i++)
        if (move_to_uci(l.moves[i]) == s) return l.moves[i];
    return MOVE_NONE;
}
