// Point-lookup benchmark: extendible hash table (through the buffer pool)
// vs std::unordered_map, with std::map and the B+ tree as extra context.
//
//   ./build/bench_lookup [N=1000000] [runs=5] [bucket_size=255]
//
// Timed: only the lookup loop (find + add the value to a checksum).
// Not timed: building the structures, generating the lookup keys.
// Each cell = median of `runs` timed runs.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "bench_util.h"
#include "index_engine/b_plus_tree.h"
#include "index_engine/extendible_hash_table.h"

using namespace index_engine;
using namespace bench;

namespace {

// Same mixing hash the extendible table uses, for a fair unordered_map.
struct MixHash {
    std::size_t operator()(KeyType k) const { return static_cast<std::size_t>(HashKey(k)); }
};


struct Result {
    double median_seconds = 0;
    double spread = 0;
    std::uint64_t checksum = 0;
    double extra = 0;
};


// Runs fn `runs` times (after one untimed warm-up call unless cold).
template <typename Fn>
Result Measure(const char* label, std::int64_t queries, int runs, Fn&& fn, bool warm_up = true) {
    Result r;
    std::vector<double> times;

    if (warm_up) {
        fn();
    }
    for (int i = 0; i < runs; ++i) {
        times.push_back(TimeOnce(fn, &r.checksum));
    }

    r.median_seconds = Median(times);
    r.spread = Spread(times);
    PrintRuns(label, queries, times);
    return r;
}


template <typename Map>
std::uint64_t LookupMap(const Map& m, const std::vector<KeyType>& queries) {
    std::uint64_t sum = 0;
    for (KeyType k : queries) {
        auto it = m.find(k);
        if (it != m.end()) {
            sum += static_cast<std::uint64_t>(it->second);
        }
    }
    return sum;
}


template <typename Index>
std::uint64_t LookupIndex(const Index& index, const std::vector<KeyType>& queries) {
    std::uint64_t sum = 0;
    for (KeyType k : queries) {
        std::optional<ValueType> v = index.Find(k);
        if (v) {
            sum += static_cast<std::uint64_t>(*v);
        }
    }
    return sum;
}


void PrintRow(const char* name, const char* condition, const Result& r, std::int64_t queries,
              const std::string& note) {
    std::printf("| %s | %s | %.1f ns ±%.0f%% | %s |\n", name, condition,
                r.median_seconds * 1e9 / static_cast<double>(queries), r.spread * 100,
                note.c_str());
}

}  // namespace


