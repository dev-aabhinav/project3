#include "index_engine/lru_k_replacer.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <deque>
#include <optional>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

#include "index_engine/buffer_pool_manager.h"

namespace index_engine {
namespace {

// Access each frame in `frames` once, in order.
void Access(LRUKReplacer& r, std::initializer_list<frame_id_t> frames) {
    for (frame_id_t f : frames) r.RecordAccess(f);
}


TEST(LRUKReplacerTest, EmptyHasNoVictim) {
    LRUKReplacer r(4);
    EXPECT_EQ(r.Victim(), std::nullopt);
    EXPECT_EQ(r.Size(), 0u);
}


// A frame used once ("infinite" 2-distance) goes before frames used twice,
// even if it was used more recently.
TEST(LRUKReplacerTest, FramesWithFewerThanKAccessesGoFirst) {
    LRUKReplacer r(4, 2);

    Access(r, {0, 0, 2, 2, 1});   // 1 is the most recent, but used only once
    r.Unpin(0);
    r.Unpin(1);
    r.Unpin(2);

    EXPECT_EQ(r.Victim(), 1);
}


TEST(LRUKReplacerTest, AmongSingleAccessFramesOldestGoesFirst) {
    LRUKReplacer r(4, 2);

    Access(r, {3, 1, 2});
    for (frame_id_t f : {1, 2, 3}) r.Unpin(f);

    EXPECT_EQ(r.Victim(), 3);
    EXPECT_EQ(r.Victim(), 1);
    EXPECT_EQ(r.Victim(), 2);
}


// The difference from plain LRU: frame 0 was used MOST recently, but its
// second-most-recent use (t=1) is older than frame 1's (t=2) -> evict 0.
TEST(LRUKReplacerTest, OrderedBySecondMostRecentAccessNotLastAccess) {
    LRUKReplacer r(4, 2);

    Access(r, {0, 1, 1, 0});   // 0: t1, t4   1: t2, t3
    r.Unpin(0);
    r.Unpin(1);

    EXPECT_EQ(r.Victim(), 0);
}


TEST(LRUKReplacerTest, PinnedFrameIsNeverAVictim) {
    LRUKReplacer r(4, 2);

    Access(r, {0, 1});
    r.Unpin(0);
    r.Unpin(1);
    r.Pin(0);

    EXPECT_EQ(r.Size(), 1u);
    EXPECT_EQ(r.Victim(), 1);
    EXPECT_EQ(r.Victim(), std::nullopt);
}


TEST(LRUKReplacerTest, AccessWhileEvictableReorders) {
    LRUKReplacer r(4, 2);

    Access(r, {0, 1});
    r.Unpin(0);
    r.Unpin(1);
    r.RecordAccess(0);   // 0 now has 2 accesses -> no longer "infinite"

    EXPECT_EQ(r.Victim(), 1);
    EXPECT_EQ(r.Victim(), 0);
}


TEST(LRUKReplacerTest, VictimAndForgetClearHistory) {
    LRUKReplacer r(4, 2);

    Access(r, {0, 0, 1, 1});
    r.Unpin(0);
    EXPECT_EQ(r.Victim(), 0);   // history of 0 wiped: it will hold a new page

    Access(r, {0});             // new page in frame 0: one access only
    r.Unpin(0);
    r.Unpin(1);
    EXPECT_EQ(r.Victim(), 0);   // 1 access beats 2 accesses

    Access(r, {2, 2});
    r.Forget(2);                // page deleted
    Access(r, {2});
    r.Unpin(2);
    EXPECT_EQ(r.Victim(), 2);   // treated as a fresh 1-access frame
}


// Brute force: per frame keep the last K times; victim = evictable frame
// minimising (has K accesses ? 1 : 0, front of history). Scans every frame.
class BruteLRUK {
public:
    BruteLRUK(int n, std::size_t k) : k_(k), hist_(n), evictable_(n, false) {}

    void Access(int f) {
        hist_[f].push_back(++clock_);
        if (hist_[f].size() > k_) hist_[f].pop_front();
    }
    void Pin(int f) { evictable_[f] = false; }
    void Unpin(int f) { evictable_[f] = true; }
    void Forget(int f) { evictable_[f] = false; hist_[f].clear(); }

    std::optional<int> Victim() {
        int best = -1;
        for (int f = 0; f < static_cast<int>(hist_.size()); ++f) {
            if (!evictable_[f]) continue;
            if (best == -1 || Key(f) < Key(best)) best = f;
        }
        if (best == -1) return std::nullopt;
        evictable_[best] = false;
        hist_[best].clear();
        return best;
    }

private:
    std::pair<int, std::uint64_t> Key(int f) const {
        return {hist_[f].size() >= k_ ? 1 : 0, hist_[f].empty() ? 0 : hist_[f].front()};
    }

