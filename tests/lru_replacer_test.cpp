#include "index_engine/lru_replacer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <list>
#include <optional>
#include <random>
#include <stdexcept>

namespace index_engine {
namespace {

TEST(LRUReplacerTest, EmptyHasNoVictim) {
    LRUReplacer replacer(4);

    EXPECT_EQ(replacer.Size(), 0u);
    EXPECT_EQ(replacer.Victim(), std::nullopt);
}


TEST(LRUReplacerTest, VictimsComeOutInUnpinOrder) {
    LRUReplacer replacer(4);

    replacer.Unpin(1);
    replacer.Unpin(2);
    replacer.Unpin(3);

    EXPECT_EQ(replacer.Size(), 3u);
    EXPECT_EQ(replacer.Victim(), 1);  // oldest unpinned first
    EXPECT_EQ(replacer.Victim(), 2);
    EXPECT_EQ(replacer.Victim(), 3);
    EXPECT_EQ(replacer.Victim(), std::nullopt);
    EXPECT_EQ(replacer.Size(), 0u);
}


TEST(LRUReplacerTest, PinnedFrameIsNeverAVictim) {
    LRUReplacer replacer(4);

    replacer.Unpin(1);
    replacer.Unpin(2);
    replacer.Unpin(3);
    replacer.Pin(2);  // middle of the list

    EXPECT_EQ(replacer.Size(), 2u);
    EXPECT_EQ(replacer.Victim(), 1);
    EXPECT_EQ(replacer.Victim(), 3);
    EXPECT_EQ(replacer.Victim(), std::nullopt);
}


TEST(LRUReplacerTest, PinHeadAndTailKeepsListConsistent) {
    LRUReplacer replacer(4);

    replacer.Unpin(0);
    replacer.Unpin(1);
    replacer.Unpin(2);

    replacer.Pin(0);  // tail (least recent)
    replacer.Pin(2);  // head (most recent)

    EXPECT_EQ(replacer.Victim(), 1);
    EXPECT_EQ(replacer.Victim(), std::nullopt);
}


TEST(LRUReplacerTest, AllPinnedMeansNoVictim) {
    LRUReplacer replacer(3);

    replacer.Unpin(0);
    replacer.Unpin(1);
    replacer.Pin(0);
    replacer.Pin(1);

    EXPECT_EQ(replacer.Victim(), std::nullopt);
}


TEST(LRUReplacerTest, PinOfAbsentFrameIsNoop) {
    LRUReplacer replacer(4);

    replacer.Pin(3);

    EXPECT_EQ(replacer.Size(), 0u);
}


TEST(LRUReplacerTest, UnpinTwiceKeepsOriginalPosition) {
    LRUReplacer replacer(4);

    replacer.Unpin(1);
    replacer.Unpin(2);
    replacer.Unpin(1);  // already evictable: no-op

    EXPECT_EQ(replacer.Size(), 2u);
    EXPECT_EQ(replacer.Victim(), 1);
}


TEST(LRUReplacerTest, UnpinAfterPinBecomesMostRecent) {
    LRUReplacer replacer(4);

    replacer.Unpin(1);
    replacer.Unpin(2);
    replacer.Pin(1);    // 1 is used again
    replacer.Unpin(1);  // and released: now newer than 2

    EXPECT_EQ(replacer.Victim(), 2);
    EXPECT_EQ(replacer.Victim(), 1);
}


TEST(LRUReplacerTest, InvalidFrameIdThrows) {
    LRUReplacer replacer(4);

    EXPECT_THROW(replacer.Unpin(4), std::out_of_range);
    EXPECT_THROW(replacer.Pin(-1), std::out_of_range);
}


// Stress test: random Pin / Unpin / Victim against a simple std::list
// reference (front = least recent). The reference scans linearly: slow
// but obviously correct.
TEST(LRUReplacerTest, MatchesBruteForceOnRandomOps) {
    const int kFrames = 64;
    const int kOps = 200000;

    LRUReplacer fast(kFrames);
    std::list<frame_id_t> brute;

    std::mt19937 rng(777);
    std::uniform_int_distribution<int> frame_dist(0, kFrames - 1);
    std::uniform_int_distribution<int> op_dist(0, 2);

    for (int i = 0; i < kOps; ++i) {
        frame_id_t f = frame_dist(rng);
        auto it = std::find(brute.begin(), brute.end(), f);

        switch (op_dist(rng)) {
            case 0:  // Unpin
                fast.Unpin(f);
                if (it == brute.end()) {
                    brute.push_back(f);
                }
                break;

            case 1:  // Pin
                fast.Pin(f);
                if (it != brute.end()) {
                    brute.erase(it);
                }
                break;

            case 2: {  // Victim
                std::optional<frame_id_t> expected;
                if (!brute.empty()) {
                    expected = brute.front();
                    brute.pop_front();
                }
                ASSERT_EQ(fast.Victim(), expected) << "op " << i;
                break;
            }
        }

        ASSERT_EQ(fast.Size(), brute.size()) << "op " << i;
    }
}

}  // namespace
}  // namespace index_engine