int main(int argc, char** argv) {
    const std::int64_t n = (argc > 1) ? std::atoll(argv[1]) : 1000000;
    const int runs = (argc > 2) ? std::atoi(argv[2]) : 5;
    const int bucket_size = (argc > 3) ? std::atoi(argv[3]) : kBucketCapacity;
    const std::int64_t warm_queries = n;
    const std::int64_t cold_queries = 10000;
    const std::size_t pool_frames = 16384;   // 64 MB > hash table and tree

    const std::string hash_path = "bench_lookup_hash.db";
    const std::string tree_path = "bench_lookup_tree.db";

    std::printf("# keys: even numbers 0..2(N-1), random insert order; hits = random even keys, misses = random odd keys\n");
    std::printf("# bench_lookup  N=%lld  runs=%d (median reported)  warm lookups/run=%lld  cold lookups/run=%lld\n",
                static_cast<long long>(n), runs, static_cast<long long>(warm_queries),
                static_cast<long long>(cold_queries));
    std::printf("# compiler %s, %s\n", __VERSION__,
#ifdef NDEBUG
                "NDEBUG (Release)"
#else
                "asserts ON (not a Release build!)"
#endif
    );

    // ---- Data: EVEN keys 0, 2, ..., 2(n-1) in a seeded random insertion order.
    //      Odd keys are then guaranteed misses that fall BETWEEN stored keys,
    //      so ordered structures don't always walk the same (hot) edge path.
    std::vector<KeyType> keys(n);
    for (std::int64_t i = 0; i < n; ++i) {
        keys[i] = 2 * i;
    }
    std::shuffle(keys.begin(), keys.end(), std::mt19937_64(42));

    // ---- Lookup keys: hits = random even keys, misses = random odd keys ----
    std::mt19937_64 rng(9);
    std::uniform_int_distribution<KeyType> index_dist(0, n - 1);

    std::vector<KeyType> hits(warm_queries);
    std::vector<KeyType> misses(warm_queries);
    std::vector<KeyType> cold_hits(cold_queries);
    for (auto& k : hits) k = 2 * index_dist(rng);
    for (auto& k : misses) k = 2 * index_dist(rng) + 1;
    for (auto& k : cold_hits) k = 2 * index_dist(rng);

    // ---- Build every structure from the same insertion order ----
    std::unordered_map<KeyType, ValueType> umap_std;          // std::hash = identity
    std::unordered_map<KeyType, ValueType, MixHash> umap_mix;
    std::map<KeyType, ValueType> tree_map;

    for (KeyType k : keys) {
        umap_std.emplace(k, k);
        umap_mix.emplace(k, k);
        tree_map.emplace(k, k);
    }

    std::remove(hash_path.c_str());
    std::remove(tree_path.c_str());

    page_id_t hash_meta;
    page_id_t tree_header;
    HashTableStats hstats;

    {
        DiskManager dm(hash_path);
        BufferPoolManager bpm(pool_frames, &dm);
        ExtendibleHashTable table(&bpm, kInvalidPageId, bucket_size);
        for (KeyType k : keys) {
            table.Insert(k, k);
        }
        hash_meta = table.MetaPageId();
        hstats = table.Stats();
    }
    {
        DiskManager dm(tree_path);
        BufferPoolManager bpm(pool_frames, &dm);
        BPlusTree tree(&bpm);
        for (KeyType k : keys) {
            tree.Insert(k, k);
        }
        tree_header = tree.HeaderPageId();
    }

    std::printf("# extendible hash: bucket_max_size %d, global depth %d, directory %lld entries (%lld pages), %lld buckets, avg bucket fill %.3f (%.1f keys/bucket)\n",
                bucket_size, hstats.global_depth, static_cast<long long>(hstats.directory_size),
                static_cast<long long>(hstats.directory_pages),
                static_cast<long long>(hstats.num_buckets), hstats.avg_bucket_fill,
                static_cast<double>(hstats.num_keys) / static_cast<double>(hstats.num_buckets));
    std::printf("# unordered_map: %zu buckets, load factor %.2f\n",
                umap_mix.bucket_count(), umap_mix.load_factor());

    // The warm pool must hold every page of the hash table: with small
    // buckets there are many more (each bucket is a whole 4KB page).
    const std::size_t hash_pool = std::max<std::size_t>(
        pool_frames, static_cast<std::size_t>(hstats.num_buckets + hstats.directory_pages + 64));
    std::printf("# hash table warm pool: %zu frames\n", hash_pool);

    DiskManager hash_dm(hash_path);
    DiskManager tree_dm(tree_path);

    std::uint64_t expected_hits = 0;   // all structures must agree
    bool all_match = true;
    auto check = [&](const Result& r) {
        if (r.checksum != expected_hits) all_match = false;
    };

    std::printf("\nper-lookup time, median of %d runs, ±spread = (max-min)/median\n", runs);
    std::printf("| structure | condition | time per lookup | notes |\n|---|---|---|---|\n");

    // ================= WARM: hits =================
    Result r_std = Measure("umap std::hash hit", warm_queries, runs,
                           [&] { return LookupMap(umap_std, hits); });
    expected_hits = r_std.checksum;

    Result r_mix = Measure("umap mixhash hit", warm_queries, runs,
                           [&] { return LookupMap(umap_mix, hits); });
    Result r_map = Measure("std::map hit", warm_queries, runs,
                           [&] { return LookupMap(tree_map, hits); });
    check(r_mix);
    check(r_map);

    Result r_hash;
    Result r_hash_miss;
    {
        BufferPoolManager bpm(hash_pool, &hash_dm);
        ExtendibleHashTable table(&bpm, hash_meta);

        LookupIndex(table, hits);    // warm-up: every bucket these keys touch
        LookupIndex(table, misses);
        std::uint64_t before = bpm.Misses();

        r_hash = Measure("ext hash warm hit", warm_queries, runs,
                         [&] { return LookupIndex(table, hits); });
        r_hash_miss = Measure("ext hash warm miss", warm_queries, runs,
                              [&] { return LookupIndex(table, misses); });
        r_hash.extra = static_cast<double>(bpm.Misses() - before);
    }
    check(r_hash);

    Result r_tree;
    Result r_tree_miss;
    {
        BufferPoolManager bpm(pool_frames, &tree_dm);
        BPlusTree tree(&bpm, tree_header);

        LookupIndex(tree, hits);
        LookupIndex(tree, misses);
        std::uint64_t before = bpm.Misses();

        r_tree = Measure("B+ tree warm hit", warm_queries, runs,
                         [&] { return LookupIndex(tree, hits); });
        r_tree_miss = Measure("B+ tree warm miss", warm_queries, runs,
                              [&] { return LookupIndex(tree, misses); });
        r_tree.extra = static_cast<double>(bpm.Misses() - before);
    }
    check(r_tree);

    // ================= WARM: misses (keys not in the table) =================
    Result r_std_miss = Measure("umap std::hash miss", warm_queries, runs,
                                [&] { return LookupMap(umap_std, misses); });
    Result r_mix_miss = Measure("umap mixhash miss", warm_queries, runs,
                                [&] { return LookupMap(umap_mix, misses); });
    Result r_map_miss = Measure("std::map miss", warm_queries, runs,
                                [&] { return LookupMap(tree_map, misses); });

    for (const Result* r : {&r_std_miss, &r_mix_miss, &r_map_miss, &r_hash_miss, &r_tree_miss}) {
        if (r->checksum != 0) all_match = false;   // a miss must find nothing
    }

    char note[160];

    PrintRow("std::unordered_map (std::hash = identity)", "warm, hit", r_std, warm_queries, "identity hash (libstdc++ default)");
    PrintRow("std::unordered_map (mixing hash)", "warm, hit", r_mix, warm_queries, "**main baseline**");
    PrintRow("std::map", "warm, hit", r_map, warm_queries, "context");
    std::snprintf(note, sizeof(note), "pool misses during timed runs: %g", r_hash.extra);
    PrintRow("extendible hash", "warm, hit", r_hash, warm_queries, note);
    std::snprintf(note, sizeof(note), "pool misses during timed runs: %g", r_tree.extra);
    PrintRow("B+ tree", "warm, hit", r_tree, warm_queries, note);

    PrintRow("std::unordered_map (std::hash = identity)", "warm, miss", r_std_miss, warm_queries, "");
    PrintRow("std::unordered_map (mixing hash)", "warm, miss", r_mix_miss, warm_queries, "");
    PrintRow("std::map", "warm, miss", r_map_miss, warm_queries, "");
    PrintRow("extendible hash", "warm, miss", r_hash_miss, warm_queries, "scans the whole bucket");
    PrintRow("B+ tree", "warm, miss", r_tree_miss, warm_queries, "");

    // ================= COLD: empty pool each run =================
    std::uint64_t cold_expected = LookupMap(umap_mix, cold_hits);

    auto run_cold = [&](auto make_and_lookup, DiskManager& dm, const std::string& path,
                        bool drop, const char* label, std::size_t frames) {
        Result r;
        std::vector<double> times;
        std::vector<double> reads;
        std::vector<double> resident;

        for (int i = 0; i < runs; ++i) {
            if (drop) {
                dm.DropOsCache();
                resident.push_back(ResidentFraction(path));
            }
            BufferPoolManager bpm(frames, &dm);
            std::uint64_t before = dm.NumReads();

            times.push_back(TimeOnce([&] { return make_and_lookup(bpm); }, &r.checksum));
            reads.push_back(static_cast<double>(dm.NumReads() - before));
        }

        r.median_seconds = Median(times);
        r.spread = Spread(times);
        r.extra = drop ? Median(resident) * 100 : Median(reads);
        PrintRuns(label, cold_queries, times);
        if (r.checksum != cold_expected) all_match = false;
        return r;
    };

    // Opening the structure (meta/header page read) is inside the timed region
    // here: it is part of what a cold lookup costs.
    auto hash_cold = [&](BufferPoolManager& bpm) {
        ExtendibleHashTable table(&bpm, hash_meta);
        return LookupIndex(table, cold_hits);
    };
    auto tree_cold = [&](BufferPoolManager& bpm) {
        BPlusTree tree(&bpm, tree_header);
        return LookupIndex(tree, cold_hits);
    };

    Result hc = run_cold(hash_cold, hash_dm, hash_path, false, "ext hash cold pool", hash_pool);
    Result hd = run_cold(hash_cold, hash_dm, hash_path, true, "ext hash cold+OS drop", hash_pool);
    Result tc = run_cold(tree_cold, tree_dm, tree_path, false, "B+ tree cold pool", pool_frames);
    Result td = run_cold(tree_cold, tree_dm, tree_path, true, "B+ tree cold+OS drop", pool_frames);

    std::snprintf(note, sizeof(note), "%g page reads per run (OS cache warm)", hc.extra);
    PrintRow("extendible hash", "cold pool, OS cached", hc, cold_queries, note);
    std::snprintf(note, sizeof(note), "OS-resident before run: %.1f%%", hd.extra);
    PrintRow("extendible hash", "cold pool + OS dropped", hd, cold_queries, note);
    std::snprintf(note, sizeof(note), "%g page reads per run (OS cache warm)", tc.extra);
    PrintRow("B+ tree", "cold pool, OS cached", tc, cold_queries, note);
    std::snprintf(note, sizeof(note), "OS-resident before run: %.1f%%", td.extra);
    PrintRow("B+ tree", "cold pool + OS dropped", td, cold_queries, note);

    std::printf("\nwarm hit ratios: ext hash / unordered_map(mixing) = %.2fx slower, "
                "ext hash / unordered_map(std::hash) = %.2fx slower, std::map / ext hash = %.2fx, "
                "B+ tree / ext hash = %.2fx\n",
                r_hash.median_seconds / r_mix.median_seconds,
                r_hash.median_seconds / r_std.median_seconds,
                r_map.median_seconds / r_hash.median_seconds,
                r_tree.median_seconds / r_hash.median_seconds);
    std::printf("checksums: %s\n", all_match ? "all match" : "MISMATCH");

    std::remove(hash_path.c_str());
    std::remove(tree_path.c_str());
    return all_match ? 0 : 1;
}
