#include "index_engine/b_plus_tree.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

#include "index_engine/page_guard.h"
#include "tree_test_util.h"

namespace index_engine {
namespace {

class BPlusTreeInsertTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = "bptree_insert_" + std::to_string(::getpid()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name() +
                ".db";
        std::remove(path_.c_str());
    }

    void TearDown() override {
        std::remove(path_.c_str());
    }

    std::string path_;
};


using namespace tree_test;

// ---------------------------------------------------------------
// Small cases
// ---------------------------------------------------------------

TEST_F(BPlusTreeInsertTest, FirstInsertCreatesLeafRoot) {
    DiskManager dm(path_);
    BufferPoolManager bpm(8, &dm);
    BPlusTree tree(&bpm);

    EXPECT_TRUE(tree.Insert(5, ValueFor(5)));

    EXPECT_EQ(tree.Height(), 1);
    EXPECT_EQ(tree.Find(5), ValueFor(5));
    EXPECT_EQ(tree.Find(6), std::nullopt);
}


TEST_F(BPlusTreeInsertTest, DuplicateKeyIsRejectedAndValueUnchanged) {
    DiskManager dm(path_);
    BufferPoolManager bpm(8, &dm);
    BPlusTree tree(&bpm);

    EXPECT_TRUE(tree.Insert(5, ValueFor(5)));
    EXPECT_FALSE(tree.Insert(5, 999));

    EXPECT_EQ(tree.Find(5), ValueFor(5));
    EXPECT_EQ(TreeChecker(&bpm, tree).Check(), 1);
}


TEST_F(BPlusTreeInsertTest, InvalidSizeLimitsThrow) {
    DiskManager dm(path_);
    BufferPoolManager bpm(8, &dm);

    EXPECT_THROW(BPlusTree(&bpm, kInvalidPageId, 1, 4), std::invalid_argument);
    EXPECT_THROW(BPlusTree(&bpm, kInvalidPageId, 4, 2), std::invalid_argument);
    EXPECT_THROW(BPlusTree(&bpm, kInvalidPageId, kLeafCapacity + 1, 4), std::invalid_argument);
}


// Whiteboard example 1: LEAF SPLIT (copy up).
// leaf_max = 4. Insert 10 20 30 40 -> one full leaf. Insert 50:
//   temp [10 20 30 40 50] -> left [10 20 30], right [40 50]
//   separator 40 is COPIED up and also stays in the right leaf.
TEST_F(BPlusTreeInsertTest, WhiteboardLeafSplitCopiesUp) {
    DiskManager dm(path_);
    BufferPoolManager bpm(8, &dm);
    BPlusTree tree(&bpm, kInvalidPageId, 4, 4);

    for (KeyType k : {10, 20, 30, 40}) {
        tree.Insert(k, ValueFor(k));
    }
    EXPECT_EQ(tree.Height(), 1);

    tree.Insert(50, ValueFor(50));

    EXPECT_EQ(tree.Height(), 2);

    page_id_t root = tree.RootPageId();
    EXPECT_EQ(SeparatorKeys(&bpm, root), (std::vector<KeyType>{40}));
    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, root, 0)), (std::vector<KeyType>{10, 20, 30}));
    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, root, 1)), (std::vector<KeyType>{40, 50}));

    EXPECT_EQ(TreeChecker(&bpm, tree).Check(), 5);
}


