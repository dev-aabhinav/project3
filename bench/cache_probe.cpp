// Runs ONE workload on ONE structure R times, for external counters
// (perf stat on real hardware, or valgrind cachegrind's simulated cache).
// No timing inside: the tool measures. Run with R=0 and R=5 and subtract,
// so setup (building the structure, warming the pool) cancels out.
//
//   ./build/cache_probe prepare                       (build the B+ tree file once)
//   ./build/cache_probe <map_random|map_asc|btree> <scan|range1000> <R>
//
// Both workloads touch 1,000,000 keys per repetition:
//   scan      = one full scan of all keys
//   range1000 = 1,000 range queries of 1,000 keys from fixed random starts

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "index_engine/b_plus_tree.h"

using namespace index_engine;

namespace {

constexpr std::int64_t kN = 1000000;
const char* kDbPath = "cache_probe.db";

std::vector<KeyType> RandomOrderKeys() {
    std::vector<KeyType> keys(kN);
    std::iota(keys.begin(), keys.end(), 0);
    std::shuffle(keys.begin(), keys.end(), std::mt19937_64(42));   // same as bench_range_scan
    return keys;
}

std::vector<KeyType> RangeStarts() {
    std::mt19937_64 rng(7);
    std::uniform_int_distribution<KeyType> start(0, kN - 1000);
    std::vector<KeyType> starts(1000);
    for (auto& s : starts) s = start(rng);
    return starts;
}

template <typename Map>
std::uint64_t MapWork(const Map& m, const std::string& work, const std::vector<KeyType>& starts) {
    std::uint64_t sum = 0;

    if (work == "scan") {
        for (const auto& kv : m) sum += static_cast<std::uint64_t>(kv.second);
        return sum;
    }
    for (KeyType lo : starts) {
        auto it = m.lower_bound(lo);
        for (int i = 0; i < 1000 && it != m.end(); ++i, ++it) {
            sum += static_cast<std::uint64_t>(it->second);
        }
    }
    return sum;
}

std::uint64_t TreeWork(const BPlusTree& tree, const std::string& work,
                       const std::vector<KeyType>& starts) {
    std::uint64_t sum = 0;

    if (work == "scan") {
        for (auto it = tree.Begin(); it.Valid(); it.Next()) sum += static_cast<std::uint64_t>(it.Value());
        return sum;
    }
    for (KeyType lo : starts) {
        auto it = tree.Begin(lo);
        for (int i = 0; i < 1000 && it.Valid(); ++i, it.Next()) {
            sum += static_cast<std::uint64_t>(it.Value());
        }
    }
    return sum;
}

}  // namespace


int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "prepare") == 0) {
        std::remove(kDbPath);
        DiskManager dm(kDbPath);
        BufferPoolManager bpm(16384, &dm);
        BPlusTree tree(&bpm);
        for (KeyType k : RandomOrderKeys()) tree.Insert(k, k);
        std::printf("prepared %s (header page %d)\n", kDbPath, tree.HeaderPageId());
        return 0;
    }

    if (argc != 4) {
        std::fprintf(stderr, "usage: %s prepare | <map_random|map_asc|btree> <scan|range1000> <R>\n", argv[0]);
        return 2;
    }

    const std::string structure = argv[1];
    const std::string work = argv[2];
    const int repeats = std::atoi(argv[3]);
    const std::vector<KeyType> starts = RangeStarts();

    std::uint64_t checksum = 0;

    if (structure == "map_random" || structure == "map_asc") {
        std::map<KeyType, ValueType> m;

        if (structure == "map_random") {
            for (KeyType k : RandomOrderKeys()) m.emplace(k, k);
        } else {
            for (KeyType k = 0; k < kN; ++k) m.emplace(k, k);
        }

        for (int r = 0; r < repeats; ++r) checksum += MapWork(m, work, starts);
    } else if (structure == "btree") {
        DiskManager dm(kDbPath);
        BufferPoolManager bpm(16384, &dm);   // 64 MB: the whole tree fits
        BPlusTree tree(&bpm, 0);             // header page is page 0 (made by `prepare`)

        // Warm-up (part of setup, cancels in the R=5 minus R=0 subtraction):
        // full scan loads every leaf, one pass of the workload loads the
        // internal nodes it needs.
        (void)TreeWork(tree, "scan", starts);
        (void)TreeWork(tree, work, starts);

        std::uint64_t misses_before = bpm.Misses();
        for (int r = 0; r < repeats; ++r) checksum += TreeWork(tree, work, starts);

        if (bpm.Misses() != misses_before) {
            std::fprintf(stderr, "warning: %llu pool misses during measured work (not warm)\n",
                         static_cast<unsigned long long>(bpm.Misses() - misses_before));
        }
    } else {
        std::fprintf(stderr, "unknown structure %s\n", structure.c_str());
        return 2;
    }

    std::printf("checksum %llu\n", static_cast<unsigned long long>(checksum));
    return 0;
}
