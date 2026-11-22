// Sequential flooding: does a full table scan flush the hot pages of a
// Zipfian point-lookup workload out of the buffer pool? LRU vs LRU-2.
//
//   ./build/bench_flooding [reps=3]
//
// B+ tree with 1M keys (~5,700 pages). Pool holds only part of it.
// Lookups are skewed at the PAGE level: Zipf theta=0.99 picks one of ~5,700
// leaf-sized key ranges (range ids scrambled with a hash so hot ranges are
// spread over the file), then a uniform key inside that range.
//
//   warm-up : 300,000 lookups (untimed)
//   phase A : 50,000 lookups                                   (baseline)
//   phase B : one full scan, 1 lookup after every 200 scanned keys (~5,000)
//   phase C : 50,000 lookups right after the scan; the first 5,000 are also
//             reported separately (recovery window)
//
// Reported for the LOOKUPS only:
//   disk %  = lookups that needed at least one pool miss (went to the device)
//   p50/p99 = per-lookup latency in microseconds
// Every lookup touches 4 pages (header, root, internal, leaf); the top 3 almost
// always hit, so "disk %" is the meaningful number (page hit rate hides it).
// The OS page cache is dropped before each phase, so a pool miss is a real
// device read. Disk % is deterministic (same seed); latencies = median of reps.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "bench_util.h"
#include "index_engine/b_plus_tree.h"
#include "index_engine/hash_table_page.h"   // HashKey (rank scrambling)
#include "zipf.h"

using namespace index_engine;
using namespace bench;

namespace {

constexpr std::int64_t kN = 1000000;
const char* kPath = "bench_flood.db";


struct PhaseResult {
    std::uint64_t lookups = 0;
    std::uint64_t disk_lookups = 0;   // lookups with >= 1 pool miss
    std::uint64_t hits = 0;           // pool hits during lookups
    std::uint64_t misses = 0;         // pool misses during lookups
    std::vector<double> latency_us;

    double DiskPct() const {
        return 100.0 * static_cast<double>(disk_lookups) / static_cast<double>(lookups);
    }
    double HitRate() const {
        return 100.0 * static_cast<double>(hits) / static_cast<double>(hits + misses);
    }
    double Mean() const {
        return std::accumulate(latency_us.begin(), latency_us.end(), 0.0) /
               static_cast<double>(latency_us.size());
    }
    double P(double q) const {
        std::vector<double> v = latency_us;
        std::sort(v.begin(), v.end());
        return v[static_cast<std::size_t>(q * static_cast<double>(v.size() - 1))];
    }
};


class LookupDriver {
public:
    LookupDriver(BPlusTree& tree, BufferPoolManager& bpm, std::int64_t ranges)
        : tree_(tree), bpm_(bpm), ranges_(ranges), zipf_(ranges, 0.99, 2024), rng_(77) {}