// Whiteboard example 2: INTERNAL SPLIT (push up).
// leaf_max = 2, internal_max = 3. Insert 1..7 ascending:
//   after 6: root children [L1 L2 L3] keys [3 5]   (full: 3 children)
//            leaves [1 2] [3 4] [5 6]
//   insert 7: leaf [5 6] splits -> [5 6] [7], separator 7 goes to the root
//   root would have 4 children, keys [3 5 7] -> split:
//            left internal  keys [3]   children [1 2] [3 4]
//            right internal keys [7]   children [5 6] [7]
//            5 is PUSHED up into the new root and kept in neither half.
TEST_F(BPlusTreeInsertTest, WhiteboardInternalSplitPushesUp) {
    DiskManager dm(path_);
    BufferPoolManager bpm(16, &dm);
    BPlusTree tree(&bpm, kInvalidPageId, 2, 3);

    for (KeyType k = 1; k <= 6; ++k) {
        tree.Insert(k, ValueFor(k));
    }

    EXPECT_EQ(tree.Height(), 2);
    EXPECT_EQ(SeparatorKeys(&bpm, tree.RootPageId()), (std::vector<KeyType>{3, 5}));

    tree.Insert(7, ValueFor(7));

    EXPECT_EQ(tree.Height(), 3);

    page_id_t root = tree.RootPageId();
    page_id_t left = ChildAt(&bpm, root, 0);
    page_id_t right = ChildAt(&bpm, root, 1);

    EXPECT_EQ(SeparatorKeys(&bpm, root), (std::vector<KeyType>{5}));
    EXPECT_EQ(SeparatorKeys(&bpm, left), (std::vector<KeyType>{3}));
    EXPECT_EQ(SeparatorKeys(&bpm, right), (std::vector<KeyType>{7}));

    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, left, 0)), (std::vector<KeyType>{1, 2}));
    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, left, 1)), (std::vector<KeyType>{3, 4}));
    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, right, 0)), (std::vector<KeyType>{5, 6}));
    EXPECT_EQ(LeafKeys(&bpm, ChildAt(&bpm, right, 1)), (std::vector<KeyType>{7}));

    EXPECT_EQ(TreeChecker(&bpm, tree).Check(), 7);
}


// ---------------------------------------------------------------
// Bigger randomized tests
// ---------------------------------------------------------------

// Tiny nodes make a deep tree quickly: splits at every level, many root
// splits. Invariants checked repeatedly, contents checked against std::map.
TEST_F(BPlusTreeInsertTest, RandomInsertsTinyNodesMatchStdMap) {
    DiskManager dm(path_);
    BufferPoolManager bpm(64, &dm);
    BPlusTree tree(&bpm, kInvalidPageId, 3, 4);

    std::mt19937_64 rng(4242);
    std::uniform_int_distribution<KeyType> key_dist(-50000, 50000);

    std::map<KeyType, ValueType> truth;

    for (int i = 0; i < 20000; ++i) {
        KeyType k = key_dist(rng);
        bool inserted = tree.Insert(k, ValueFor(k));
        bool expected = truth.emplace(k, ValueFor(k)).second;

        ASSERT_EQ(inserted, expected) << "insert " << i << " key " << k;

        if (i % 2000 == 0) {
            ASSERT_EQ(TreeChecker(&bpm, tree).Check(),
                      static_cast<std::int64_t>(truth.size())) << "after insert " << i;
        }
    }

    ASSERT_EQ(TreeChecker(&bpm, tree).Check(), static_cast<std::int64_t>(truth.size()));
    EXPECT_GE(tree.Height(), 6);

    for (const auto& [k, v] : truth) {
        ASSERT_EQ(tree.Find(k), v) << "key " << k;
    }

    for (int i = 0; i < 20000; ++i) {
        KeyType q = key_dist(rng);
        std::optional<ValueType> expected;

        if (truth.count(q)) {
            expected = truth[q];
        }

        ASSERT_EQ(tree.Find(q), expected) << "query " << q;
    }
}


// Real page-sized nodes, three insert orders. 100k keys -> height 3.
TEST_F(BPlusTreeInsertTest, AscendingDescendingAndRandomOrders) {
    const int kN = 100000;

    std::vector<KeyType> ascending(kN);
    std::iota(ascending.begin(), ascending.end(), 0);

    std::vector<KeyType> descending(ascending.rbegin(), ascending.rend());

    std::vector<KeyType> shuffled = ascending;
    std::shuffle(shuffled.begin(), shuffled.end(), std::mt19937(7));

    for (const auto* order : {&ascending, &descending, &shuffled}) {
        std::remove(path_.c_str());

        DiskManager dm(path_);
        BufferPoolManager bpm(256, &dm);
        BPlusTree tree(&bpm);

        for (KeyType k : *order) {
            ASSERT_TRUE(tree.Insert(k, ValueFor(k)));
        }

        ASSERT_EQ(TreeChecker(&bpm, tree).Check(), kN);
        EXPECT_EQ(tree.Height(), 3);

        for (KeyType k = 0; k < kN; ++k) {
            ASSERT_EQ(tree.Find(k), ValueFor(k)) << "key " << k;
        }

        EXPECT_EQ(tree.Find(-1), std::nullopt);
        EXPECT_EQ(tree.Find(kN), std::nullopt);
    }
}


