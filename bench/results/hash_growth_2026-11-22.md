# Extendible hash growth: uniform vs Zipfian insert streams

```
date (UTC): 2026-11-22 13:06:28
nproc: 4
Model name:                              Intel(R) Xeon(R) Processor @ 2.10GHz
Hypervisor vendor:                       KVM
L2 cache:                                8 MiB (4 instances)
               total        used        free      shared  buff/cache   available
Mem:            15Gi       552Mi        14Gi        12Mi       561Mi        15Gi
flags: -O3 -DNDEBUG (Release)
```

## Run 1: ./build/hash_growth 2000000 10000000  (3 streams, 2M attempts each, universe 10M keys)

```
# hash_growth: 2000000 insert attempts per stream, key universe 10000000, bucket size 255, mixing hash

## stream: uniform
| attempts | distinct keys | dup rate | global depth | directory entries | buckets | avg fill | splits | doublings |
|---|---|---|---|---|---|---|---|---|
| 200000 | 197987 | 1.0% | 10 | 1024 | 1024 | 0.758 | 1023 | 10 |
| 400000 | 391887 | 2.0% | 11 | 2048 | 2048 | 0.750 | 2047 | 11 |
| 600000 | 582295 | 3.0% | 12 | 4096 | 4021 | 0.568 | 4020 | 12 |
| 800000 | 768895 | 3.9% | 12 | 4096 | 4096 | 0.736 | 4095 | 12 |
| 1000000 | 951419 | 4.9% | 13 | 8192 | 4331 | 0.861 | 4330 | 13 |
| 1200000 | 1130609 | 5.8% | 13 | 8192 | 7766 | 0.571 | 7765 | 13 |
| 1400000 | 1306325 | 6.7% | 13 | 8192 | 8191 | 0.625 | 8190 | 13 |
| 1600000 | 1478620 | 7.6% | 13 | 8192 | 8192 | 0.708 | 8191 | 13 |
| 1800000 | 1647265 | 8.5% | 14 | 16384 | 8193 | 0.788 | 8192 | 14 |
| 2000000 | 1812537 | 9.4% | 14 | 16384 | 8290 | 0.857 | 8289 | 14 |
buckets per local depth: d=13:8094 d=14:196
pool misses after creation: 0 (0 = everything stayed in memory)
insert latency without doubling: p50 0.29 us, p99 0.63 us, p99.9 3.96 us, max 3545.1 us (1999986 inserts)
directory doublings (the insert that triggered each):
  -> depth  1 (      2 entries) at      256 distinct keys:      3.2 us
  -> depth  2 (      4 entries) at      511 distinct keys:      3.0 us
  -> depth  3 (      8 entries) at      982 distinct keys:      3.0 us
  -> depth  4 (     16 entries) at     1922 distinct keys:      2.8 us
  -> depth  5 (     32 entries) at     3826 distinct keys:      3.0 us
  -> depth  6 (     64 entries) at     7233 distinct keys:      3.1 us
  -> depth  7 (    128 entries) at    14179 distinct keys:      3.3 us
  -> depth  8 (    256 entries) at    26849 distinct keys:      3.1 us
  -> depth  9 (    512 entries) at    55621 distinct keys:      3.6 us
  -> depth 10 (   1024 entries) at   107739 distinct keys:      5.1 us
  -> depth 11 (   2048 entries) at   215381 distinct keys:      5.9 us
  -> depth 12 (   4096 entries) at   429169 distinct keys:      7.2 us
  -> depth 13 (   8192 entries) at   839544 distinct keys:     10.2 us
  -> depth 14 (  16384 entries) at  1632169 distinct keys:     13.9 us

## stream: zipf theta=0.99
generator check: rank-0 share measured 0.0559 vs theory 0.0554; distinct ranks in 1M samples: 343002
| attempts | distinct keys | dup rate | global depth | directory entries | buckets | avg fill | splits | doublings |
|---|---|---|---|---|---|---|---|---|
| 200000 | 88710 | 55.6% | 9 | 512 | 512 | 0.679 | 511 | 9 |
| 400000 | 160119 | 60.0% | 10 | 1024 | 1024 | 0.613 | 1023 | 10 |
| 600000 | 225225 | 62.5% | 11 | 2048 | 1039 | 0.850 | 1038 | 11 |
| 800000 | 286278 | 64.2% | 11 | 2048 | 1975 | 0.568 | 1974 | 11 |
| 1000000 | 344177 | 65.6% | 11 | 2048 | 2048 | 0.659 | 2047 | 11 |
| 1200000 | 399603 | 66.7% | 11 | 2048 | 2048 | 0.765 | 2047 | 11 |
| 1400000 | 452990 | 67.6% | 12 | 4096 | 2075 | 0.856 | 2074 | 12 |
| 1600000 | 504664 | 68.5% | 12 | 4096 | 2603 | 0.760 | 2602 | 12 |
| 1800000 | 554572 | 69.2% | 12 | 4096 | 3729 | 0.583 | 3728 | 12 |
| 2000000 | 603344 | 69.8% | 12 | 4096 | 4082 | 0.580 | 4081 | 12 |
buckets per local depth: d=11:14 d=12:4068
pool misses after creation: 0 (0 = everything stayed in memory)
insert latency without doubling: p50 0.17 us, p99 0.44 us, p99.9 3.69 us, max 1093.4 us (1999988 inserts)
directory doublings (the insert that triggered each):
  -> depth  1 (      2 entries) at      256 distinct keys:      4.2 us
  -> depth  2 (      4 entries) at      510 distinct keys:      3.1 us
  -> depth  3 (      8 entries) at      981 distinct keys:      3.1 us
  -> depth  4 (     16 entries) at     1836 distinct keys:      3.1 us
  -> depth  5 (     32 entries) at     3751 distinct keys:      3.0 us
  -> depth  6 (     64 entries) at     6860 distinct keys:      3.0 us
  -> depth  7 (    128 entries) at    14097 distinct keys:      3.0 us
  -> depth  8 (    256 entries) at    27860 distinct keys:      5.1 us
  -> depth  9 (    512 entries) at    52223 distinct keys:      3.9 us
  -> depth 10 (   1024 entries) at   109268 distinct keys:      5.0 us
  -> depth 11 (   2048 entries) at   212719 distinct keys:      8.7 us
  -> depth 12 (   4096 entries) at   424992 distinct keys:      7.8 us

## stream: zipf theta=1.2
generator check: rank-0 share measured 0.1861 vs theory 0.1854; distinct ranks in 1M samples: 95957
| attempts | distinct keys | dup rate | global depth | directory entries | buckets | avg fill | splits | doublings |
|---|---|---|---|---|---|---|---|---|
| 200000 | 27391 | 86.3% | 8 | 256 | 129 | 0.833 | 128 | 8 |
| 400000 | 47070 | 88.2% | 8 | 256 | 256 | 0.721 | 255 | 8 |
| 600000 | 64712 | 89.2% | 9 | 512 | 367 | 0.691 | 366 | 9 |
| 800000 | 80925 | 89.9% | 9 | 512 | 512 | 0.620 | 511 | 9 |
| 1000000 | 96115 | 90.4% | 9 | 512 | 512 | 0.736 | 511 | 9 |
| 1200000 | 110616 | 90.8% | 10 | 1024 | 513 | 0.846 | 512 | 10 |
| 1400000 | 124607 | 91.1% | 10 | 1024 | 631 | 0.774 | 630 | 10 |
| 1600000 | 137949 | 91.4% | 10 | 1024 | 927 | 0.584 | 926 | 10 |
| 1800000 | 150903 | 91.6% | 10 | 1024 | 1022 | 0.579 | 1021 | 10 |
| 2000000 | 163565 | 91.8% | 10 | 1024 | 1024 | 0.626 | 1023 | 10 |
buckets per local depth: d=10:1024
pool misses after creation: 0 (0 = everything stayed in memory)
insert latency without doubling: p50 0.13 us, p99 0.42 us, p99.9 2.91 us, max 1548.6 us (1999990 inserts)
directory doublings (the insert that triggered each):
  -> depth  1 (      2 entries) at      256 distinct keys:      3.7 us
  -> depth  2 (      4 entries) at      497 distinct keys:      3.5 us
  -> depth  3 (      8 entries) at      959 distinct keys:      2.8 us
  -> depth  4 (     16 entries) at     1755 distinct keys:      2.7 us
  -> depth  5 (     32 entries) at     3477 distinct keys:      3.1 us
  -> depth  6 (     64 entries) at     6834 distinct keys:      2.9 us
  -> depth  7 (    128 entries) at    14385 distinct keys:     35.9 us
  -> depth  8 (    256 entries) at    26941 distinct keys:     30.4 us
  -> depth  9 (    512 entries) at    55159 distinct keys:      7.0 us
  -> depth 10 (   1024 entries) at   109336 distinct keys:      6.5 us
```

