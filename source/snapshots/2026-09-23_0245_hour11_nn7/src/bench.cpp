#include "position.h"
#include "search.h"
#include "tt.h"
#include <chrono>
#include <cstdio>

static const char *BenchFens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
    "2r3k1/pp3ppp/2n1b3/3p4/3P4/2PB1N2/P4PPP/R5K1 w - - 0 20",
    "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP1B1PPP/R2QKB1R w KQ - 0 8",
    "8/8/4k3/8/2p5/8/B2K4/8 w - - 0 1",
    "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
    "r2q1rk1/ppp2ppp/2n1bn2/2bpp3/4P3/2PP1NP1/PP1N1PBP/R1BQ1RK1 w - - 0 8",
    "3r2k1/1p3ppp/p1n5/2p5/2P1b3/1P2B1P1/P4P1P/3R2K1 w - - 0 25",
    "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
    "r1b1k2r/ppppnppp/2n2q2/2b5/3NP3/2P1B3/PP3PPP/RN1QKB1R w KQkq - 0 1",
    "8/5pk1/6p1/8/5P2/6P1/5K2/8 w - - 0 1",
};

void bench(int depth) {
    static Position pos;
    uint64_t total = 0;
    auto t0 = std::chrono::steady_clock::now();
    g_silent = true;
    for (const char *f : BenchFens) {
        search_clear();
        pos.set_fen(f);
        SearchLimits lim;
        lim.depth = depth;
        SearchResult r = think(pos, lim, false);
        total += r.nodes;
    }
    g_silent = false;
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("Bench: %llu nodes %.2f s %.0f nps\n", (unsigned long long)total, s, total / s);
}

