#!/bin/bash
# Chess 47 vs Stockfish Elo bracket. Logs go to build/elo/.
# nodes 25000  ~ DM42n level 6-7 on battery (~10k nodes/s)
# nodes 100000 ~ DM42n level 7-8 on USB power (~19k nodes/s)
# nodes 400000 ~ DM42n level 9 (30 s) on USB power
cd "$(dirname "$0")/.." || exit 1
OUT=build/elo
mkdir -p "$OUT"
G=${GAMES:-20}
s1() {
  for a in "25000 1600" "25000 2000" "100000 2000" "100000 2400"; do
    set -- $a
    python3 test/match.py --nodes "$1" --elo "$2" --games "$G" 2>"$OUT/std_$1_$2.log"
  done
}
s2() {
  for a in "100000 1800" "100000 2200"; do
    set -- $a
    python3 test/match.py --960 --nodes "$1" --elo "$2" --games "$G" 2>"$OUT/c960_$1_$2.log"
  done
  python3 test/match.py --nodes 400000 --elo 2400 --games "$G" 2>"$OUT/std_400000_2400.log"
}
s1 > "$OUT/stream1.txt" 2>&1 &
s2 > "$OUT/stream2.txt" 2>&1 &
wait
cat "$OUT/stream1.txt" "$OUT/stream2.txt"
