#!/usr/bin/env bash
set -euo pipefail
# Portable build. Set EXTRA_FLAGS="-DEMBED_NET=nets/net13.nnue" only when that
# compatible network is present. Android builds use .github/workflows/android.yml.
OUT=${1:-build/engine}
mkdir -p "$(dirname "$OUT")"
SRC="src/main.cpp src/position.cpp src/bitboard.cpp src/search.cpp src/eval.cpp src/perft.cpp src/bench.cpp src/datagen.cpp src/nnue.cpp"
CXX="${CXX:-g++}"
ARCH_FLAGS="${ARCH_FLAGS:--O3}"
"$CXX" -std=c++17 $ARCH_FLAGS -DNDEBUG -fno-exceptions -Wall -Wno-unused-function \
  ${EXTRA_FLAGS:-} -o "$OUT" $SRC -pthread
echo "Built: $OUT"
