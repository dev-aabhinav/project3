# Range-scan benchmark: B+ tree vs std::map (1M keys)

```
date (UTC): 2026-11-18 00:24:41
command: BENCH_VERBOSE=1 ./build/bench_range_scan 1000000 5
git: 5a08131 + uncommitted bench changes
nproc: 4
Model name:                              Intel(R) Xeon(R) Processor @ 2.10GHz
Thread(s) per core:                      1
Hypervisor vendor:                       KVM
L1d cache:                               192 KiB (4 instances)
L2 cache:                                8 MiB (4 instances)
L3 cache:                                260 MiB (1 instance)
               total        used        free      shared  buff/cache   available
Mem:            15Gi       487Mi        15Gi        12Mi       324Mi        15Gi
flags: -O3 -DNDEBUG -std=c++17 (CMake Release)
```

## Output

```
  [runs] range 10      map random-ins        16.754 ms    14.596 ms    13.895 ms    13.786 ms    14.149 ms
  [runs] range 10      map asc-ins           16.512 ms    13.024 ms    12.632 ms    11.811 ms    11.317 ms
  [runs] range 10      B+ warm                5.294 ms     4.618 ms     4.541 ms     4.435 ms     4.405 ms
  [runs] range 10      B+ cold pool          12.927 ms    11.170 ms    11.828 ms    11.512 ms    11.375 ms
  [runs] range 10      B+ cold+OS drop      182.397 ms   156.844 ms   148.661 ms   159.988 ms   110.508 ms
  [runs] range 1000    map random-ins       135.265 ms   133.969 ms   131.043 ms   133.724 ms   131.304 ms
  [runs] range 1000    map asc-ins           10.453 ms     7.878 ms     7.241 ms     6.891 ms     6.707 ms
  [runs] range 1000    B+ warm                5.418 ms     3.725 ms     3.341 ms     3.272 ms     3.319 ms
  [runs] range 1000    B+ cold pool           9.830 ms     8.972 ms     9.434 ms     9.127 ms     9.314 ms
  [runs] range 1000    B+ cold+OS drop      130.648 ms   144.985 ms   144.614 ms   161.071 ms   174.037 ms
  [runs] range 100000  map random-ins       137.298 ms   136.339 ms   137.367 ms   134.322 ms   135.779 ms
  [runs] range 100000  map asc-ins            8.486 ms     6.455 ms     5.808 ms     5.780 ms     5.597 ms
  [runs] range 100000  B+ warm                5.331 ms     3.764 ms     3.395 ms     3.516 ms     3.334 ms
  [runs] range 100000  B+ cold pool           8.155 ms    10.066 ms     8.079 ms     8.309 ms     8.411 ms
  [runs] range 100000  B+ cold+OS drop      120.279 ms   128.294 ms   130.738 ms   132.864 ms   121.272 ms
  [runs] range 1000000 map random-ins       142.007 ms   160.319 ms   148.714 ms   144.971 ms   144.420 ms
  [runs] range 1000000 map asc-ins            9.775 ms     9.547 ms     9.440 ms     9.486 ms    10.569 ms
  [runs] range 1000000 B+ warm                6.901 ms     6.818 ms     5.963 ms     5.188 ms     4.884 ms
  [runs] range 1000000 B+ cold pool          10.817 ms    10.134 ms    10.142 ms    10.121 ms    12.460 ms
  [runs] range 1000000 B+ cold+OS drop      172.767 ms   172.596 ms   148.360 ms   147.292 ms   153.715 ms
# bench_range_scan  N=1000000  runs=5 (median reported)
# compiler 13.3.0, NDEBUG (Release)
# tree: height 3, 5700 leaves, 31 internal, fill 0.688

per-query time, median of 5 runs, ±spread = (max-min)/median
| range | queries | map (random ins.) | map (asc. ins.) | B+ warm | B+ cold pool, OS cached | B+ cold pool + OS dropped | warm: map(random)/B+ |
|---|---|---|---|---|---|---|---|
| 10 | 10000 | 1.41 us ±21% | 1.26 us ±41% | 0.45 us ±20% (misses 0) | 1.15 us ±15% (4809 reads) | 15.68 us ±46% (resident 0.0%) | 3.12x | 
| 1000 | 1000 | 133.72 us ±3% | 7.24 us ±52% | 3.34 us ±64% (misses 0) | 9.31 us ±9% (4045 reads) | 144.98 us ±30% (resident 0.0%) | 40.02x | 
| 100000 | 10 | 13633.93 us ±2% | 580.77 us ±50% | 351.57 us ±57% (misses 0) | 830.91 us ±24% (3375 reads) | 12829.42 us ±10% (resident 0.0%) | 38.78x | 
| 1000000 | 1 | 144970.90 us ±13% | 9547.32 us ±12% | 5962.78 us ±34% (misses 0) | 10142.35 us ±23% (5702 reads) | 153714.86 us ±17% (resident 0.0%) | 24.31x | 
```
