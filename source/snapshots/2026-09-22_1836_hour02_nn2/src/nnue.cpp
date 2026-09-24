#include "nnue.h"
#include <cstdio>
#include <vector>

bool g_nnueLoaded = false;

alignas(64) static int16_t L1W[768 * HIDDEN];
alignas(64) static int16_t L1B[HIDDEN];
alignas(64) static int16_t L2W[OUT_BUCKETS][2 * HIDDEN];
static int32_t L2B[OUT_BUCKETS];
static Accumulator accStack[MAX_STATES];

static const size_t NET_BYTES = sizeof(L1W) + sizeof(L1B) + sizeof(L2W) + sizeof(L2B);

#ifdef EMBED_NET
#define STR2(x) #x
#define STR(x) STR2(x)
asm(".section .rdata,\"dr\"\n"
    ".balign 64\n"
    ".global gEmbeddedNet\n"
    "gEmbeddedNet:\n"
    ".incbin \"" STR(EMBED_NET) "\"\n"
    ".global gEmbeddedNetEnd\n"
    "gEmbeddedNetEnd:\n"
    ".byte 0\n"
    ".text\n");
extern "C" const unsigned char gEmbeddedNet[];
extern "C" const unsigned char gEmbeddedNetEnd[];
#endif

static bool load_from_memory(const unsigned char *p, size_t n) {
    if (n < NET_BYTES) return false;
    memcpy(L1W, p, sizeof(L1W));
    p += sizeof(L1W);
    memcpy(L1B, p, sizeof(L1B));
    p += sizeof(L1B);
    memcpy(L2W, p, sizeof(L2W));
    p += sizeof(L2W);
    memcpy(L2B, p, sizeof(L2B));
    g_nnueLoaded = true;
    return true;
}

bool nnue_load_embedded() {
#ifdef EMBED_NET
    return load_from_memory(gEmbeddedNet, (size_t)(gEmbeddedNetEnd - gEmbeddedNet));
#else
    return false;
#endif
}

bool nnue_load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> buf(NET_BYTES);
    size_t n = fread(buf.data(), 1, NET_BYTES, f);
    fclose(f);
    return load_from_memory(buf.data(), n);
}

static inline void add_row(int16_t *acc, int feat) {
    const int16_t *w = &L1W[feat * HIDDEN];
    for (int i = 0; i < HIDDEN; i++) acc[i] += w[i];
}
static inline void sub_row(int16_t *acc, int feat) {
    const int16_t *w = &L1W[feat * HIDDEN];
    for (int i = 0; i < HIDDEN; i++) acc[i] -= w[i];
}

static void refresh(const Position &pos, Accumulator &a) {
    for (int p = 0; p < 2; p++) {
        memcpy(a.v[p], L1B, sizeof(L1B));
        int ksq = pos.king_sq(p);
        U64 b = pos.occupied();
        while (b) {
            int s = pop_lsb(b);
            add_row(a.v[p], nnue_feature(p, pos.piece_on(s), s, ksq));
        }
    }
}

// true if a king moved between the a-d and e-h halves (mirroring changes -> needs refresh)
static inline bool king_crossed(const DirtyPiece &d) {
    for (int i = 0; i < d.n; i++)
        if (piece_type(d.piece[i]) == KING && d.from[i] != NO_SQ && d.to[i] != NO_SQ &&
            ((file_of(d.from[i]) >= 4) != (file_of(d.to[i]) >= 4)))
            return true;
    return false;
}

static void apply_dirty(const Accumulator &src, Accumulator &dst, const DirtyPiece &d, const int ksq[2]) {
    for (int p = 0; p < 2; p++) {
        int16_t *acc = dst.v[p];
        const int16_t *in = src.v[p];
        int adds[3], subs[3], na = 0, ns = 0;
        for (int i = 0; i < d.n; i++) {
            if (d.from[i] != NO_SQ) subs[ns++] = nnue_feature(p, d.piece[i], d.from[i], ksq[p]);
            if (d.to[i] != NO_SQ) adds[na++] = nnue_feature(p, d.piece[i], d.to[i], ksq[p]);
        }
        if (na == 1 && ns == 1) {
            const int16_t *wa = &L1W[adds[0] * HIDDEN], *ws = &L1W[subs[0] * HIDDEN];
            for (int i = 0; i < HIDDEN; i++) acc[i] = in[i] + wa[i] - ws[i];
        } else if (na == 1 && ns == 2) {
            const int16_t *wa = &L1W[adds[0] * HIDDEN], *ws = &L1W[subs[0] * HIDDEN], *ws2 = &L1W[subs[1] * HIDDEN];
            for (int i = 0; i < HIDDEN; i++) acc[i] = in[i] + wa[i] - ws[i] - ws2[i];
        } else {
            memcpy(acc, in, sizeof(int16_t) * HIDDEN);
            for (int i = 0; i < na; i++) add_row(acc, adds[i]);
            for (int i = 0; i < ns; i++) sub_row(acc, subs[i]);
        }
    }
}

static inline int32_t hsum(__m256i v) {
    __m128i lo = _mm256_castsi256_si128(v), hi = _mm256_extracti128_si256(v, 1);
    __m128i s = _mm_add_epi32(lo, hi);
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, 0x4E));
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, 0xB1));
    return _mm_cvtsi128_si32(s);
}

int nnue_evaluate(Position &pos) {
    int sp = pos.sp;
    if (!pos.states[sp].accComputed) {
        int k = sp;
        bool crossed = false;
        while (k > 0 && !pos.states[k].accComputed && sp - k < 64) {
            if (king_crossed(pos.states[k].dirty)) crossed = true;
            k--;
        }
        if (crossed || !pos.states[k].accComputed) {
            refresh(pos, accStack[sp]);
            pos.states[sp].accComputed = true;
        } else {
            // no king crossed halves along the path, so mirroring = current kings' halves
            int ksq[2] = {pos.king_sq(WHITE), pos.king_sq(BLACK)};
            for (int j = k + 1; j <= sp; j++) {
                apply_dirty(accStack[j - 1], accStack[j], pos.states[j].dirty, ksq);
                pos.states[j].accComputed = true;
            }
        }
    }
    const Accumulator &a = accStack[sp];
#ifdef NNUE_DEBUG
    {
        static Accumulator chk;
        refresh(pos, chk);
        if (memcmp(&chk, &a, sizeof(chk)) != 0) {
            printf("NNUE MISMATCH at %s\n", pos.fen().c_str());
            exit(1);
        }
    }
#endif
    int us = pos.stm;
    int bucket = nnue_bucket(popcount(pos.occupied()));
    const __m256i zero = _mm256_setzero_si256();
    const __m256i qa = _mm256_set1_epi16(NNUE_QA);
    __m256i sum = _mm256_setzero_si256();
    const int16_t *accs[2] = {a.v[us], a.v[!us]};
    for (int p = 0; p < 2; p++) {
        const int16_t *acc = accs[p];
        const int16_t *w = &L2W[bucket][p * HIDDEN];
        for (int i = 0; i < HIDDEN; i += 16) {
            __m256i v = _mm256_load_si256((const __m256i *)(acc + i));
            v = _mm256_min_epi16(_mm256_max_epi16(v, zero), qa);
            __m256i ww = _mm256_load_si256((const __m256i *)(w + i));
            __m256i t = _mm256_mullo_epi16(v, ww);
            sum = _mm256_add_epi32(sum, _mm256_madd_epi16(t, v));
        }
    }
    int64_t out = hsum(sum);
    out = out / NNUE_QA + L2B[bucket];
    return (int)(out * NNUE_SCALE / (NNUE_QA * NNUE_QB));
}
