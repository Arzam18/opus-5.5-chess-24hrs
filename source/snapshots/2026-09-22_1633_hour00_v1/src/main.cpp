#include "position.h"
#include "search.h"
#include "eval.h"
#include "tt.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <thread>
#include <mutex>

#define ENGINE_NAME "Opus 5.5 chess 24hrs"
#define ENGINE_AUTHOR "Opus 5.5"

extern std::mutex g_ioMutex;
void perft_suite(const std::string &file, int maxDepth);
void perft_divide(Position &pos, int depth);
int datagen_main(int argc, char **argv);
void bench(int depth);

static Position g_pos;
static std::thread g_thread;
static const char *StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

static void out(const std::string &s) {
    std::lock_guard<std::mutex> lk(g_ioMutex);
    fputs(s.c_str(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static void wait_search() {
    if (g_thread.joinable()) g_thread.join();
    g_searching = false;
}

static void stop_search() {
    g_stop = true;
    wait_search();
}

static void cmd_position(std::istringstream &is) {
    std::string tok, fen;
    is >> tok;
    if (tok == "startpos") {
        fen = StartFEN;
        is >> tok; // maybe "moves"
    } else if (tok == "fen") {
        while (is >> tok && tok != "moves") fen += tok + " ";
    } else return;
    g_pos.set_fen(fen);
    if (tok == "moves") {
        while (is >> tok) {
            Move m = g_pos.parse_uci(tok);
            if (m == MOVE_NONE) break;
            g_pos.make_move(m);
            // keep the state stack bounded for very long games: reset history when a capture/pawn move makes it irrelevant
            if (g_pos.sp > 1000) {
                std::string f = g_pos.fen();
                g_pos.set_fen(f);
            }
        }
    }
}

static void cmd_go(std::istringstream &is) {
    SearchLimits lim;
    std::string tok;
    bool anyLimit = false;
    while (is >> tok) {
        if (tok == "wtime") { is >> lim.time[WHITE]; anyLimit = true; }
        else if (tok == "btime") { is >> lim.time[BLACK]; anyLimit = true; }
        else if (tok == "winc") is >> lim.inc[WHITE];
        else if (tok == "binc") is >> lim.inc[BLACK];
        else if (tok == "movestogo") is >> lim.movestogo;
        else if (tok == "depth") { is >> lim.depth; anyLimit = true; }
        else if (tok == "movetime") { is >> lim.movetime; anyLimit = true; }
        else if (tok == "nodes") { is >> lim.nodes; anyLimit = true; }
        else if (tok == "infinite") lim.infinite = true;
        else if (tok == "ponder") lim.infinite = true;
    }
    if (!anyLimit) lim.infinite = true;
    g_stop = false;
    g_searching = true;
    g_thread = std::thread([lim]() {
        think(g_pos, lim, true);
    });
}

static void cmd_setoption(std::istringstream &is) {
    std::string tok, name, value;
    is >> tok; // name
    while (is >> tok && tok != "value") name += (name.empty() ? "" : " ") + tok;
    while (is >> tok) value += (value.empty() ? "" : " ") + tok;
    for (auto &c : name) c = (char)tolower((unsigned char)c);
    if (name == "hash") {
        int mb = atoi(value.c_str());
        mb = std::max(1, std::min(mb, 65536));
        TT.resize(mb);
    } else if (name == "move overhead") {
        g_moveOverhead = std::max(0, std::min(atoi(value.c_str()), 5000));
    } else if (name == "clear hash") {
        TT.clear();
    }
}

int main(int argc, char **argv) {
    init_bitboards();
    Zobrist::init();
    init_eval();
    TT.resize(16);
    search_init();
    g_pos.set_fen(StartFEN);

    if (argc > 1) {
        std::string a = argv[1];
        if (a == "bench") { bench(argc > 2 ? atoi(argv[2]) : 13); return 0; }
        if (a == "perftsuite") { perft_suite(argv[2], argc > 3 ? atoi(argv[3]) : 5); return 0; }
        if (a == "datagen") return datagen_main(argc, argv);
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream is(line);
        std::string cmd;
        if (!(is >> cmd)) continue;
        if (cmd == "uci") {
            out("id name " ENGINE_NAME);
            out("id author " ENGINE_AUTHOR);
            out("option name Hash type spin default 16 min 1 max 65536");
            out("option name Threads type spin default 1 min 1 max 1");
            out("option name Move Overhead type spin default 30 min 0 max 5000");
            out("option name Clear Hash type button");
            out("uciok");
        } else if (cmd == "isready") {
            out("readyok");
        } else if (cmd == "ucinewgame") {
            stop_search();
            search_clear();
        } else if (cmd == "setoption") {
            stop_search();
            cmd_setoption(is);
        } else if (cmd == "position") {
            stop_search();
            cmd_position(is);
        } else if (cmd == "go") {
            stop_search();
            cmd_go(is);
        } else if (cmd == "stop") {
            stop_search();
        } else if (cmd == "ponderhit") {
            // pondering is not supported; treat as stop so a bestmove is produced
            stop_search();
        } else if (cmd == "quit") {
            stop_search();
            return 0;
        } else if (cmd == "d") {
            out(g_pos.fen());
        } else if (cmd == "perft") {
            int d = 1;
            is >> d;
            stop_search();
            perft_divide(g_pos, d);
        } else if (cmd == "eval") {
            out("eval " + std::to_string(evaluate(g_pos)));
        }
    }
    stop_search();
    return 0;
}
