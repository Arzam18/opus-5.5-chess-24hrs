#pragma once
#include "position.h"
#include <atomic>
#include <cstdint>
#include <chrono>

struct SearchLimits {
    int64_t time[2] = {-1, -1};
    int64_t inc[2] = {0, 0};
    int movestogo = 0;
    int depth = 0;
    int64_t movetime = 0;
    int64_t nodes = 0;
    bool infinite = false;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

struct SearchResult {
    Move best = MOVE_NONE;
    int score = 0;
    int depth = 0;
    uint64_t nodes = 0;
};

extern std::atomic<bool> g_stop;
extern std::atomic<bool> g_searching;
extern int g_moveOverhead;
extern bool g_silent; // suppress info output (datagen)

void search_init();
void search_clear(); // ucinewgame
SearchResult think(Position &pos, const SearchLimits &limits, bool printBest = true);
int evaluate(Position &pos);
