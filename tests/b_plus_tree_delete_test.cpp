#include "index_engine/b_plus_tree.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

#include "tree_test_util.h"

namespace index_engine {
namespace {

using namespace tree_test;

class BPlusTreeDeleteTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = "bptree_delete_" + std::to_string(::getpid()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name() +
                ".db";
        std::remove(path_.c_str());
    }

    void TearDown() override {
        std::remove(path_.c_str());
    }

    void InsertAll(BPlusTree& tree, std::initializer_list<KeyType> keys) {
        for (KeyType k : keys) {
            ASSERT_TRUE(tree.Insert(k, ValueFor(k)));
        }
    }

    std::string path_;
};


// ---------------------------------------------------------------
// Small cases and whiteboard shapes (leaf_max = 3, internal_max = 3,
// so min leaf = 2 keys, min internal = 2 children)
// ---------------------------------------------------------------

TEST_F(BPlusTreeDeleteTest, RemoveMissingKeyReturnsFalse) {
    DiskManager dm(path_);
    BufferPoolManager bpm(8, &dm);
    BPlusTree tree(&bpm);

    EXPECT_FALSE(tree.Remove(1));   // empty tree

    tree.Insert(5, ValueFor(5));
    EXPECT_FALSE(tree.Remove(6));
    EXPECT_EQ(tree.Find(5), ValueFor(5));
}


TEST_F(BPlusTreeDeleteTest, RootLeafCanEmptyCompletely) {
    DiskManager dm(path_);
    BufferPoolManager bpm(8, &dm);
    BPlusTree tree(&bpm, kInvalidPageId, 3, 3);

    InsertAll(tree, {1, 2, 3});

    EXPECT_TRUE(tree.Remove(2));
    EXPECT_TRUE(tree.Remove(1));    // root leaf may go below half full
    EXPECT_EQ(tree.Height(), 1);

    EXPECT_TRUE(tree.Remove(3));
    EXPECT_TRUE(tree.IsEmpty());
    EXPECT_EQ(tree.Height(), 0);

    EXPECT_TRUE(tree.Insert(9, ValueFor(9)));  // usable again
    EXPECT_EQ(tree.Find(9), ValueFor(9));
}


// (30) [10 20] [30 40 50]  --remove 10-->  [20] is underfull,
// right sibling has 3 > 2: BORROW 30.  Separator becomes 40.
//   (40) [20 30] [40 50]
TEST_F(BPlusTreeDeleteTest, WhiteboardLeafBorrowFromRight) {
    DiskManager dm(path_);
    BufferPoolManager bpm(16, &dm);
    BPlusTree tree(&bpm, kInvalidPageId, 3, 3);

    InsertAll(tree, {10, 20, 30, 40, 50});
    ASSERT_EQ(SeparatorKeys(&bpm, tree.RootPageId()), (std::vector<KeyType>{30}));

    tree.Remove(10);

    page_id_t root = tree.RootPageId();
    EXPECT_EQ(SeparatorKeys(&bpm, root), (std::vector<KeyType>{40}));
    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, root, 0)), (std::vector<KeyType>{20, 30}));
    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, root, 1)), (std::vector<KeyType>{40, 50}));
    EXPECT_EQ(TreeChecker(&bpm, tree).Check(), 4);
}


// (30) [10 15 20] [30 40]  --remove 40-->  [30] is underfull,
// left sibling has 3 > 2: BORROW 20.  Separator becomes 20.
//   (20) [10 15] [20 30]
TEST_F(BPlusTreeDeleteTest, WhiteboardLeafBorrowFromLeft) {
    DiskManager dm(path_);
    BufferPoolManager bpm(16, &dm);
    BPlusTree tree(&bpm, kInvalidPageId, 3, 3);

    InsertAll(tree, {10, 20, 30, 40, 15});
    tree.Remove(40);

    page_id_t root = tree.RootPageId();
    EXPECT_EQ(SeparatorKeys(&bpm, root), (std::vector<KeyType>{20}));
    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, root, 0)), (std::vector<KeyType>{10, 15}));
    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, root, 1)), (std::vector<KeyType>{20, 30}));
    EXPECT_EQ(TreeChecker(&bpm, tree).Check(), 4);
}


