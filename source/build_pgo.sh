#!/bin/bash
# PGO build. Usage (from source/): NET=nets/net1.nnue ./build_pgo.sh build/out.exe
OUT=${1:-build/engine_pgo.exe}
mkdir -p build "$(dirname "$OUT")"
NET=${NET:-nets/net1.nnue}
SRC="src/main.cpp src/position.cpp src/bitboard.cpp src/search.cpp src/eval.cpp src/perft.cpp src/bench.cpp src/datagen.cpp src/nnue.cpp"
FLAGS="-std=c++17 -O3 -march=x86-64-v3 -mtune=znver4 -flto -flto-partition=one -DNDEBUG -static -fno-exceptions -DEMBED_NET=$NET $EXTRA_FLAGS"
PGODIR=$(mktemp -d)
g++ $FLAGS -fprofile-generate -fprofile-dir=$PGODIR -o build/_pgo_gen.exe $SRC -lpthread || exit 1
./build/_pgo_gen.exe bench 13 > /dev/null || exit 1
g++ $FLAGS -fprofile-use -fprofile-dir=$PGODIR -fprofile-correction -Wno-missing-profile -o "$OUT" $SRC -lpthread || exit 1
rm -rf $PGODIR build/_pgo_gen.exe
echo "built $OUT"