// Splits must never hold many pages pinned at once: a tiny pool works.
TEST_F(BPlusTreeInsertTest, InsertsWorkWithATinyPoolAndLeakNoPins) {
    DiskManager dm(path_);
    BufferPoolManager bpm(4, &dm);  // header + node + new sibling + spare
    BPlusTree tree(&bpm, kInvalidPageId, 3, 4);

    for (KeyType k = 0; k < 3000; ++k) {
        ASSERT_NO_THROW(tree.Insert((k * 7919) % 3001, 0)) << "insert " << k;
    }

    for (std::size_t i = 0; i < bpm.PoolSize(); ++i) {
        page_id_t pid;
        ASSERT_NO_THROW(bpm.NewPage(&pid)) << "frame " << i << " still pinned";
    }
}


TEST_F(BPlusTreeInsertTest, TreeAndSizeLimitsSurviveReopen) {
    page_id_t header_id;

    {
        DiskManager dm(path_);
        BufferPoolManager bpm(32, &dm);
        BPlusTree tree(&bpm, kInvalidPageId, 5, 6);

        for (KeyType k = 0; k < 1000; ++k) {
            tree.Insert(k, ValueFor(k));
        }
        header_id = tree.HeaderPageId();
    }

    DiskManager dm(path_);
    BufferPoolManager bpm(32, &dm);
    BPlusTree tree(&bpm, header_id);

    EXPECT_EQ(tree.LeafMaxSize(), 5);
    EXPECT_EQ(tree.InternalMaxSize(), 6);

    for (KeyType k = 1000; k < 2000; ++k) {   // keep inserting after reopen
        tree.Insert(k, ValueFor(k));
    }

    EXPECT_EQ(TreeChecker(&bpm, tree).Check(), 2000);
}


// Measured, not assumed: ascending inserts leave every leaf about half
// full (the left half of each split is never touched again). Random
// inserts fill leaves more; the textbook figure is ~69% (ln 2), this
// build measured 0.742 at 200k keys. The bounds below are deliberately loose.
TEST_F(BPlusTreeInsertTest, LeafFillAscendingVsRandom) {
    const int kN = 200000;

    std::vector<KeyType> keys(kN);
    std::iota(keys.begin(), keys.end(), 0);

    TreeStats ascending_stats;
    TreeStats random_stats;

    {
        DiskManager dm(path_);
        BufferPoolManager bpm(256, &dm);
        BPlusTree tree(&bpm);

        for (KeyType k : keys) {
            tree.Insert(k, 0);
        }
        ascending_stats = tree.Stats();
    }

    std::remove(path_.c_str());
    std::shuffle(keys.begin(), keys.end(), std::mt19937(11));

    {
        DiskManager dm(path_);
        BufferPoolManager bpm(256, &dm);
        BPlusTree tree(&bpm);

        for (KeyType k : keys) {
            tree.Insert(k, 0);
        }
        random_stats = tree.Stats();
    }

    EXPECT_EQ(ascending_stats.num_keys, kN);
    EXPECT_EQ(random_stats.num_keys, kN);

    EXPECT_NEAR(ascending_stats.avg_leaf_fill, 0.5, 0.02);
    EXPECT_GT(random_stats.avg_leaf_fill, 0.62);
    EXPECT_LT(random_stats.avg_leaf_fill, 0.76);

    std::printf("    [measured] ascending: fill %.3f, %lld leaves | random: fill %.3f, %lld leaves\n",
                ascending_stats.avg_leaf_fill,
                static_cast<long long>(ascending_stats.leaf_pages),
                random_stats.avg_leaf_fill,
                static_cast<long long>(random_stats.leaf_pages));
}

}  // namespace
}  // namespace index_engine