// (30) [10 20] [30 40]  --remove 10-->  [20] underfull, sibling has only 2:
// MERGE into [20 30 40]. Root now has 1 child -> ROOT COLLAPSE, height 2 -> 1.
TEST_F(BPlusTreeDeleteTest, WhiteboardLeafMergeAndRootCollapse) {
    DiskManager dm(path_);
    BufferPoolManager bpm(16, &dm);
    BPlusTree tree(&bpm, kInvalidPageId, 3, 3);

    InsertAll(tree, {10, 20, 30, 40});
    ASSERT_EQ(tree.Height(), 2);

    tree.Remove(10);

    EXPECT_EQ(tree.Height(), 1);
    EXPECT_EQ(LeafKeys(&bpm, tree.RootPageId()), (std::vector<KeyType>{20, 30, 40}));
    EXPECT_EQ(TreeChecker(&bpm, tree).Check(), 3);
}


// Starts from the tree drawn in the M6 checkpoint (inserts 10..80):
//                  (50)
//          (30)             (70)
//    [10 20]  [30 40]  [50 60]  [70 80]
// remove 10: [20] merges with [30 40] -> [20 30 40]; (30) is left with one
// child -> underfull; sibling (70) has only 2 children -> INTERNAL MERGE:
// separator 50 is PULLED DOWN. Root left with one child -> collapse.
//            (50 | 70)
//    [20 30 40]  [50 60]  [70 80]
TEST_F(BPlusTreeDeleteTest, WhiteboardInternalMergePullsSeparatorDown) {
    DiskManager dm(path_);
    BufferPoolManager bpm(16, &dm);
    BPlusTree tree(&bpm, kInvalidPageId, 3, 3);

    InsertAll(tree, {10, 20, 30, 40, 50, 60, 70, 80});
    ASSERT_EQ(tree.Height(), 3);

    tree.Remove(10);

    EXPECT_EQ(tree.Height(), 2);

    page_id_t root = tree.RootPageId();
    EXPECT_EQ(SeparatorKeys(&bpm, root), (std::vector<KeyType>{50, 70}));
    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, root, 0)), (std::vector<KeyType>{20, 30, 40}));
    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, root, 1)), (std::vector<KeyType>{50, 60}));
    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, root, 2)), (std::vector<KeyType>{70, 80}));
    EXPECT_EQ(TreeChecker(&bpm, tree).Check(), 7);
}


// Same as above plus 90, 100, so the right internal node is (70 | 90) with
// 3 children and CAN lend. remove 10: after the leaf merge, (30) has one
// child and BORROWS from (70 | 90) by rotating through the root:
// root separator 50 comes down, sibling's first key 70 goes up.
//                  (70)
//          (50)             (90)
//  [20 30 40]  [50 60]  [70 80]  [90 100]
TEST_F(BPlusTreeDeleteTest, WhiteboardInternalBorrowRotatesThroughParent) {
    DiskManager dm(path_);
    BufferPoolManager bpm(16, &dm);
    BPlusTree tree(&bpm, kInvalidPageId, 3, 3);

    InsertAll(tree, {10, 20, 30, 40, 50, 60, 70, 80, 90, 100});
    tree.Remove(10);

    EXPECT_EQ(tree.Height(), 3);

    page_id_t root = tree.RootPageId();
    page_id_t left = ChildAt(&bpm, root, 0);
    page_id_t right = ChildAt(&bpm, root, 1);

    EXPECT_EQ(SeparatorKeys(&bpm, root), (std::vector<KeyType>{70}));
    EXPECT_EQ(SeparatorKeys(&bpm, left), (std::vector<KeyType>{50}));
    EXPECT_EQ(SeparatorKeys(&bpm, right), (std::vector<KeyType>{90}));

    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, left, 0)), (std::vector<KeyType>{20, 30, 40}));
    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, left, 1)), (std::vector<KeyType>{50, 60}));
    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, right, 0)), (std::vector<KeyType>{70, 80}));
    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, right, 1)), (std::vector<KeyType>{90, 100}));
    EXPECT_EQ(TreeChecker(&bpm, tree).Check(), 9);
}


// ---------------------------------------------------------------
// Randomized: mixed inserts and deletes against std::map
// ---------------------------------------------------------------

