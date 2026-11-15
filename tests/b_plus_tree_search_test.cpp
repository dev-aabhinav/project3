#include "index_engine/b_plus_tree.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <random>
#include <set>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#include "index_engine/page_guard.h"

namespace index_engine {
namespace {

// ---------------------------------------------------------------
// Node-level tests (no buffer pool needed)
// ---------------------------------------------------------------

TEST(BPlusTreeNodeTest, CapacitiesMatchTheMath) {
    EXPECT_EQ(kLeafCapacity, 255);       // 16 + 255 * 16 = 4096
    EXPECT_EQ(kInternalCapacity, 340);   // 16 + 340 * 12 = 4096

    EXPECT_EQ(sizeof(LeafNode), 4096u);
    EXPECT_EQ(sizeof(InternalNode), 4096u);
}


TEST(BPlusTreeNodeTest, LeafLowerBoundAndLookup) {
    LeafNode leaf;
    leaf.Init();

    leaf.keys[0] = 10;  leaf.values[0] = 100;
    leaf.keys[1] = 20;  leaf.values[1] = 200;
    leaf.keys[2] = 30;  leaf.values[2] = 300;
    leaf.header.size = 3;

    EXPECT_EQ(leaf.LowerBound(5), 0);
    EXPECT_EQ(leaf.LowerBound(10), 0);
    EXPECT_EQ(leaf.LowerBound(15), 1);
    EXPECT_EQ(leaf.LowerBound(30), 2);
    EXPECT_EQ(leaf.LowerBound(35), 3);   // past the end

    EXPECT_EQ(leaf.Lookup(20), 200);
    EXPECT_EQ(leaf.Lookup(25), std::nullopt);
    EXPECT_EQ(leaf.Lookup(35), std::nullopt);
}


TEST(BPlusTreeNodeTest, InternalChildIndexBoundaries) {
    InternalNode node;
    node.Init();

    // children: c0 | 10 | c1 | 20 | c2 | 30 | c3
    node.keys[1] = 10;
    node.keys[2] = 20;
    node.keys[3] = 30;
    node.header.size = 4;

    EXPECT_EQ(node.ChildIndex(5), 0);    // below every separator
    EXPECT_EQ(node.ChildIndex(10), 1);   // equal to separator: go RIGHT
    EXPECT_EQ(node.ChildIndex(19), 1);
    EXPECT_EQ(node.ChildIndex(20), 2);
    EXPECT_EQ(node.ChildIndex(30), 3);
    EXPECT_EQ(node.ChildIndex(99), 3);
}


// ---------------------------------------------------------------
// Tree-level tests: trees are built by hand, bottom-up, until Insert
// exists (M6). Like building a segment tree from its leaves.
// ---------------------------------------------------------------

class BPlusTreeSearchTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = "bptree_search_" + std::to_string(::getpid()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name() +
                ".db";
        std::remove(path_.c_str());
    }

    void TearDown() override {
        std::remove(path_.c_str());
    }

