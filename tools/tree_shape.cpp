// Prints the shape of a B+ tree after inserting N keys in ascending and in
// random order (default N = 1,000,000). Shape only: no timing.
//
//   ./build/tree_shape [N]

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "index_engine/b_plus_tree.h"

using namespace index_engine;

static void Report(const char* order, const std::vector<KeyType>& keys) {
    const std::string path = "tree_shape.db";
    std::remove(path.c_str());

    TreeStats s;
    {
        DiskManager dm(path);
        BufferPoolManager bpm(16384, &dm);   // 64 MB pool, everything fits
        BPlusTree tree(&bpm);

        for (KeyType k : keys) {
            tree.Insert(k, k);
        }
        s = tree.Stats();
    }
    std::remove(path.c_str());

    std::printf("%-10s keys=%lld height=%d leaves=%lld internal=%lld "
                "avg_leaf_fill=%.3f keys/leaf=%.1f file_MB=%.1f\n",
                order, static_cast<long long>(s.num_keys), s.height,
                static_cast<long long>(s.leaf_pages),
                static_cast<long long>(s.internal_pages), s.avg_leaf_fill,
                static_cast<double>(s.num_keys) / s.leaf_pages,
                (s.leaf_pages + s.internal_pages + 1) * 4096.0 / (1024 * 1024));
}

int main(int argc, char** argv) {
    long long n = (argc > 1) ? std::atoll(argv[1]) : 1000000;

    std::vector<KeyType> keys(n);
    std::iota(keys.begin(), keys.end(), 0);
    Report("ascending", keys);

    std::shuffle(keys.begin(), keys.end(), std::mt19937_64(2026));
    Report("random", keys);
    return 0;
}