void RunMixedWorkload(BPlusTree& tree, BufferPoolManager* bpm, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<KeyType> key_dist(0, 6000);
    std::uniform_int_distribution<int> coin(0, 1);

    std::map<KeyType, ValueType> truth;

    // Grow first, then mix 50/50 inserts and removes.
    for (int i = 0; i < 4000; ++i) {
        KeyType k = key_dist(rng);
        ASSERT_EQ(tree.Insert(k, ValueFor(k)), truth.emplace(k, ValueFor(k)).second);
    }

    for (int i = 0; i < 40000; ++i) {
        KeyType k = key_dist(rng);

        if (coin(rng) == 0) {
            ASSERT_EQ(tree.Insert(k, ValueFor(k)), truth.emplace(k, ValueFor(k)).second)
                << "insert op " << i;
        } else {
            ASSERT_EQ(tree.Remove(k), truth.erase(k) == 1) << "remove op " << i;
        }

        if (i % 2000 == 0) {
            ASSERT_EQ(TreeChecker(bpm, tree).Check(),
                      static_cast<std::int64_t>(truth.size())) << "after op " << i;
        }
    }

    // Full scan must equal std::map exactly, in order.
    auto expected = truth.begin();
    for (auto it = tree.Begin(); it.Valid(); it.Next(), ++expected) {
        ASSERT_NE(expected, truth.end());
        ASSERT_EQ(it.Key(), expected->first);
        ASSERT_EQ(it.Value(), expected->second);
    }
    ASSERT_EQ(expected, truth.end());

    // Delete everything in random order: the tree must shrink to empty.
    std::vector<KeyType> remaining;
    for (const auto& kv : truth) {
        remaining.push_back(kv.first);
    }
    std::shuffle(remaining.begin(), remaining.end(), rng);

    for (std::size_t i = 0; i < remaining.size(); ++i) {
        ASSERT_TRUE(tree.Remove(remaining[i])) << "final delete " << i;

        if (i % 500 == 0) {
            ASSERT_EQ(TreeChecker(bpm, tree).Check(),
                      static_cast<std::int64_t>(remaining.size() - i - 1));
        }
    }

    EXPECT_TRUE(tree.IsEmpty());
    EXPECT_EQ(tree.Height(), 0);
}


TEST_F(BPlusTreeDeleteTest, MixedWorkloadTinyNodesMatchStdMap) {
    DiskManager dm(path_);
    BufferPoolManager bpm(64, &dm);
    BPlusTree tree(&bpm, kInvalidPageId, 3, 4);

    RunMixedWorkload(tree, &bpm, 1);
}


// leaf_max = 2 means min leaf = 1: every delete from a 1-key leaf rebalances.
TEST_F(BPlusTreeDeleteTest, MixedWorkloadSmallestNodesMatchStdMap) {
    DiskManager dm(path_);
    BufferPoolManager bpm(64, &dm);
    BPlusTree tree(&bpm, kInvalidPageId, 2, 3);

    RunMixedWorkload(tree, &bpm, 2);
}


// Real page-sized nodes: 100k keys, delete half, then the rest.
TEST_F(BPlusTreeDeleteTest, RealNodeSizesDeleteHalfThenAll) {
    const int kN = 100000;

    DiskManager dm(path_);
    BufferPoolManager bpm(256, &dm);
    BPlusTree tree(&bpm);

    std::vector<KeyType> keys(kN);
    std::iota(keys.begin(), keys.end(), 0);
    std::shuffle(keys.begin(), keys.end(), std::mt19937(5));

    for (KeyType k : keys) {
        tree.Insert(k, ValueFor(k));
    }
    ASSERT_EQ(tree.Height(), 3);

    for (KeyType k : keys) {
        if (k % 2 == 0) {
            ASSERT_TRUE(tree.Remove(k));
        }
    }

    ASSERT_EQ(TreeChecker(&bpm, tree).Check(), kN / 2);

    for (KeyType k = 0; k < kN; ++k) {
        ASSERT_EQ(tree.Find(k), (k % 2 == 0) ? std::nullopt : std::optional<ValueType>(ValueFor(k)));
    }

    for (KeyType k : keys) {
        if (k % 2 == 1) {
            ASSERT_TRUE(tree.Remove(k));
        }
    }

    EXPECT_TRUE(tree.IsEmpty());
}


