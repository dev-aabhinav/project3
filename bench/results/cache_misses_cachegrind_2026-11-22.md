# Cache misses per key: B+ tree vs std::map (SIMULATED, cachegrind)

```
date (UTC): 2026-11-22 12:49:31
command: ./bench/cachegrind_cache_misses.sh
valgrind 3.22.0 cachegrind --cache-sim=yes --I1=32768,8,64 --D1=32768,8,64 --LL=8388608,16,64
per key = (counters with R=5 - counters with R=0) / (5 * 1,000,000 keys)
nproc: 4
Model name:                              Intel(R) Xeon(R) Processor @ 2.10GHz
Hypervisor vendor:                       KVM
flags: -O3 -DNDEBUG (Release)
```

## Simulated cache misses

| structure | workload | instructions / key | L1d misses / key | LL data misses / key |
|---|---|---|---|---|
| map_random | scan | 21.8 | 1.003 | 0.986 |
| map_random | range1000 | 24.9 | 1.023 | 0.877 |
| map_asc | scan | 21.7 | 1.002 | 1.000 |
| map_asc | range1000 | 24.9 | 1.022 | 0.902 |
| btree | scan | 31.2 | 0.146 | 0.130 |
| btree | range1000 | 34.3 | 0.177 | 0.081 |

## Node placement (./build/map_node_gaps)

```
random build   median |gap|  18779904 B, 90th pct   43737024 B, gap == +64 B:   0.0%, next node within 4 KB:   0.0%
ascending build median |gap|        64 B, 90th pct         64 B, gap == +64 B: 100.0%, next node within 4 KB: 100.0%
sizeof node payload: 16 B (pair) + 32 B rb-tree header; malloc rounds the node to 64 B
```

## Software counter only (VM has no PMU): ./bench/perf_cache_misses.sh, REPEAT=1

task-clock per key (hardware columns n/a in this VM):

|  map_random  |  scan  |  132.48 ns  |
|  map_random  |  range1000  |  137.25 ns  |
|  map_asc  |  scan  |  0.74 ns  |
|  map_asc  |  range1000  |  6.45 ns  |
|  btree  |  scan  |  3.16 ns  |
|  btree  |  range1000  |  3.14 ns  |

Real hardware counters: NOT MEASURED yet (needs a native Linux machine).
