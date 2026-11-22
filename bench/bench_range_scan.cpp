// Range-scan benchmark: B+ tree (through the buffer pool) vs std::map.
//
//   ./build/bench_range_scan [N=1000000] [runs=5]
//
// Timed: only the scan loop (lower_bound / Begin + walk + sum of values).
// Not timed: building the structures, generating the start keys.
// Each cell = median of `runs` timed runs.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "bench_util.h"
#include "index_engine/b_plus_tree.h"

using namespace index_engine;
using namespace bench;

namespace {

template <typename Map>
std::uint64_t ScanMap(const Map& m, KeyType lo, std::int64_t count) {
    std::uint64_t sum = 0;
    auto it = m.lower_bound(lo);

    for (std::int64_t i = 0; i < count && it != m.end(); ++i, ++it) {
        sum += static_cast<std::uint64_t>(it->second);
    }
    return sum;
}


std::uint64_t ScanTree(const BPlusTree& tree, KeyType lo, std::int64_t count) {
    std::uint64_t sum = 0;
    auto it = tree.Begin(lo);

    for (std::int64_t i = 0; i < count && it.Valid(); ++i, it.Next()) {
        sum += static_cast<std::uint64_t>(it.Value());
    }
    return sum;
}


struct Workload {
    std::int64_t range;
    std::vector<KeyType> starts;
};


struct Cell {
    double median_seconds = 0;
    double spread = 0;   // (max - min) / median across runs
    std::uint64_t checksum = 0;
    double extra = 0;   // misses, reads or resident fraction, depending on condition
};

}  // namespace


