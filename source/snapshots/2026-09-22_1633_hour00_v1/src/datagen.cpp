// Self-play data generation for NNUE training.
// Usage: engine datagen <outfile> <games> <nodes> <seed>
#include "position.h"
#include "search.h"
#include "tt.h"
#include <cstdio>
#include <vector>
#include <chrono>

#pragma pack(push, 1)
struct PackedPos {
    uint64_t occ;
    uint8_t pcs[16]; // nibbles, in occupancy lsb order
    int16_t score;   // white-relative cp
    uint8_t result;  // 0 black win, 1 draw, 2 white win
    uint8_t stm;
    uint16_t ply;
    uint16_t pad;
};
#pragma pack(pop)
static_assert(sizeof(PackedPos) == 32, "packed size");

static uint64_t rng_s;
static uint64_t rng() {
    rng_s ^= rng_s << 13;
    rng_s ^= rng_s >> 7;
    rng_s ^= rng_s << 17;
    return rng_s;
}

static PackedPos pack(const Position &pos, int whiteScore, int ply) {
    PackedPos p;
    memset(&p, 0, sizeof(p));
    p.occ = pos.occupied();
    U64 b = p.occ;
    int i = 0;
    while (b) {
        int s = pop_lsb(b);
        int pc = pos.piece_on(s);
        p.pcs[i / 2] |= (uint8_t)(pc << (4 * (i & 1)));
        i++;
    }
    p.score = (int16_t)whiteScore;
    p.stm = (uint8_t)pos.stm;
    p.ply = (uint16_t)ply;
    return p;
}

static bool insufficient(const Position &pos) {
    if (pos.pieces(PAWN) | pos.pieces(ROOK) | pos.pieces(QUEEN)) return false;
    int minors = popcount(pos.pieces(KNIGHT) | pos.pieces(BISHOP));
    return minors <= 1;
}

int datagen_main(int argc, char **argv) {
    if (argc < 6) {
        printf("usage: datagen <out> <games> <nodes> <seed>\n");
        return 1;
    }
    const char *out = argv[2];
    int games = atoi(argv[3]);
    int nodes = atoi(argv[4]);
    rng_s = 0x1234567ULL ^ (uint64_t)atoll(argv[5]) * 0x9E3779B97F4A7C15ULL;
    if (!rng_s) rng_s = 1;
    g_silent = true;
    TT.resize(16);
    FILE *f = fopen(out, "ab");
    if (!f) { printf("cannot open %s\n", out); return 1; }
    static Position pos;
    std::vector<PackedPos> recs;
    uint64_t total = 0;
    auto t0 = std::chrono::steady_clock::now();
    int wW = 0, dD = 0, bW = 0;
    for (int g = 0; g < games; g++) {
        search_clear();
        pos.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        int nrand = 8 + (int)(rng() % 2);
        bool ok = true;
        for (int i = 0; i < nrand; i++) {
            MoveList l;
            pos.generate_legal(l);
            if (l.n == 0) { ok = false; break; }
            pos.make_move(l.moves[rng() % l.n]);
        }
        if (!ok) continue;
        {
            MoveList l;
            pos.generate_legal(l);
            if (l.n == 0) continue;
        }
        // reject very unbalanced starts
        SearchLimits lim0;
        lim0.nodes = 2000;
        SearchResult r0 = think(pos, lim0, false);
        if (std::abs(r0.score) > 400) continue;

        recs.clear();
        int result = -1;
        int winPlies = 0, drawPlies = 0;
        for (int ply = 0; ply < 500; ply++) {
            MoveList l;
            pos.generate_legal(l);
            if (l.n == 0) {
                result = pos.in_check() ? (pos.stm == WHITE ? 0 : 2) : 1;
                break;
            }
            if (pos.is_draw(0) || insufficient(pos)) { result = 1; break; }
            SearchLimits lim;
            lim.nodes = nodes;
            SearchResult r = think(pos, lim, false);
            int score = r.score;
            int ws = pos.stm == WHITE ? score : -score;
            if (std::abs(score) >= MATE_IN_MAX) {
                result = ws > 0 ? 2 : 0;
                break;
            }
            if (std::abs(score) > 1500) {
                if (++winPlies >= 6) { result = ws > 0 ? 2 : 0; break; }
            } else winPlies = 0;
            if (ply >= 80 && std::abs(score) <= 8) {
                if (++drawPlies >= 12) { result = 1; break; }
            } else drawPlies = 0;
            if (!pos.in_check() && !pos.is_tactical(r.best) && std::abs(score) < 2000)
                recs.push_back(pack(pos, ws, pos.gamePly));
            pos.make_move(r.best);
        }
        if (result < 0) result = 1;
        if (result == 2) wW++; else if (result == 1) dD++; else bW++;
        for (auto &p : recs) p.result = (uint8_t)result;
        fwrite(recs.data(), sizeof(PackedPos), recs.size(), f);
        total += recs.size();
        if ((g + 1) % 50 == 0) {
            fflush(f);
            double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            fprintf(stderr, "games %d positions %llu  %.0f pos/s  W/D/L %d/%d/%d\n", g + 1, (unsigned long long)total, total / s, wW, dD, bW);
        }
    }
    fclose(f);
    return 0;
}