## Run 2: ./build/hash_growth 20000000 4000000000000000000 262144 1  (uniform only, 20M distinct keys, 1 GB pool)

```
# hash_growth: 20000000 insert attempts per stream, key universe 4000000000000000000, bucket size 255, mixing hash

## stream: uniform
| attempts | distinct keys | dup rate | global depth | directory entries | buckets | avg fill | splits | doublings |
|---|---|---|---|---|---|---|---|---|
| 2000000 | 2000000 | 0.0% | 14 | 16384 | 10099 | 0.777 | 10098 | 14 |
| 4000000 | 4000000 | 0.0% | 15 | 32768 | 20220 | 0.776 | 20219 | 15 |
| 6000000 | 6000000 | 0.0% | 15 | 32768 | 32768 | 0.718 | 32767 | 15 |
| 8000000 | 8000000 | 0.0% | 16 | 65536 | 40448 | 0.776 | 40447 | 16 |
| 10000000 | 10000000 | 0.0% | 16 | 65536 | 65490 | 0.599 | 65489 | 16 |
| 12000000 | 12000000 | 0.0% | 16 | 65536 | 65536 | 0.718 | 65535 | 16 |
| 14000000 | 14000000 | 0.0% | 17 | 131072 | 65717 | 0.835 | 65716 | 17 |
| 16000000 | 16000000 | 0.0% | 17 | 131072 | 80752 | 0.777 | 80751 | 17 |
| 18000000 | 18000000 | 0.0% | 17 | 131072 | 122939 | 0.574 | 122938 | 17 |
| 20000000 | 20000000 | 0.0% | 17 | 131072 | 130962 | 0.599 | 130961 | 17 |
buckets per local depth: d=16:110 d=17:130852
pool misses after creation: 0 (0 = everything stayed in memory)
insert latency without doubling: p50 0.91 us, p99 2.07 us, p99.9 19.78 us, max 5017.7 us (19999983 inserts)
directory doublings (the insert that triggered each):
  -> depth  1 (      2 entries) at      256 distinct keys:      4.3 us
  -> depth  2 (      4 entries) at      507 distinct keys:      3.7 us
  -> depth  3 (      8 entries) at      983 distinct keys:      3.7 us
  -> depth  4 (     16 entries) at     1918 distinct keys:      3.6 us
  -> depth  5 (     32 entries) at     3643 distinct keys:      3.9 us
  -> depth  6 (     64 entries) at     7061 distinct keys:      4.0 us
  -> depth  7 (    128 entries) at    14467 distinct keys:      4.4 us
  -> depth  8 (    256 entries) at    28732 distinct keys:      5.2 us
  -> depth  9 (    512 entries) at    55208 distinct keys:      5.3 us
  -> depth 10 (   1024 entries) at   109806 distinct keys:      5.5 us
  -> depth 11 (   2048 entries) at   214478 distinct keys:      9.7 us
  -> depth 12 (   4096 entries) at   429201 distinct keys:      9.0 us
  -> depth 13 (   8192 entries) at   835931 distinct keys:     10.3 us
  -> depth 14 (  16384 entries) at  1587437 distinct keys:     17.0 us
  -> depth 15 (  32768 entries) at  3334520 distinct keys:     26.4 us
  -> depth 16 (  65536 entries) at  6477553 distinct keys:     37.7 us
  -> depth 17 ( 131072 entries) at 12915485 distinct keys:     78.2 us

real	0m25.952s
user	0m24.186s
sys	0m1.577s
```