    void One(PhaseResult* r) {
        // Zipf over leaf-sized key ranges (scrambled), uniform inside a range.
        std::int64_t range = static_cast<std::int64_t>(
            HashKey(static_cast<KeyType>(zipf_.Next())) % static_cast<std::uint64_t>(ranges_));
        std::int64_t lo = range * kN / ranges_;
        std::int64_t hi = (range + 1) * kN / ranges_ - 1;
        KeyType key = std::uniform_int_distribution<KeyType>(lo, hi)(rng_);

        std::uint64_t h0 = bpm_.Hits();
        std::uint64_t m0 = bpm_.Misses();
        auto t0 = Clock::now();

        std::optional<ValueType> v = tree_.Find(key);

        auto t1 = Clock::now();
        if (!v || *v != key) {
            std::fprintf(stderr, "lookup of %lld failed\n", static_cast<long long>(key));
            std::exit(1);
        }

        if (r != nullptr) {
            r->lookups += 1;
            r->hits += bpm_.Hits() - h0;
            r->misses += bpm_.Misses() - m0;
            r->disk_lookups += (bpm_.Misses() != m0) ? 1 : 0;
            r->latency_us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
    }

private:
    BPlusTree& tree_;
    BufferPoolManager& bpm_;
    std::int64_t ranges_;
    ZipfGenerator zipf_;
    std::mt19937_64 rng_;
};


struct ConfigResult {
    PhaseResult a, b, c, c_first;
    double scan_ms = 0;
};


ConfigResult RunOnce(ReplacementPolicy policy, std::size_t frames, page_id_t header,
                     std::int64_t ranges) {
    DiskManager dm(kPath);
    BufferPoolManager bpm(frames, &dm, policy);
    BPlusTree tree(&bpm, header);
    LookupDriver driver(tree, bpm, ranges);

    ConfigResult res;

    for (int i = 0; i < 300000; ++i) driver.One(nullptr);   // warm-up

    dm.DropOsCache();
    for (int i = 0; i < 50000; ++i) driver.One(&res.a);

    dm.DropOsCache();
    auto s0 = Clock::now();
    std::uint64_t scanned = 0;
    std::uint64_t checksum = 0;
    for (auto it = tree.Begin(); it.Valid(); it.Next()) {
        checksum += static_cast<std::uint64_t>(it.Value());
        if (++scanned % 200 == 0) {
            driver.One(&res.b);
        }
    }
    res.scan_ms = std::chrono::duration<double, std::milli>(Clock::now() - s0).count();
    if (checksum != static_cast<std::uint64_t>(kN) * (kN - 1) / 2) {
        std::fprintf(stderr, "scan checksum mismatch\n");
        std::exit(1);
    }

    dm.DropOsCache();
    for (int i = 0; i < 50000; ++i) {
        driver.One(&res.c);
        if (i < 5000) {
            res.c_first.lookups += 1;   // copy the recovery window's numbers
            res.c_first.disk_lookups = res.c.disk_lookups;
            res.c_first.latency_us.push_back(res.c.latency_us.back());
        }
    }

    return res;
}

}  // namespace


int main(int argc, char** argv) {
    const int reps = (argc > 1) ? std::atoi(argv[1]) : 3;

    // Build the 1M-key tree once (random insertion order, as in M9).
    std::remove(kPath);
    page_id_t header;
    TreeStats shape;
    {
        std::vector<KeyType> keys(kN);
        std::iota(keys.begin(), keys.end(), 0);
        std::shuffle(keys.begin(), keys.end(), std::mt19937_64(42));

        DiskManager dm(kPath);
        BufferPoolManager bpm(16384, &dm);
        BPlusTree tree(&bpm);
        for (KeyType k : keys) tree.Insert(k, k);
        header = tree.HeaderPageId();
        shape = tree.Stats();
    }
    const std::int64_t pages = shape.leaf_pages + shape.internal_pages + 1;

    std::printf("# bench_flooding: 1M-key B+ tree, %lld pages (%lld leaves); page-level Zipf 0.99 lookups; reps=%d\n",
                static_cast<long long>(pages), static_cast<long long>(shape.leaf_pages), reps);
    std::printf("# disk %% = lookups needing >= 1 pool miss; p50/p99 per lookup in us; page hit %% in brackets\n\n");
    std::printf("| policy | pool (%% of tree) | A baseline: disk %% (page hit %%) | A: mean / p50 / p99 | B during scan: disk %% (page hit %%) | B: mean / p50 / p99 | C first 5k after: disk %% | C first 5k: mean / p99 | C all 50k: disk %% | scan ms |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|\n");

    for (std::size_t frames : {512, 1024, 2048}) {
        for (ReplacementPolicy policy : {ReplacementPolicy::kLRU, ReplacementPolicy::kLRU2}) {
            std::vector<ConfigResult> runs;
            for (int r = 0; r < reps; ++r) runs.push_back(RunOnce(policy, frames, header, shape.leaf_pages));

            auto med = [&](auto f) {
                std::vector<double> v;
                for (const ConfigResult& c : runs) v.push_back(f(c));
                return Median(v);
            };

            const ConfigResult& c0 = runs[0];   // disk % identical across reps (same seed)
            std::printf("| %s | %zu (%.0f%%) | %.1f (%.1f) | %.1f / %.2f / %.1f | %.1f (%.1f) | %.1f / %.2f / %.1f | %.1f | %.1f / %.1f | %.1f | %.0f |\n",
                        policy == ReplacementPolicy::kLRU ? "LRU" : "LRU-2", frames,
                        100.0 * static_cast<double>(frames) / static_cast<double>(pages),
                        c0.a.DiskPct(), c0.a.HitRate(),
                        med([](const ConfigResult& c) { return c.a.Mean(); }),
                        med([](const ConfigResult& c) { return c.a.P(0.5); }),
                        med([](const ConfigResult& c) { return c.a.P(0.99); }),
                        c0.b.DiskPct(), c0.b.HitRate(),
                        med([](const ConfigResult& c) { return c.b.Mean(); }),
                        med([](const ConfigResult& c) { return c.b.P(0.5); }),
                        med([](const ConfigResult& c) { return c.b.P(0.99); }),
                        c0.c_first.DiskPct(),
                        med([](const ConfigResult& c) { return c.c_first.Mean(); }),
                        med([](const ConfigResult& c) { return c.c_first.P(0.99); }),
                        c0.c.DiskPct(),
                        med([](const ConfigResult& c) { return c.scan_ms; }));
            std::fflush(stdout);

            for (const ConfigResult& c : runs) {
                if (c.a.DiskPct() != c0.a.DiskPct() || c.b.DiskPct() != c0.b.DiskPct() ||
                    c.c.DiskPct() != c0.c.DiskPct()) {
                    std::printf("  note: disk %% differed between reps\n");
                }
            }
        }
    }

    std::remove(kPath);
    return 0;
}