int main(int argc, char** argv) {
    const std::int64_t n = (argc > 1) ? std::atoll(argv[1]) : 1000000;
    const int runs = (argc > 2) ? std::atoi(argv[2]) : 5;
    const std::string path = "bench_range.db";
    const std::size_t warm_pool = 16384;   // 64 MB > whole tree

    std::printf("# bench_range_scan  N=%lld  runs=%d (median reported)\n",
                static_cast<long long>(n), runs);
    std::printf("# compiler %s, %s\n", __VERSION__,
#ifdef NDEBUG
                "NDEBUG (Release)"
#else
                "asserts ON (not a Release build!)"
#endif
    );

    // ---- Data: keys 0..n-1 inserted in a seeded random order ----
    std::vector<KeyType> keys(n);
    std::iota(keys.begin(), keys.end(), 0);
    std::shuffle(keys.begin(), keys.end(), std::mt19937_64(42));

    std::map<KeyType, ValueType> map_random;
    for (KeyType k : keys) {
        map_random.emplace(k, k);
    }

    std::map<KeyType, ValueType> map_ascending;   // extra context: allocation order
    for (KeyType k = 0; k < n; ++k) {
        map_ascending.emplace(k, k);
    }

    std::remove(path.c_str());
    page_id_t header_id;
    TreeStats shape;
    {
        DiskManager dm(path);
        BufferPoolManager bpm(warm_pool, &dm);
        BPlusTree tree(&bpm);

        for (KeyType k : keys) {
            tree.Insert(k, k);
        }
        header_id = tree.HeaderPageId();
        shape = tree.Stats();
    }  // flushed to the file

    std::printf("# tree: height %d, %lld leaves, %lld internal, fill %.3f\n", shape.height,
                static_cast<long long>(shape.leaf_pages),
                static_cast<long long>(shape.internal_pages), shape.avg_leaf_fill);

    // ---- Workloads: same start keys for every structure ----
    std::mt19937_64 rng(7);
    std::vector<Workload> workloads;

    for (auto [range, queries] : std::vector<std::pair<std::int64_t, int>>{
             {10, 10000}, {1000, 1000}, {100000, 10}, {n, 1}}) {
        Workload w{range, {}};
        std::uniform_int_distribution<KeyType> start(0, n - range);

        for (int q = 0; q < queries; ++q) {
            w.starts.push_back(range == n ? 0 : start(rng));
        }
        workloads.push_back(std::move(w));
    }

    DiskManager dm(path);

    std::printf("\nper-query time, median of %d runs, ±spread = (max-min)/median\n", runs);
    std::printf("| range | queries | map (random ins.) | map (asc. ins.) | B+ warm | B+ cold pool, OS cached | B+ cold pool + OS dropped | warm: map(random)/B+ |\n");
    std::printf("|---|---|---|---|---|---|---|---|\n");

    for (const Workload& w : workloads) {
        auto per_query_us = [&](double s) { return s * 1e6 / w.starts.size(); };

        auto run_map = [&](const std::map<KeyType, ValueType>& m, const char* label) {
            Cell c;
            std::vector<double> times;
            std::uint64_t sum = 0;

            for (KeyType lo : w.starts) sum += ScanMap(m, lo, w.range);   // warm-up

            for (int r = 0; r < runs; ++r) {
                times.push_back(TimeOnce([&] {
                    std::uint64_t s = 0;
                    for (KeyType lo : w.starts) s += ScanMap(m, lo, w.range);
                    return s;
                }, &c.checksum));
            }
            c.median_seconds = Median(times);
            c.spread = Spread(times);
            PrintRuns(label, w.range, times);
            return c;
        };

        Cell map_r = run_map(map_random, "map random-ins");
        Cell map_a = run_map(map_ascending, "map asc-ins");

        // ---- B+ tree, warm: every page resident, verified 0 misses ----
        Cell warm;
        {
            BufferPoolManager bpm(warm_pool, &dm);
            BPlusTree tree(&bpm, header_id);

            // Warm-up: a full scan loads every leaf, and one untimed pass of
            // this workload loads every internal node it descends through.
            ScanTree(tree, 0, n);
            for (KeyType lo : w.starts) ScanTree(tree, lo, w.range);

            std::uint64_t misses_before = bpm.Misses();
            std::vector<double> times;

            for (int r = 0; r < runs; ++r) {
                times.push_back(TimeOnce([&] {
                    std::uint64_t s = 0;
                    for (KeyType lo : w.starts) s += ScanTree(tree, lo, w.range);
                    return s;
                }, &warm.checksum));
            }
            warm.median_seconds = Median(times);
            warm.spread = Spread(times);
            PrintRuns("B+ warm", w.range, times);
            warm.extra = static_cast<double>(bpm.Misses() - misses_before);
        }

        // ---- B+ tree, cold pool: fresh empty pool each run ----
        auto run_cold = [&](bool drop_os_cache) {
            Cell c;
            std::vector<double> times;
            std::vector<double> reads;
            std::vector<double> resident;

            for (int r = 0; r < runs; ++r) {
                if (drop_os_cache) {
                    dm.DropOsCache();
                    resident.push_back(ResidentFraction(path));
                }

                BufferPoolManager bpm(warm_pool, &dm);
                BPlusTree tree(&bpm, header_id);   // reads only the header page
                std::uint64_t reads_before = dm.NumReads();

                times.push_back(TimeOnce([&] {
                    std::uint64_t s = 0;
                    for (KeyType lo : w.starts) s += ScanTree(tree, lo, w.range);
                    return s;
                }, &c.checksum));

                reads.push_back(static_cast<double>(dm.NumReads() - reads_before));
            }
            c.median_seconds = Median(times);
            c.spread = Spread(times);
            PrintRuns(drop_os_cache ? "B+ cold+OS drop" : "B+ cold pool", w.range, times);
            c.extra = drop_os_cache ? Median(resident) : Median(reads);
            return c;
        };

        Cell cold = run_cold(false);
        Cell cold_disk = run_cold(true);

        bool ok = map_r.checksum == map_a.checksum && map_r.checksum == warm.checksum &&
                  map_r.checksum == cold.checksum && map_r.checksum == cold_disk.checksum;

        auto pct = [](const Cell& c) { return c.spread * 100; };

        std::printf("| %lld | %zu | %.2f us ±%.0f%% | %.2f us ±%.0f%% | %.2f us ±%.0f%% (misses %g) | %.2f us ±%.0f%% (%g reads) | %.2f us ±%.0f%% (resident %.1f%%) | %.2fx | %s\n",
                    static_cast<long long>(w.range), w.starts.size(),
                    per_query_us(map_r.median_seconds), pct(map_r),
                    per_query_us(map_a.median_seconds), pct(map_a),
                    per_query_us(warm.median_seconds), pct(warm), warm.extra,
                    per_query_us(cold.median_seconds), pct(cold), cold.extra,
                    per_query_us(cold_disk.median_seconds), pct(cold_disk), cold_disk.extra * 100,
                    map_r.median_seconds / warm.median_seconds,
                    ok ? "" : "CHECKSUM MISMATCH");
    }

    std::remove(path.c_str());
    return 0;
}
