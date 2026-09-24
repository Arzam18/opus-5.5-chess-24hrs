#include "position.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <chrono>
#include <vector>

U64 perft(Position &pos, int depth) {
    MoveList l;
    pos.generate(l, GEN_ALL);
    U64 n = 0;
    for (int i = 0; i < l.n; i++) {
        Move m = l.moves[i];
        if (!pos.is_legal(m)) continue;
        if (depth == 1) { n++; continue; }
        pos.make_move(m);
        n += perft(pos, depth - 1);
        pos.unmake_move(m);
    }
    return n;
}

void perft_divide(Position &pos, int depth) {
    auto t0 = std::chrono::steady_clock::now();
    MoveList l;
    pos.generate_legal(l);
    U64 total = 0;
    for (int i = 0; i < l.n; i++) {
        U64 n = 1;
        if (depth > 1) {
            pos.make_move(l.moves[i]);
            n = perft(pos, depth - 1);
            pos.unmake_move(l.moves[i]);
        }
        printf("%s: %llu\n", pos.move_to_uci(l.moves[i]).c_str(), (unsigned long long)n);
        total += n;
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("\nNodes searched: %llu  time %.0f ms  nps %.0f\n", (unsigned long long)total, ms, total / (ms / 1000.0 + 1e-9));
    fflush(stdout);
}

// Each line: FEN ;D1 n ;D2 n ...
void perft_suite(const std::string &file, int maxDepth) {
    std::ifstream in(file);
    if (!in) { printf("cannot open %s\n", file.c_str()); return; }
    std::string line;
    int ok = 0, bad = 0, idx = 0;
    U64 totalNodes = 0;
    auto t0 = std::chrono::steady_clock::now();
    static Position pos;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        idx++;
        size_t p = line.find(';');
        std::string fen = line.substr(0, p);
        pos.set_fen(fen);
        while (p != std::string::npos) {
            size_t q = line.find(';', p + 1);
            std::string tok = line.substr(p + 1, q == std::string::npos ? std::string::npos : q - p - 1);
            p = q;
            int d;
            unsigned long long expected;
            if (sscanf(tok.c_str(), " D%d %llu", &d, &expected) != 2) continue;
            if (d > maxDepth) continue;
            U64 got = perft(pos, d);
            totalNodes += got;
            if (got != expected) {
                bad++;
                printf("FAIL #%d depth %d: got %llu expected %llu  fen %s\n", idx, d, (unsigned long long)got, expected, fen.c_str());
            } else ok++;
        }
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("perft suite: %d ok, %d failed, %llu nodes, %.1f s, %.0f nps\n", ok, bad, (unsigned long long)totalNodes, ms / 1000,
           totalNodes / (ms / 1000.0 + 1e-9));
    fflush(stdout);
}
