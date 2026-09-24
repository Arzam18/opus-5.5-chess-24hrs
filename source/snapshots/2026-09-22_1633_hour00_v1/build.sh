#!/bin/bash
# Usage: ./build.sh [output.exe]   (run from source/)
OUT=${1:-build/engine.exe}
SRC="src/main.cpp src/position.cpp src/bitboard.cpp src/search.cpp src/eval.cpp src/perft.cpp src/bench.cpp src/datagen.cpp $EXTRA_SRC"
g++ -std=c++17 -O3 -march=x86-64-v3 -mtune=znver4 -flto -flto-partition=one -DNDEBUG -static -fno-exceptions -Wall -Wno-unused-function $EXTRA_FLAGS -o "$OUT" $SRC -lpthread
