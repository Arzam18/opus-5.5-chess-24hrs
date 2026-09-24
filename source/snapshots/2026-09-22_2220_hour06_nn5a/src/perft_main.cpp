#include "position.h"
#include <cstdio>
#include <cstdlib>
void perft_suite(const std::string &file, int maxDepth);
void perft_divide(Position &pos, int depth);
int main(int argc, char **argv) {
    init_bitboards(); Zobrist::init();
    perft_suite(argv[1], atoi(argv[2]));
}
