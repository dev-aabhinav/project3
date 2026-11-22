# Benchmarks

Build first (Release is the default): `cmake -B build -G Ninja && cmake --build build`.
Run everything from the repository root. Saved results with machine specs are in `results/`.

| Program | What it measures |
|---|---|
| `build/bench_range_scan [N] [runs]` | Range scans: B+ tree vs std::map, warm / cold pool / cold + OS cache dropped |
| `build/bench_lookup [N] [runs] [bucket_size]` | Point lookups: extendible hash vs std::unordered_map (+ std::map, B+ tree) |
| `bench/cachegrind_cache_misses.sh` | SIMULATED cache misses per key (valgrind cachegrind, L1d 32 KB, LL 8 MB) |
| `bench/perf_cache_misses.sh` | REAL hardware counters per key (perf stat); needs native Linux |
| `build/map_node_gaps` | Memory gap between consecutive std::map nodes, random vs ascending build |
| `build/tree_shape [N]` | B+ tree height / leaves / fill after N inserts |

`BENCH_VERBOSE=1` prints every individual run of the timing benchmarks.

## Running the real cache-miss measurement (native Linux laptop)

perf needs hardware performance counters: native Linux works; most VMs, WSL2 and
macOS do not.

```bash
git clone https://github.com/dev-aabhinav/project3 && cd project3
sudo apt install -y build-essential cmake ninja-build linux-tools-common linux-tools-generic linux-tools-$(uname -r)
sudo sysctl kernel.perf_event_paranoid=1          # temporary, resets on reboot
cmake -B build -G Ninja && cmake --build build
perf stat -e cycles,cache-misses true             # sanity: numbers, not <not supported>
./bench/perf_cache_misses.sh | tee perf_results.txt
```

If a column shows `n/a`, that event is not supported on the CPU; `perf list` shows
the names it does support (pass them with `EVENTS=a,b,c`).
