#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <cassert>
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

typedef uint64_t U64;

// Portable PEXT/PDEP fallbacks. x86 builds can use hardware BMI2; ARM64 uses
// these scalar implementations unless a dedicated implementation is added.
inline U64 portable_pext(U64 value, U64 mask) {
    U64 result = 0, bit = 1;
    while (mask) {
        U64 low = mask & (~mask + 1);
        if (value & low) result |= bit;
        mask &= mask - 1;
        bit <<= 1;
    }
    return result;
}
inline U64 portable_pdep(U64 value, U64 mask) {
    U64 result = 0, bit = 1;
    while (mask) {
        U64 low = mask & (~mask + 1);
        if (value & bit) result |= low;
        mask &= mask - 1;
        bit <<= 1;
    }
    return result;
}
#if defined(__BMI2__) && (defined(__x86_64__) || defined(__i386__))
#define ENGINE_PEXT(v,m) _pext_u64((v),(m))
#define ENGINE_PDEP(v,m) _pdep_u64((v),(m))
#else
#define ENGINE_PEXT(v,m) portable_pext((v),(m))
#define ENGINE_PDEP(v,m) portable_pdep((v),(m))
#endif

enum Color : int { WHITE = 0, BLACK = 1 };
enum PieceType : int { PAWN = 0, KNIGHT, BISHOP, ROOK, QUEEN, KING, NO_PT };
// piece = pt + 6*color ; 12 = none
enum Piece : int { WP = 0, WN, WB, WR, WQ, WK, BP, BN, BB, BR, BQ, BK, NO_PIECE };

inline Piece make_piece(int c, int pt) { return Piece(pt + 6 * c); }
inline int piece_type(int p) { return p % 6; }
inline int piece_color(int p) { return p / 6; }

enum Square : int {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    NO_SQ = 64
};

inline int file_of(int s) { return s & 7; }
inline int rank_of(int s) { return s >> 3; }
inline int make_sq(int f, int r) { return r * 8 + f; }
inline int relative_rank(int c, int s) { return c == WHITE ? rank_of(s) : 7 - rank_of(s); }
inline int relative_sq(int c, int s) { return c == WHITE ? s : s ^ 56; }

enum CastleRights : int { WK_CASTLE = 1, WQ_CASTLE = 2, BK_CASTLE = 4, BQ_CASTLE = 8 };

// Move: bits 0-5 from, 6-11 to, 12-13 promo piece (0=N..3=Q), 14-15 type
typedef uint16_t Move;
enum MoveType : int { MT_NORMAL = 0, MT_PROMO = 1 << 14, MT_EP = 2 << 14, MT_CASTLE = 3 << 14 };
const Move MOVE_NONE = 0;
const Move MOVE_NULL = 65; // from b1 to b1: never legal

inline int from_sq(Move m) { return m & 63; }
inline int to_sq(Move m) { return (m >> 6) & 63; }
inline int move_type(Move m) { return m & (3 << 14); }
inline int promo_type(Move m) { return ((m >> 12) & 3) + KNIGHT; }
inline Move encode_move(int from, int to, int type = MT_NORMAL) { return Move(from | (to << 6) | type); }
inline Move make_promo(int from, int to, int pt) { return Move(from | (to << 6) | MT_PROMO | ((pt - KNIGHT) << 12)); }

inline int popcount(U64 b) { return __builtin_popcountll(b); }
inline int lsb(U64 b) { return __builtin_ctzll(b); }
inline int msb(U64 b) { return 63 - __builtin_clzll(b); }
inline int pop_lsb(U64 &b) { int s = lsb(b); b &= b - 1; return s; }
inline U64 sqbb(int s) { return 1ULL << s; }

typedef int Value;
const Value VALUE_INF = 32000;
const Value VALUE_MATE = 31000;
const Value VALUE_NONE = 32001;
const int MAX_PLY = 128;
const Value MATE_IN_MAX = VALUE_MATE - MAX_PLY;
