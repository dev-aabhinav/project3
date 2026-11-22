// Growth of the extendible hash table under uniform vs Zipfian insert streams.
//
//   ./build/hash_growth [attempts=2000000] [universe=10000000] [pool_frames=65536] [uniform_only=0]
//
// For each stream, inserts `attempts` keys drawn from a universe of
// `universe` possible keys (duplicates are rejected by the table) and prints:
//   - shape at 10 checkpoints: distinct keys, global depth, directory size,
//     buckets, average fill, splits, doublings
//   - every directory doubling: when it happened and how long that insert took
//   - latency of all other inserts (p50 / p99 / max)
// The whole table stays in the buffer pool (no disk I/O after creation).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "index_engine/extendible_hash_table.h"
#include "zipf.h"

using namespace index_engine;
using Clock = std::chrono::steady_clock;

namespace {

struct Stream {
    std::string name;
    double theta;   // 0 = uniform
};


double Percentile(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    return v[static_cast<std::size_t>(p * static_cast<double>(v.size() - 1))];
}


void Run(const Stream& stream, std::int64_t attempts, std::uint64_t universe, std::size_t pool_frames) {
    const std::string path = "hash_growth.db";
    std::remove(path.c_str());

    DiskManager dm(path);
    BufferPoolManager bpm(pool_frames, &dm);   // sized so the whole table stays in memory
    ExtendibleHashTable table(&bpm);

    std::mt19937_64 uniform_rng(123);
    std::uniform_int_distribution<std::uint64_t> uniform(0, universe - 1);
    // Only Zipf streams build a generator: its setup sums over the whole
    // universe (O(universe)), which is huge for the uniform runs.
    std::optional<bench::ZipfGenerator> zipf;
    if (stream.theta > 0) {
        zipf.emplace(universe, stream.theta, 123);
    }

    std::printf("\n## stream: %s\n", stream.name.c_str());

    // Sanity check the generator: measured share of the top key vs theory.
    if (stream.theta > 0) {
        bench::ZipfGenerator probe(universe, stream.theta, 999);
        std::unordered_map<std::uint64_t, int> counts;
        const int samples = 1000000;
        for (int i = 0; i < samples; ++i) ++counts[probe.Next()];
        std::printf("generator check: rank-0 share measured %.4f vs theory %.4f; "
                    "distinct ranks in 1M samples: %zu\n",
                    static_cast<double>(counts[0]) / samples, probe.TopProbability(),
                    counts.size());
    }

    std::printf("| attempts | distinct keys | dup rate | global depth | directory entries | buckets | avg fill | splits | doublings |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|\n");

    std::vector<double> normal_us;
    normal_us.reserve(attempts);
    std::int64_t distinct = 0;

    struct Doubling { std::int64_t distinct; int new_depth; double us; };
    std::vector<Doubling> doublings;

    for (std::int64_t i = 1; i <= attempts; ++i) {
        KeyType key = static_cast<KeyType>(zipf ? zipf->Next() : uniform(uniform_rng));

        std::int64_t doublings_before = table.NumDoublings();
        auto t0 = Clock::now();
        bool inserted = table.Insert(key, key);
        auto t1 = Clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();

        distinct += inserted ? 1 : 0;

        if (table.NumDoublings() != doublings_before) {
            doublings.push_back({distinct, table.GlobalDepth(), us});
        } else {
            normal_us.push_back(us);
        }

        if (i % (attempts / 10) == 0) {
            HashTableStats s = table.Stats();
            std::printf("| %lld | %lld | %.1f%% | %d | %lld | %lld | %.3f | %lld | %lld |\n",
                        static_cast<long long>(i), static_cast<long long>(distinct),
                        100.0 * static_cast<double>(i - distinct) / static_cast<double>(i),
                        s.global_depth, static_cast<long long>(s.directory_size),
                        static_cast<long long>(s.num_buckets), s.avg_bucket_fill,
                        static_cast<long long>(table.NumSplits()),
                        static_cast<long long>(table.NumDoublings()));
        }
    }

    HashTableStats s = table.Stats();
    std::printf("buckets per local depth:");
    for (std::size_t d = 0; d < s.buckets_by_local_depth.size(); ++d) {
        if (s.buckets_by_local_depth[d] > 0) {
            std::printf(" d=%zu:%lld", d, static_cast<long long>(s.buckets_by_local_depth[d]));
        }
    }
    std::printf("\n");

    std::printf("pool misses after creation: %llu (0 = everything stayed in memory)\n",
                static_cast<unsigned long long>(bpm.Misses()));
    std::printf("insert latency without doubling: p50 %.2f us, p99 %.2f us, p99.9 %.2f us, max %.1f us (%zu inserts)\n",
                Percentile(normal_us, 0.50), Percentile(normal_us, 0.99), Percentile(normal_us, 0.999),
                *std::max_element(normal_us.begin(), normal_us.end()), normal_us.size());

    std::printf("directory doublings (the insert that triggered each):\n");
    for (const Doubling& d : doublings) {
        std::printf("  -> depth %2d (%7lld entries) at %8lld distinct keys: %8.1f us\n",
                    d.new_depth, static_cast<long long>(std::int64_t{1} << d.new_depth),
                    static_cast<long long>(d.distinct), d.us);
    }

    std::remove(path.c_str());
}

}  // namespace


int main(int argc, char** argv) {
    const std::int64_t attempts = (argc > 1) ? std::atoll(argv[1]) : 2000000;
    const std::uint64_t universe = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 10000000;
    const std::size_t pool_frames = (argc > 3) ? std::strtoull(argv[3], nullptr, 10) : 65536;
    const bool uniform_only = (argc > 4) && std::atoi(argv[4]) != 0;

    std::printf("# hash_growth: %lld insert attempts per stream, key universe %llu, bucket size %d, mixing hash\n",
                static_cast<long long>(attempts), static_cast<unsigned long long>(universe),
                kBucketCapacity);

    std::vector<Stream> streams{{"uniform", 0.0}};
    if (!uniform_only) {
        streams.push_back({"zipf theta=0.99", 0.99});
        streams.push_back({"zipf theta=1.2", 1.2});
    }

    for (const Stream& s : streams) {
        Run(s, attempts, universe, pool_frames);
    }
    return 0;
}
