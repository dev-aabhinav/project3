// Measures the memory address gap between consecutive std::map nodes during an
// in-order scan, for a map built in random vs ascending key order (both alive at
// once, as in bench_range_scan). Evidence for WHY the two maps scan at different
// speeds even though cachegrind counts the same number of cache misses.
//
//   ./build/map_node_gaps


#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <numeric>
#include <random>
#include <vector>

void Report(const char* name, const std::map<long long, long long>& m) {
    std::vector<long long> gaps;
    const void* prev = nullptr;
    for (const auto& kv : m) {
        const void* cur = &kv;
        if (prev) gaps.push_back(static_cast<long long>(reinterpret_cast<std::intptr_t>(cur) - reinterpret_cast<std::intptr_t>(prev)));
        prev = cur;
    }
    std::vector<long long> abs_gaps(gaps.size());
    std::transform(gaps.begin(), gaps.end(), abs_gaps.begin(), [](long long g) { return std::llabs(g); });
    std::sort(abs_gaps.begin(), abs_gaps.end());
    long long same_4k = std::count_if(abs_gaps.begin(), abs_gaps.end(), [](long long g) { return g < 4096; });
    long long fwd64 = std::count(gaps.begin(), gaps.end(), 64LL);
    std::printf("%-14s median |gap| %9lld B, 90th pct %10lld B, gap == +64 B: %5.1f%%, next node within 4 KB: %5.1f%%\n",
                name, abs_gaps[abs_gaps.size() / 2], abs_gaps[abs_gaps.size() * 9 / 10],
                100.0 * fwd64 / gaps.size(), 100.0 * same_4k / gaps.size());
}

int main() {
    const long long n = 1000000;
    std::vector<long long> keys(n);
    std::iota(keys.begin(), keys.end(), 0);
    std::shuffle(keys.begin(), keys.end(), std::mt19937_64(42));
    // Both maps alive at once (as in bench_range_scan), so the second one
    // gets fresh heap memory instead of reusing the first one's freed nodes.
    std::map<long long, long long> random_map;
    for (auto k : keys) random_map.emplace(k, k);
    std::map<long long, long long> asc_map;
    for (long long k = 0; k < n; ++k) asc_map.emplace(k, k);
    Report("random build", random_map);
    Report("ascending build", asc_map);
    std::printf("sizeof node payload: %zu B (pair) + 32 B rb-tree header; malloc rounds the node to 64 B\n",
                sizeof(std::pair<const long long, long long>));
}
