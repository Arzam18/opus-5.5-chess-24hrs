#include "bitboard.h"
#include <cstdlib>
#include <algorithm>

U64 PawnAttacks[2][64];
U64 KnightAttacks[64];
U64 KingAttacks[64];
U64 BetweenBB[64][64];
U64 LineBB[64][64];
U64 FileBB[8];
U64 RankBB[8];
U64 AdjFilesBB[8];
U64 PassedMask[2][64];
U64 ForwardFileBB[2][64];
int SqDist[64][64];
PextEntry RookPext[64];
PextEntry BishopPext[64];

static U64 RookTable[102400];
static U64 BishopTable[5248];

static U64 slide(int s, U64 occ, const int dirs[4][2]) {
    U64 res = 0;
    for (int d = 0; d < 4; d++) {
        int f = file_of(s), r = rank_of(s);
        while (true) {
            f += dirs[d][0];
            r += dirs[d][1];
            if (f < 0 || f > 7 || r < 0 || r > 7) break;
            int t = make_sq(f, r);
            res |= sqbb(t);
            if (occ & sqbb(t)) break;
        }
    }
    return res;
}

static const int RookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
static const int BishopDirs[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};

static void init_pext(PextEntry *tab, U64 *storage, const int dirs[4][2]) {
    U64 *ptr = storage;
    for (int s = 0; s < 64; s++) {
        // relevant occupancy mask: exclude edges unless on them
        U64 edges = ((RANK_1 | RANK_8) & ~RankBB[rank_of(s)]) | ((FILE_A | FILE_H) & ~FileBB[file_of(s)]);
        U64 mask = slide(s, 0, dirs) & ~edges;
        tab[s].mask = mask;
        tab[s].attacks = ptr;
        int n = popcount(mask);
        for (int i = 0; i < (1 << n); i++) {
            U64 occ = _pdep_u64((U64)i, mask);
            ptr[i] = slide(s, occ, dirs);
        }
        ptr += (1 << n);
    }
}

void init_bitboards() {
    for (int i = 0; i < 8; i++) {
        FileBB[i] = FILE_A << i;
        RankBB[i] = RANK_1 << (8 * i);
    }
    for (int i = 0; i < 8; i++)
        AdjFilesBB[i] = (i > 0 ? FileBB[i - 1] : 0) | (i < 7 ? FileBB[i + 1] : 0);
    for (int a = 0; a < 64; a++)
        for (int b = 0; b < 64; b++)
            SqDist[a][b] = std::max(std::abs(file_of(a) - file_of(b)), std::abs(rank_of(a) - rank_of(b)));

    for (int s = 0; s < 64; s++) {
        U64 b = sqbb(s);
        PawnAttacks[WHITE][s] = pawn_attacks_bb(WHITE, b);
        PawnAttacks[BLACK][s] = pawn_attacks_bb(BLACK, b);
        U64 k = shift_east(b) | shift_west(b);
        k |= shift_north(k | b) | shift_south(k | b);
        KingAttacks[s] = k;
        U64 n = 0;
        const int kd[8][2] = {{1, 2}, {2, 1}, {-1, 2}, {-2, 1}, {1, -2}, {2, -1}, {-1, -2}, {-2, -1}};
        for (auto &d : kd) {
            int f = file_of(s) + d[0], r = rank_of(s) + d[1];
            if (f >= 0 && f < 8 && r >= 0 && r < 8) n |= sqbb(make_sq(f, r));
        }
        KnightAttacks[s] = n;
    }
    init_pext(RookPext, RookTable, RookDirs);
    init_pext(BishopPext, BishopTable, BishopDirs);

    for (int a = 0; a < 64; a++) {
        for (int b = 0; b < 64; b++) {
            BetweenBB[a][b] = 0;
            LineBB[a][b] = 0;
            if (a == b) continue;
            if (slide(a, 0, RookDirs) & sqbb(b)) {
                BetweenBB[a][b] = rook_attacks(a, sqbb(b)) & rook_attacks(b, sqbb(a));
                LineBB[a][b] = (rook_attacks(a, 0) & rook_attacks(b, 0)) | sqbb(a) | sqbb(b);
            } else if (slide(a, 0, BishopDirs) & sqbb(b)) {
                BetweenBB[a][b] = bishop_attacks(a, sqbb(b)) & bishop_attacks(b, sqbb(a));
                LineBB[a][b] = (bishop_attacks(a, 0) & bishop_attacks(b, 0)) | sqbb(a) | sqbb(b);
            }
        }
    }
    for (int s = 0; s < 64; s++) {
        int f = file_of(s), r = rank_of(s);
        U64 wf = 0, bf = 0;
        for (int rr = r + 1; rr < 8; rr++) wf |= sqbb(make_sq(f, rr));
        for (int rr = r - 1; rr >= 0; rr--) bf |= sqbb(make_sq(f, rr));
        ForwardFileBB[WHITE][s] = wf;
        ForwardFileBB[BLACK][s] = bf;
        PassedMask[WHITE][s] = wf | shift_east(wf) | shift_west(wf);
        PassedMask[BLACK][s] = bf | shift_east(bf) | shift_west(bf);
    }
}
