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

inline int nnue_feature(int persp, int pc, int sq, int ksq) {
    int c = piece_color(pc), pt = piece_type(pc);
    if (file_of(ksq) >= 4) sq ^= 7; // horizontal mirror so own king is always on files a-d
    if (persp == WHITE) return (c == WHITE ? 0 : 384) + pt * 64 + sq;
    return (c == BLACK ? 0 : 384) + pt * 64 + (sq ^ 56);
}
inline int nnue_bucket(int pieceCount) { return std::min((pieceCount - 2) / 4, OUT_BUCKETS - 1); }
