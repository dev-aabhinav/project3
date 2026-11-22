#!/usr/bin/env bash
# SIMULATED cache misses per key scanned (valgrind cachegrind).
# Each cell = (counters with R=5 minus counters with R=0) / (5 * 1,000,000 keys),
# so building the structure and warming the pool cancel out.
#
# Simulated caches (set explicitly; the VM reports an unrealistic 260 MB L3):
#   L1d 32 KB 8-way, L1i 32 KB 8-way, last level (LL) 8 MB 16-way, 64-byte lines.
#
#   ./bench/cachegrind_cache_misses.sh            (from the repo root, after building)

set -euo pipefail

BIN=${BIN:-./build/cache_probe}
OUT=${OUT:-cachegrind_runs}
CG=(valgrind --tool=cachegrind --cache-sim=yes
    --I1=32768,8,64 --D1=32768,8,64 --LL=8388608,16,64
    --cachegrind-out-file=/dev/null)

mkdir -p "$OUT"
"$BIN" prepare > /dev/null

# Launch every (structure, workload, R) run, 4 at a time.
jobs_running=0
for s in map_random map_asc btree; do
  for w in scan range1000; do
    for r in 0 5; do
      ( "${CG[@]}" "$BIN" "$s" "$w" "$r" > "$OUT/$s.$w.$r.stdout" 2> "$OUT/$s.$w.$r.log" ) &
      jobs_running=$((jobs_running + 1))
      if [ "$jobs_running" -ge 4 ]; then wait -n; jobs_running=$((jobs_running - 1)); fi
    done
  done
done
wait

# Pull "I refs", "D1  misses" and "LLd misses" totals out of a cachegrind log.
field() {   # $1 = log file, $2 = label regex
  grep -E "$2" "$1" | head -1 | sed -E 's/.*: *([0-9,]+).*/\1/' | tr -d ','
}

echo "| structure | workload | instructions / key | L1d misses / key | LL data misses / key |"
echo "|---|---|---|---|---|"
for s in map_random map_asc btree; do
  for w in scan range1000; do
    a="$OUT/$s.$w.0.log"; b="$OUT/$s.$w.5.log"
    ir=$(( $(field "$b" "I *refs:") - $(field "$a" "I *refs:") ))
    d1=$(( $(field "$b" "D1 *misses:") - $(field "$a" "D1 *misses:") ))
    ll=$(( $(field "$b" "LLd misses:") - $(field "$a" "LLd misses:") ))
    awk -v s="$s" -v w="$w" -v ir="$ir" -v d1="$d1" -v ll="$ll" 'BEGIN {
      k = 5 * 1000000
      printf "| %s | %s | %.1f | %.3f | %.3f |\n", s, w, ir / k, d1 / k, ll / k }'
  done
done

if grep -l "not warm" "$OUT"/btree.*.log > /dev/null 2>&1; then
  echo "WARNING: B+ tree was not warm in some run (see $OUT/*.log)"
fi
