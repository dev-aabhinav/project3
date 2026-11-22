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

## Software counter only (VM has no PMU): ./bench/perf_cache_misses.sh (REPEAT=3, R_HI=20), two runs

task-clock per key; hardware columns are n/a in this VM. (A first attempt with R=5 and REPEAT=1
gave map_asc scan = 0.74 ns/key: setup noise between the two separate processes swamped 5 fast
scans. Fixed by R_HI=20 + REPEAT=3; values below agree with bench_range_scan.)

```
--- run 1 (REPEAT=3, R_HI=20)
|  map_random  |  scan  |  144.98 ns  |
|  map_random  |  range1000  |  142.93 ns  |
|  map_asc  |  scan  |  9.04 ns  |
|  map_asc  |  range1000  |  6.15 ns  |
|  btree  |  scan  |  4.26 ns  |
|  btree  |  range1000  |  4.21 ns  |
--- run 2 (REPEAT=3, R_HI=20)
|  map_random  |  scan  |  153.00 ns  |
|  map_random  |  range1000  |  141.00 ns  |
|  map_asc  |  scan  |  8.44 ns  |
|  map_asc  |  range1000  |  6.25 ns  |
|  btree  |  scan  |  4.14 ns  |
|  btree  |  range1000  |  4.16 ns  |
```

Real hardware counters (cache-misses, LLC-load-misses, dTLB-load-misses): NOT MEASURED yet
(needs a native Linux machine; see bench/README.md).