    std::string path_;
};


// Value stored for each key, so lookups can be checked exactly.
ValueType ValueFor(KeyType key) {
    return key * 10 + 7;
}


// Builds a valid B+ tree from sorted unique keys and points the tree's
// header at the root. leaf_fill keys per leaf, fanout children per node.
void BuildTree(BufferPoolManager* bpm, const BPlusTree& tree,
               const std::vector<KeyType>& keys, int leaf_fill, int fanout) {
    // Each entry: (smallest key under this node, its page id)
    std::vector<std::pair<KeyType, page_id_t>> level;

    // Leaves, linked left to right.
    PageGuard prev;

    for (std::size_t start = 0; start < keys.size(); start += leaf_fill) {
        page_id_t pid;
        PageGuard guard = NewPageGuard(bpm, &pid);

        LeafNode* leaf = guard.As<LeafNode>();
        leaf->Init();

        std::size_t end = std::min(keys.size(), start + leaf_fill);

        for (std::size_t i = start; i < end; ++i) {
            leaf->keys[leaf->header.size] = keys[i];
            leaf->values[leaf->header.size] = ValueFor(keys[i]);
            ++leaf->header.size;
        }

        if (prev.IsValid()) {
            prev.As<LeafNode>()->header.next_leaf = pid;
            prev.MarkDirty();
        }

        guard.MarkDirty();
        level.push_back({keys[start], pid});
        prev = std::move(guard);
    }

    prev.Release();

    // Internal levels until a single root remains.
    while (level.size() > 1) {
        std::vector<std::pair<KeyType, page_id_t>> parents;

        for (std::size_t start = 0; start < level.size(); start += fanout) {
            page_id_t pid;
            PageGuard guard = NewPageGuard(bpm, &pid);

            InternalNode* node = guard.As<InternalNode>();
            node->Init();

            std::size_t end = std::min(level.size(), start + fanout);

            for (std::size_t i = start; i < end; ++i) {
                int slot = node->header.size;
                node->children[slot] = level[i].second;

                if (slot > 0) {
                    node->keys[slot] = level[i].first;  // smallest key of that child
                }

                ++node->header.size;
            }

            guard.MarkDirty();
            parents.push_back({level[start].first, pid});
        }

        level = std::move(parents);
    }

    PageGuard header = FetchPageGuard(bpm, tree.HeaderPageId());
    header.As<TreeHeaderPage>()->root_page_id = level[0].second;
    header.MarkDirty();
}


std::vector<KeyType> EvenKeys(int n) {
    std::vector<KeyType> keys(n);

    for (int i = 0; i < n; ++i) {
        keys[i] = 2 * i;  // 0, 2, 4, ... so every odd key is absent
    }
    return keys;
}


TEST_F(BPlusTreeSearchTest, EmptyTree) {
    DiskManager dm(path_);
    BufferPoolManager bpm(8, &dm);
    BPlusTree tree(&bpm);

    EXPECT_TRUE(tree.IsEmpty());
    EXPECT_EQ(tree.Height(), 0);
    EXPECT_EQ(tree.Find(42), std::nullopt);
}


TEST_F(BPlusTreeSearchTest, SingleLeafTree) {
    DiskManager dm(path_);
    BufferPoolManager bpm(8, &dm);
    BPlusTree tree(&bpm);

    BuildTree(&bpm, tree, EvenKeys(100), kLeafCapacity, kInternalCapacity);

    EXPECT_EQ(tree.Height(), 1);
    EXPECT_EQ(tree.Find(0), ValueFor(0));
    EXPECT_EQ(tree.Find(198), ValueFor(198));
    EXPECT_EQ(tree.Find(99), std::nullopt);
}


// 1000 keys, 10 per leaf, 10 children per node -> 100 leaves, 10 internal
// nodes, 1 root: height 3. Pool of 16 frames forces evictions mid-search.
TEST_F(BPlusTreeSearchTest, ThreeLevelTreeFindsEveryKey) {
    DiskManager dm(path_);
    BufferPoolManager bpm(16, &dm);
    BPlusTree tree(&bpm);

    BuildTree(&bpm, tree, EvenKeys(1000), 10, 10);

    EXPECT_EQ(tree.Height(), 3);

    for (KeyType k = 0; k < 2000; k += 2) {
        ASSERT_EQ(tree.Find(k), ValueFor(k)) << "key " << k;
    }

    for (KeyType k = 1; k < 2000; k += 2) {
        ASSERT_EQ(tree.Find(k), std::nullopt) << "key " << k;
    }

    EXPECT_EQ(tree.Find(-5), std::nullopt);    // below the smallest key
    EXPECT_EQ(tree.Find(5000), std::nullopt);  // above the largest key

    EXPECT_GT(bpm.Misses(), 0u);  // pages really were evicted and re-read
}


// Proves the cost model: one Find = 1 header page + Height() node pages.
TEST_F(BPlusTreeSearchTest, FindTouchesHeaderPlusOnePagePerLevel) {
    DiskManager dm(path_);
    BufferPoolManager bpm(64, &dm);
    BPlusTree tree(&bpm);

    BuildTree(&bpm, tree, EvenKeys(1000), 10, 10);

    std::uint64_t before = bpm.Hits() + bpm.Misses();
    tree.Find(777);
    std::uint64_t after = bpm.Hits() + bpm.Misses();

    EXPECT_EQ(after - before, 1u + 3u);
}


// After many searches every frame must be evictable again: no pin leaks.
TEST_F(BPlusTreeSearchTest, NoPinsLeakedAfterSearches) {
    DiskManager dm(path_);
    BufferPoolManager bpm(16, &dm);
    BPlusTree tree(&bpm);

    BuildTree(&bpm, tree, EvenKeys(1000), 10, 10);

    for (KeyType k = 0; k < 2000; ++k) {
        tree.Find(k);
    }

    // If any frame were still pinned, one of these would throw.
    for (std::size_t i = 0; i < bpm.PoolSize(); ++i) {
        page_id_t pid;
        ASSERT_NO_THROW(bpm.NewPage(&pid)) << "frame " << i;
    }
}


TEST_F(BPlusTreeSearchTest, TreeReopensFromHeaderPage) {
    page_id_t header_id;

    {
        DiskManager dm(path_);
        BufferPoolManager bpm(16, &dm);
        BPlusTree tree(&bpm);

        BuildTree(&bpm, tree, EvenKeys(1000), 10, 10);
        header_id = tree.HeaderPageId();
    }  // pool flushes everything to disk

    DiskManager dm(path_);
    BufferPoolManager bpm(16, &dm);
    BPlusTree tree(&bpm, header_id);

    EXPECT_EQ(tree.Height(), 3);
    EXPECT_EQ(tree.Find(1234), ValueFor(1234));
    EXPECT_EQ(tree.Find(1235), std::nullopt);
}


// Random keys with random gaps, odd node sizes, random queries,
// checked against std::set.
TEST_F(BPlusTreeSearchTest, RandomQueriesMatchStdSet) {
    DiskManager dm(path_);
    BufferPoolManager bpm(32, &dm);
    BPlusTree tree(&bpm);

    std::mt19937_64 rng(99);
    std::uniform_int_distribution<KeyType> key_dist(-1000000, 1000000);

    std::set<KeyType> truth;
    while (truth.size() < 5000) {
        truth.insert(key_dist(rng));
    }

    std::vector<KeyType> keys(truth.begin(), truth.end());
    BuildTree(&bpm, tree, keys, 7, 5);

    EXPECT_GE(tree.Height(), 4);

    for (int i = 0; i < 20000; ++i) {
        KeyType q = key_dist(rng);

        std::optional<ValueType> expected;
        if (truth.count(q)) {
            expected = ValueFor(q);
        }

        ASSERT_EQ(tree.Find(q), expected) << "query " << q;
    }

    for (KeyType k : keys) {
        ASSERT_EQ(tree.Find(k), ValueFor(k)) << "key " << k;
    }
}

}  // namespace
}  // namespace index_engine