TEST_F(BPlusTreeDeleteTest, DeletesWorkWithATinyPoolAndLeakNoPins) {
    DiskManager dm(path_);
    BufferPoolManager bpm(4, &dm);  // parent + node + sibling + header
    BPlusTree tree(&bpm, kInvalidPageId, 3, 3);

    for (KeyType k = 0; k < 2000; ++k) {
        tree.Insert(k, ValueFor(k));
    }

    for (KeyType k = 0; k < 2000; k += 3) {
        ASSERT_NO_THROW(tree.Remove(k)) << "remove " << k;
    }

    ASSERT_EQ(TreeChecker(&bpm, tree).Check(), 2000 - 667);

    for (std::size_t i = 0; i < bpm.PoolSize(); ++i) {
        page_id_t pid;
        ASSERT_NO_THROW(bpm.NewPage(&pid)) << "frame " << i << " still pinned";
    }
}


// ---------------------------------------------------------------
// Range scan / iterator
// ---------------------------------------------------------------

TEST_F(BPlusTreeDeleteTest, IteratorOnEmptyTreeIsInvalid) {
    DiskManager dm(path_);
    BufferPoolManager bpm(8, &dm);
    BPlusTree tree(&bpm);

    EXPECT_FALSE(tree.Begin().Valid());
    EXPECT_FALSE(tree.Begin(42).Valid());
}


// (30) [10 20] [30 40]: Begin(25) lands past the end of the first leaf
// and must hop to the next leaf.
TEST_F(BPlusTreeDeleteTest, BeginFindsFirstKeyAtOrAfterLo) {
    DiskManager dm(path_);
    BufferPoolManager bpm(8, &dm);
    BPlusTree tree(&bpm, kInvalidPageId, 3, 3);

    InsertAll(tree, {10, 20, 30, 40});

    EXPECT_EQ(tree.Begin(-5).Key(), 10);
    EXPECT_EQ(tree.Begin(20).Key(), 20);   // exact match
    EXPECT_EQ(tree.Begin(25).Key(), 30);   // crosses into the next leaf
    EXPECT_EQ(tree.Begin(40).Key(), 40);
    EXPECT_FALSE(tree.Begin(41).Valid());  // past the last key
}


// Random [lo, hi) range queries on real node sizes: count and sum must
// match std::map.
TEST_F(BPlusTreeDeleteTest, RangeQueriesMatchStdMap) {
    DiskManager dm(path_);
    BufferPoolManager bpm(128, &dm);
    BPlusTree tree(&bpm);

    std::mt19937_64 rng(77);
    std::uniform_int_distribution<KeyType> key_dist(0, 1000000);

    std::map<KeyType, ValueType> truth;

    for (int i = 0; i < 50000; ++i) {
        KeyType k = key_dist(rng);
        tree.Insert(k, ValueFor(k));
        truth.emplace(k, ValueFor(k));
    }

    for (int q = 0; q < 300; ++q) {
        KeyType lo = key_dist(rng);
        KeyType hi = lo + key_dist(rng) / 20;   // ranges up to ~50k wide

        std::int64_t count = 0;
        std::int64_t sum = 0;
        for (auto it = tree.Begin(lo); it.Valid() && it.Key() < hi; it.Next()) {
            ++count;
            sum += it.Value();
        }

        std::int64_t expected_count = 0;
        std::int64_t expected_sum = 0;
        for (auto it = truth.lower_bound(lo); it != truth.end() && it->first < hi; ++it) {
            ++expected_count;
            expected_sum += it->second;
        }

        ASSERT_EQ(count, expected_count) << "range [" << lo << ", " << hi << ")";
        ASSERT_EQ(sum, expected_sum) << "range [" << lo << ", " << hi << ")";
    }
}


// The iterator holds one pin at a time, so a full scan over many leaves
// works even with a 3-frame pool, and releases its pin when it ends.
TEST_F(BPlusTreeDeleteTest, FullScanHoldsOnePinAtATime) {
    DiskManager dm(path_);
    BufferPoolManager bpm(3, &dm);
    BPlusTree tree(&bpm, kInvalidPageId, 4, 4);

    for (KeyType k = 0; k < 1000; ++k) {
        tree.Insert(k, ValueFor(k));
    }

    KeyType expected = 0;
    for (auto it = tree.Begin(); it.Valid(); it.Next()) {
        ASSERT_EQ(it.Key(), expected++);
    }
    EXPECT_EQ(expected, 1000);

    for (std::size_t i = 0; i < bpm.PoolSize(); ++i) {
        page_id_t pid;
        ASSERT_NO_THROW(bpm.NewPage(&pid)) << "frame " << i << " still pinned";
    }
}

}  // namespace
}  // namespace index_engine
