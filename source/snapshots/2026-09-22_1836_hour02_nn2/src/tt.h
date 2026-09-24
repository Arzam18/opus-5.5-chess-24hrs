#pragma once
#include "types.h"
#include <cstdlib>
#include <vector>
#include <algorithm>

enum Bound : uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

struct TTEntry {
    uint16_t key16;
    uint16_t move;
    int16_t score;
    int16_t eval;
    uint8_t depth;
    uint8_t genBound; // gen << 2 | bound
    int bound() const { return genBound & 3; }
};

struct TTCluster {
    TTEntry e[3];
    char pad[2];
};
static_assert(sizeof(TTCluster) == 32, "cluster size");

class TranspositionTable {
public:
    TTCluster *table = nullptr;
    size_t nClusters = 0;
    uint8_t gen = 0;

    void resize(size_t mb) {
        free(table);
        nClusters = (mb * 1024 * 1024) / sizeof(TTCluster);
        if (nClusters < 1) nClusters = 1;
        table = (TTCluster *)aligned_alloc_(nClusters * sizeof(TTCluster));
        clear();
    }
    void clear() { memset(table, 0, nClusters * sizeof(TTCluster)); gen = 0; }
    void new_search() { gen = (gen + 4) & 0xFC; }

    TTCluster *cluster(U64 key) const {
        return &table[(size_t)(((unsigned __int128)key * nClusters) >> 64)];
    }
    void prefetch(U64 key) const { __builtin_prefetch(cluster(key)); }

    TTEntry *probe(U64 key, bool &found) {
        TTCluster *c = cluster(key);
        uint16_t k = (uint16_t)key;
        for (int i = 0; i < 3; i++) {
            if (c->e[i].key16 == k && c->e[i].genBound) {
                found = true;
                c->e[i].genBound = gen | c->e[i].bound();
                return &c->e[i];
            }
        }
        found = false;
        TTEntry *repl = &c->e[0];
        for (int i = 1; i < 3; i++) {
            if (value(c->e[i]) < value(*repl)) repl = &c->e[i];
        }
        return repl;
    }
    void store(TTEntry *e, U64 key, Move m, int score, int eval, int depth, int bound, bool pv) {
        uint16_t k = (uint16_t)key;
        if (m || e->key16 != k) e->move = m;
        if (bound == BOUND_EXACT || e->key16 != k || depth + 4 + 2 * pv > e->depth || (e->genBound & 0xFC) != gen) {
            e->key16 = k;
            e->score = (int16_t)score;
            e->eval = (int16_t)eval;
            e->depth = (uint8_t)std::max(0, depth + 1); // depth stored +1 so 0 means empty-ish
            e->genBound = gen | bound;
        }
    }
    int hashfull() const {
        int cnt = 0;
        for (int i = 0; i < 1000 && i < (int)nClusters; i++)
            for (int j = 0; j < 3; j++)
                if (table[i].e[j].genBound && (table[i].e[j].genBound & 0xFC) == gen) cnt++;
        return cnt / 3;
    }

private:
    int value(const TTEntry &e) const {
        int age = ((gen - (e.genBound & 0xFC)) & 0xFF) >> 2;
        return e.depth - 8 * age;
    }
    static void *aligned_alloc_(size_t sz) {
        void *p = _aligned_malloc_wrapper(sz);
        return p;
    }
    static void *_aligned_malloc_wrapper(size_t sz) {
        // 64-byte aligned via std::aligned_alloc equivalent on mingw
        void *p = nullptr;
        p = _mm_malloc(sz, 64);
        return p;
    }
    static void free(void *p) {
        if (p) _mm_free(p);
    }
};

extern TranspositionTable TT;

inline int score_to_tt(int s, int ply) {
    if (s >= MATE_IN_MAX) return s + ply;
    if (s <= -MATE_IN_MAX) return s - ply;
    return s;
}
inline int score_from_tt(int s, int ply, int rule50) {
    if (s == VALUE_NONE) return s;
    if (s >= MATE_IN_MAX) {
        return s - ply;
    }
    if (s <= -MATE_IN_MAX) {
        return s + ply;
    }
    (void)rule50;
    return s;
}
