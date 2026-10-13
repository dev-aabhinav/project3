#include "index_engine/lru_cache.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace index_engine {
namespace {

TEST(LRUCacheTest, MissOnEmptyCache) {
    LRUCache<int, int> cache(2);

    EXPECT_EQ(cache.Get(1), std::nullopt);
}


TEST(LRUCacheTest, PutThenGet) {
    LRUCache<int, int> cache(2);

    cache.Put(1, 100);
    cache.Put(2, 200);

    EXPECT_EQ(cache.Get(1), 100);
    EXPECT_EQ(cache.Get(2), 200);
    EXPECT_EQ(cache.Size(), 2u);
}


TEST(LRUCacheTest, EvictsLeastRecentlyUsed) {
    LRUCache<int, int> cache(2);

    cache.Put(1, 100);
    cache.Put(2, 200);
    cache.Put(3, 300);  // full: evicts 1 (oldest)

    EXPECT_EQ(cache.Get(1), std::nullopt);
    EXPECT_EQ(cache.Get(2), 200);
    EXPECT_EQ(cache.Get(3), 300);
    EXPECT_EQ(cache.Evictions(), 1u);
}


TEST(LRUCacheTest, GetRefreshesRecency) {
    LRUCache<int, int> cache(2);

    cache.Put(1, 100);
    cache.Put(2, 200);
    cache.Get(1);       // now 2 is the oldest
    cache.Put(3, 300);  // evicts 2, not 1

    EXPECT_EQ(cache.Get(1), 100);
    EXPECT_EQ(cache.Get(2), std::nullopt);
}


TEST(LRUCacheTest, PutExistingKeyUpdatesAndRefreshes) {
    LRUCache<int, int> cache(2);

    cache.Put(1, 100);
    cache.Put(2, 200);
    cache.Put(1, 111);  // update, and 1 becomes most recent
    cache.Put(3, 300);  // evicts 2

    EXPECT_EQ(cache.Get(1), 111);
    EXPECT_EQ(cache.Get(2), std::nullopt);
    EXPECT_EQ(cache.Size(), 2u);
}


TEST(LRUCacheTest, CapacityOne) {
    LRUCache<int, int> cache(1);

    cache.Put(1, 100);
    cache.Put(2, 200);

    EXPECT_EQ(cache.Get(1), std::nullopt);
    EXPECT_EQ(cache.Get(2), 200);
}


TEST(LRUCacheTest, ZeroCapacityThrows) {
    EXPECT_THROW((LRUCache<int, int>(0)), std::invalid_argument);
}


// Brute-force reference: a vector in recency order (front = newest).
// O(n) per operation, but obviously correct.
class BruteLRU {
public:
    explicit BruteLRU(std::size_t cap) : cap_(cap) {}

    std::optional<int> Get(int key) {
        auto it = Find(key);

        if (it == items_.end()) {
            return std::nullopt;
        }

        std::pair<int, int> item = *it;
        items_.erase(it);
        items_.insert(items_.begin(), item);
        return item.second;
    }

    void Put(int key, int value) {
        auto it = Find(key);

        if (it != items_.end()) {
            items_.erase(it);
        } else if (items_.size() == cap_) {
            items_.pop_back();
        }

        items_.insert(items_.begin(), {key, value});
    }

private:
    std::vector<std::pair<int, int>>::iterator Find(int key) {
        return std::find_if(items_.begin(), items_.end(),
                            [key](const auto& p) { return p.first == key; });
    }

    std::size_t cap_;
    std::vector<std::pair<int, int>> items_;
};


// Stress test: same random operations on both, results must always match.
TEST(LRUCacheTest, MatchesBruteForceOnRandomOps) {
    const std::size_t kCapacity = 16;
    const int kKeyRange = 40;  // > capacity, so evictions happen often
    const int kOps = 200000;

    LRUCache<int, int> fast(kCapacity);
    BruteLRU brute(kCapacity);

    std::mt19937 rng(12345);  // fixed seed: failures are reproducible
    std::uniform_int_distribution<int> key_dist(0, kKeyRange - 1);
    std::uniform_int_distribution<int> op_dist(0, 1);

    for (int i = 0; i < kOps; ++i) {
        int key = key_dist(rng);

        if (op_dist(rng) == 0) {
            ASSERT_EQ(fast.Get(key), brute.Get(key)) << "op " << i;
        } else {
            fast.Put(key, i);
            brute.Put(key, i);
        }
    }
}

}  // namespace
}  // namespace index_engine
