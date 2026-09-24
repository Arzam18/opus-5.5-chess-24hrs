#pragma once
#include "position.h"

// (768 -> HIDDEN) x2 perspectives -> SCReLU -> OUTPUT_BUCKETS (by piece count)
#ifndef NNUE_HIDDEN
#define NNUE_HIDDEN 256
#endif
const int HIDDEN = NNUE_HIDDEN;
const int OUT_BUCKETS = 8;
const int NNUE_QA = 255;
const int NNUE_QB = 64;
const int NNUE_SCALE = 400;

struct alignas(64) Accumulator {
    int16_t v[2][HIDDEN];
};

bool nnue_load_embedded();
bool nnue_load_file(const char *path);
extern bool g_nnueLoaded;
int nnue_evaluate(Position &pos);

#ifndef NNUE_KB
#define NNUE_KB 1
#endif
const int KBUCKETS = NNUE_KB;
const int NNUE_INPUTS = 768 * KBUCKETS;

// king bucket from the king square relative to the perspective (rank-flipped for black)
inline int king_bucket(int relKsq) {
#if NNUE_KB == 1
    (void)relKsq;
    return 0;
#else
    int f = file_of(relKsq), r = rank_of(relKsq);
    if (f >= 4) f = 7 - f;
    if (r == 0) return f < 2 ? 0 : 1;
    if (r == 1) return 2;
    return 3;
#endif
}

inline int nnue_feature(int persp, int pc, int sq, int ksq) {
    int c = piece_color(pc), pt = piece_type(pc);
    int kb = king_bucket(persp == WHITE ? ksq : ksq ^ 56);
    if (file_of(ksq) >= 4) sq ^= 7; // horizontal mirror so own king is always on files a-d
    if (persp == WHITE) return kb * 768 + (c == WHITE ? 0 : 384) + pt * 64 + sq;
    return kb * 768 + (c == BLACK ? 0 : 384) + pt * 64 + (sq ^ 56);
}
inline int nnue_bucket(int pieceCount) { return std::min((pieceCount - 2) / 4, OUT_BUCKETS - 1); }
