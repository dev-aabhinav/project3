#!/usr/bin/env bash
# REAL hardware counters per key scanned (perf stat). Needs native Linux with
# hardware performance counters (not most VMs, usually not WSL2, never macOS).
#
# Each cell = (counter with R=R_HI minus counter with R=0) / (R_HI * 1,000,000 keys),
# so building the structure and warming the buffer pool cancel out.
# perf -r averages REPEAT runs of each command.
#
# Why R_HI = 20 and not 5: the two runs are separate processes, and setup (building a
# 1M-key map, ~hundreds of ms) varies run to run by tens of ms. With only 5 fast scans
# (~40 ms for the ascending map) that noise swamped the difference; 20 scans dominate it.
#
#   sudo sysctl kernel.perf_event_paranoid=1     # allow user-space counters (temporary)
#   ./bench/perf_cache_misses.sh | tee perf_results.txt

set -euo pipefail

BIN=${BIN:-./build/cache_probe}
REPEAT=${REPEAT:-3}
R_HI=${R_HI:-20}
EVENTS=${EVENTS:-task-clock,cycles,instructions,cache-references,cache-misses,L1-dcache-load-misses,LLC-load-misses,dTLB-load-misses}

PIN=()
if command -v taskset > /dev/null; then PIN=(taskset -c 1); fi   # one core: less noise

echo "# $(date -u '+%F %T') UTC, $(uname -sr), perf $(perf --version 2>/dev/null | awk '{print $3}')"
lscpu | grep -E "Model name|^L1d|^L2|^L3" | sed 's/^/# /'
echo "# cache_probe, REPEAT=$REPEAT, per key = (R$R_HI - R0) / ($R_HI * 1e6)"

"$BIN" prepare > /dev/null

# Prints "event value" lines; sums hybrid-CPU duplicates (cpu_core/x/ + cpu_atom/x/).
measure() {
  "${PIN[@]}" perf stat -x, -r "$REPEAT" -e "$EVENTS" "$BIN" "$@" 2>&1 > /dev/null |
    awk -F, '$1 ~ /^[0-9.]+$/ {
               ev = $3; sub(/^.*\//, "", ev); sub(/\/.*$/, "", ev)
               sum[ev] += $1 }
             $1 ~ /^</ { ev = $3; sub(/^.*\//, "", ev); sub(/\/.*$/, "", ev)
               if (!(ev in sum)) bad[ev] = 1 }
             END { for (e in sum) print e, sum[e]; for (e in bad) print e, "n/a" }'
}

IFS=, read -r -a EV <<< "$EVENTS"
printf "| structure | workload |"; for e in "${EV[@]}"; do printf " %s / key |" "$e"; done; echo
printf "|---|---|"; for e in "${EV[@]}"; do printf "%s" "---|"; done; echo

for s in map_random map_asc btree; do
  for w in scan range1000; do
    a=$(measure "$s" "$w" 0)
    b=$(measure "$s" "$w" "$R_HI")
    printf "| %s | %s |" "$s" "$w"
    for e in "${EV[@]}"; do
      va=$(awk -v e="$e" '$1 == e { print $2 }' <<< "$a")
      vb=$(awk -v e="$e" '$1 == e { print $2 }' <<< "$b")
      if [[ "$va" =~ ^[0-9.]+$ && "$vb" =~ ^[0-9.]+$ ]]; then
        if [ "$e" = task-clock ]; then   # perf reports msec; show ns per key
          awk -v a="$va" -v b="$vb" -v r="$R_HI" 'BEGIN { printf " %.2f ns |", (b - a) * 1e6 / (r * 1e6) }'
        else
          awk -v a="$va" -v b="$vb" -v r="$R_HI" 'BEGIN { printf " %.3f |", (b - a) / (r * 1e6) }'
        fi
      else
        printf " n/a |"
      fi
    done
    echo
  done
done
