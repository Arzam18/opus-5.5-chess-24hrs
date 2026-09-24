#!/bin/bash
# Usage (from source/): ./snapshot.sh <label> <exe> "<note>"
LABEL=$1
EXE=$2
NOTE=$3
TS=$(date +%Y-%m-%d_%H%M)
D=snapshots/${TS}_${LABEL}
mkdir -p "$D"
cp -r src trainer build.sh snapshot.sh "$D"/
[ -n "$EXE" ] && cp "$EXE" "$D"/
[ -d nets ] && ls nets/*.nnue >/dev/null 2>&1 && mkdir -p "$D/nets" && for n in $EMBED_NETS; do cp "$n" "$D/nets/"; done
echo "$NOTE" > "$D/NOTE.txt"
echo "snapshot: $D"