    std::size_t k_;
    std::uint64_t clock_ = 0;
    std::vector<std::deque<std::uint64_t>> hist_;
    std::vector<bool> evictable_;
};


TEST(LRUKReplacerTest, MatchesBruteForceOnRandomOps) {
    for (std::size_t k : {1u, 2u, 3u}) {
        const int frames = 32;
        LRUKReplacer fast(frames, k);
        BruteLRUK brute(frames, k);

        std::mt19937 rng(static_cast<unsigned>(k) * 1000 + 7);
        std::uniform_int_distribution<int> frame(0, frames - 1);
        std::uniform_int_distribution<int> op(0, 9);

        for (int i = 0; i < 100000; ++i) {
            int f = frame(rng);
            switch (op(rng)) {
                case 0: case 1: case 2: case 3: fast.RecordAccess(f); brute.Access(f); break;
                case 4: case 5: fast.Unpin(f); brute.Unpin(f); break;
                case 6: fast.Pin(f); brute.Pin(f); break;
                case 7: fast.Forget(f); brute.Forget(f); break;
                default: ASSERT_EQ(fast.Victim(), brute.Victim()) << "k=" << k << " op " << i;
            }
        }
    }
}


// ---------------------------------------------------------------
// Sequential flooding inside the real buffer pool.
// 8 frames. Hot pages 0..3 are used twice each, then a "scan" touches 30
// other pages once each, then the hot pages are read again.
// ---------------------------------------------------------------

std::uint64_t HotMissesAfterScan(ReplacementPolicy policy, const std::string& path) {
    std::remove(path.c_str());
    DiskManager dm(path);
    BufferPoolManager bpm(8, &dm, policy);

    for (int i = 0; i < 40; ++i) {
        page_id_t pid;
        bpm.NewPage(&pid);
        bpm.UnpinPage(pid, true);
    }

    for (int round = 0; round < 2; ++round) {       // hot set: 2 accesses each
        for (page_id_t p = 0; p < 4; ++p) {
            bpm.FetchPage(p);
            bpm.UnpinPage(p, false);
        }
    }

    for (page_id_t p = 10; p < 40; ++p) {           // the scan: 1 access each
        bpm.FetchPage(p);
        bpm.UnpinPage(p, false);
    }

    std::uint64_t before = bpm.Misses();
    for (page_id_t p = 0; p < 4; ++p) {             // hot set again
        bpm.FetchPage(p);
        bpm.UnpinPage(p, false);
    }

    std::remove(path.c_str());
    return bpm.Misses() - before;
}


TEST(SequentialFloodingTest, ScanFlushesHotPagesUnderLRU) {
    EXPECT_EQ(HotMissesAfterScan(ReplacementPolicy::kLRU,
                                 "flood_lru_" + std::to_string(::getpid()) + ".db"), 4u);
}


TEST(SequentialFloodingTest, HotPagesSurviveScanUnderLRU2) {
    EXPECT_EQ(HotMissesAfterScan(ReplacementPolicy::kLRU2,
                                 "flood_lru2_" + std::to_string(::getpid()) + ".db"), 0u);
}


// The whole buffer pool must stay correct with LRU-2 under heavy eviction.
TEST(SequentialFloodingTest, PoolContentsCorrectWithLRU2) {
    const std::string path = "lru2_contents_" + std::to_string(::getpid()) + ".db";
    std::remove(path.c_str());
    DiskManager dm(path);
    BufferPoolManager bpm(8, &dm, ReplacementPolicy::kLRU2);

    std::vector<std::string> expected(200);
    for (int i = 0; i < 200; ++i) {
        page_id_t pid;
        Page* page = bpm.NewPage(&pid);
        expected[pid] = "v0-" + std::to_string(pid);
        std::strcpy(page->GetData(), expected[pid].c_str());
        bpm.UnpinPage(pid, true);
    }

    std::mt19937 rng(5);
    std::uniform_int_distribution<int> page_dist(0, 199);
    for (int i = 0; i < 50000; ++i) {
        page_id_t pid = page_dist(rng);
        Page* page = bpm.FetchPage(pid);
        ASSERT_EQ(std::string(page->GetData()), expected[pid]) << "op " << i;

        bool write = (i % 3 == 0);
        if (write) {
            expected[pid] = "v" + std::to_string(i);
            std::strcpy(page->GetData(), expected[pid].c_str());
        }
        bpm.UnpinPage(pid, write);
    }
    std::remove(path.c_str());
}

}  // namespace
}  // namespace index_engine
