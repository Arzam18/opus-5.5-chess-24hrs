#pragma once
#include "types.h"

const U64 FILE_A = 0x0101010101010101ULL;
const U64 FILE_H = FILE_A << 7;
const U64 RANK_1 = 0xFFULL;
const U64 RANK_8 = RANK_1 << 56;

extern U64 PawnAttacks[2][64];
extern U64 KnightAttacks[64];
extern U64 KingAttacks[64];
extern U64 BetweenBB[64][64];   // squares strictly between (if aligned), else 0
extern U64 LineBB[64][64];      // full line through both (if aligned), else 0
extern U64 FileBB[8];
extern U64 RankBB[8];
extern U64 AdjFilesBB[8];
extern U64 PassedMask[2][64];   // squares in front on same+adjacent files
extern U64 ForwardFileBB[2][64];
extern int SqDist[64][64];

struct PextEntry {
    U64 mask;
    U64 *attacks;
};
extern PextEntry RookPext[64];
extern PextEntry BishopPext[64];

inline U64 rook_attacks(int s, U64 occ) { return RookPext[s].attacks[ENGINE_PEXT(occ, RookPext[s].mask)]; }
inline U64 bishop_attacks(int s, U64 occ) { return BishopPext[s].attacks[ENGINE_PEXT(occ, BishopPext[s].mask)]; }
inline U64 queen_attacks(int s, U64 occ) { return rook_attacks(s, occ) | bishop_attacks(s, occ); }

inline U64 attacks_bb(int pt, int s, U64 occ) {
    switch (pt) {
    case KNIGHT: return KnightAttacks[s];
    case BISHOP: return bishop_attacks(s, occ);
    case ROOK: return rook_attacks(s, occ);
    case QUEEN: return queen_attacks(s, occ);
    case KING: return KingAttacks[s];
    }
    return 0;
}

inline U64 shift_north(U64 b) { return b << 8; }
inline U64 shift_south(U64 b) { return b >> 8; }
inline U64 shift_east(U64 b) { return (b & ~FILE_H) << 1; }
inline U64 shift_west(U64 b) { return (b & ~FILE_A) >> 1; }
inline U64 pawn_push(int c, U64 b) { return c == WHITE ? b << 8 : b >> 8; }
inline U64 pawn_attacks_bb(int c, U64 b) {
    return c == WHITE ? (shift_east(b) | shift_west(b)) << 8 : (shift_east(b) | shift_west(b)) >> 8;
}

void init_bitboards();
