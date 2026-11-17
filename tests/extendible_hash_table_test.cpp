#include "index_engine/extendible_hash_table.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "index_engine/page_guard.h"

namespace index_engine {
namespace {

class ExtendibleHashTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = "ehash_test_" + std::to_string(::getpid()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name() +
                ".db";
        std::remove(path_.c_str());
    }

    void TearDown() override {
        std::remove(path_.c_str());
    }

    std::string path_;
};


ValueType ValueFor(KeyType key) {
    return key * 10 + 7;
}


struct BucketView {
    int local_depth;
    std::set<KeyType> keys;
};

BucketView ReadBucket(BufferPoolManager* bpm, page_id_t pid) {
    PageGuard guard = FetchPageGuard(bpm, pid);
    const HashBucketPage* b = guard.As<HashBucketPage>();
    return {b->local_depth, std::set<KeyType>(b->keys, b->keys + b->size)};
}


// Invariant checker: for every directory entry i pointing to a bucket of
// local depth d:
//   - d <= global depth
//   - every key in the bucket has the same low d hash bits as i
//   - exactly 2^(g - d) directory entries point to that bucket
std::int64_t CheckTable(BufferPoolManager* bpm, const ExtendibleHashTable& table,
                        HashFunction hash) {
    int g = table.GlobalDepth();
    std::unordered_map<page_id_t, std::int64_t> pointers;
    std::int64_t total_keys = 0;

    for (std::int64_t i = 0; i < table.DirectorySize(); ++i) {
        page_id_t pid = table.BucketAt(i);
        BucketView b = ReadBucket(bpm, pid);

        EXPECT_LE(b.local_depth, g) << "entry " << i;

        std::uint64_t mask = (std::uint64_t{1} << b.local_depth) - 1;

        for (KeyType k : b.keys) {
            EXPECT_EQ(hash(k) & mask, static_cast<std::uint64_t>(i) & mask)
                << "key " << k << " in the wrong bucket (entry " << i << ")";
        }

        if (++pointers[pid] == 1) {
            total_keys += static_cast<std::int64_t>(b.keys.size());
        }
    }

    for (const auto& [pid, count] : pointers) {
        int d = ReadBucket(bpm, pid).local_depth;
        EXPECT_EQ(count, std::int64_t{1} << (g - d)) << "bucket page " << pid;
    }

    return total_keys;
}


// ---------------------------------------------------------------
// Hash function
// ---------------------------------------------------------------

// Keys 0, 1024, 2048, ...: identity hash puts them all in low-bits bucket 0;
// HashKey spreads them over all 8 values of the low 3 bits.
TEST(HashFunctionTest, MixingHashSpreadsPatternedKeys) {
    std::map<std::uint64_t, int> identity_buckets;
    std::map<std::uint64_t, int> mixed_buckets;

    for (KeyType k = 0; k < 8000; ++k) {
        ++identity_buckets[IdentityHash(k * 1024) & 7];
        ++mixed_buckets[HashKey(k * 1024) & 7];
    }

    EXPECT_EQ(identity_buckets.size(), 1u);   // everything in bucket 0
    EXPECT_EQ(mixed_buckets.size(), 8u);

    for (const auto& [bucket, count] : mixed_buckets) {
        EXPECT_GT(count, 800) << "bucket " << bucket;   // ~1000 each
        EXPECT_LT(count, 1200) << "bucket " << bucket;
    }
}


// ---------------------------------------------------------------
// Basics
// ---------------------------------------------------------------

TEST_F(ExtendibleHashTest, EmptyTableHasOneBucketAndDepthZero) {
    DiskManager dm(path_);
    BufferPoolManager bpm(8, &dm);
    ExtendibleHashTable table(&bpm);

    EXPECT_EQ(table.GlobalDepth(), 0);
    EXPECT_EQ(table.DirectorySize(), 1);
    EXPECT_EQ(table.Find(1), std::nullopt);
    EXPECT_EQ(table.Stats().num_buckets, 1);
}


TEST_F(ExtendibleHashTest, InsertFindRemoveAndDuplicates) {
    DiskManager dm(path_);
    BufferPoolManager bpm(8, &dm);
    ExtendibleHashTable table(&bpm);

    EXPECT_TRUE(table.Insert(5, ValueFor(5)));
    EXPECT_FALSE(table.Insert(5, 999));        // duplicate
    EXPECT_EQ(table.Find(5), ValueFor(5));     // value unchanged

    EXPECT_TRUE(table.Remove(5));
    EXPECT_FALSE(table.Remove(5));
    EXPECT_EQ(table.Find(5), std::nullopt);
}


TEST_F(ExtendibleHashTest, InvalidBucketSizeThrows) {
    DiskManager dm(path_);
    BufferPoolManager bpm(8, &dm);

    EXPECT_THROW(ExtendibleHashTable(&bpm, kInvalidPageId, 0), std::invalid_argument);
    EXPECT_THROW(ExtendibleHashTable(&bpm, kInvalidPageId, kBucketCapacity + 1),
                 std::invalid_argument);
}


// ---------------------------------------------------------------
// Whiteboard example: identity hash (hash = key), bucket size 2.
//
//   insert 4 (100), 6 (110)     g=0  [B0: 4 6]
//   insert 5 (101): B0 full, d=0 == g=0 -> DOUBLE to g=1, split B0 on bit 0
//                               dir: 0->B0 [4 6] d=1, 1->B1 [5] d=1
//   insert 0 (000): B0 full, d=1 == g=1 -> DOUBLE to g=2, split B0 on bit 1
//                               dir: 00->B0 [4 0] d=2, 01->B1 [5] d=1,
//                                    10->B2 [6]   d=2, 11->B1 (shared)
//   insert 7 (111): entry 11 -> B1, fits: B1 [5 7]
//   insert 3 (011): B1 full, d=1 <  g=2 -> SPLIT WITHOUT DOUBLING on bit 1
//                               dir: 01->B1 [5] d=2, 11->B3 [7 3] d=2
// ---------------------------------------------------------------
TEST_F(ExtendibleHashTest, WhiteboardDoublingAndSplitWithoutDoubling) {
    DiskManager dm(path_);
    BufferPoolManager bpm(16, &dm);
    ExtendibleHashTable table(&bpm, kInvalidPageId, 2, IdentityHash);

    table.Insert(4, ValueFor(4));
    table.Insert(6, ValueFor(6));
    EXPECT_EQ(table.GlobalDepth(), 0);

    table.Insert(5, ValueFor(5));
    EXPECT_EQ(table.GlobalDepth(), 1);
    EXPECT_EQ(ReadBucket(&bpm, table.BucketAt(0)).keys, (std::set<KeyType>{4, 6}));
    EXPECT_EQ(ReadBucket(&bpm, table.BucketAt(1)).keys, (std::set<KeyType>{5}));

    table.Insert(0, ValueFor(0));
    EXPECT_EQ(table.GlobalDepth(), 2);
    EXPECT_EQ(ReadBucket(&bpm, table.BucketAt(0b00)).keys, (std::set<KeyType>{0, 4}));
    EXPECT_EQ(ReadBucket(&bpm, table.BucketAt(0b10)).keys, (std::set<KeyType>{6}));

    // Bucket B1 has local depth 1 < global depth 2: entries 01 and 11 share it.
    EXPECT_EQ(table.BucketAt(0b01), table.BucketAt(0b11));
    EXPECT_EQ(ReadBucket(&bpm, table.BucketAt(0b01)).local_depth, 1);

    table.Insert(7, ValueFor(7));
    EXPECT_EQ(ReadBucket(&bpm, table.BucketAt(0b11)).keys, (std::set<KeyType>{5, 7}));

    table.Insert(3, ValueFor(3));
    EXPECT_EQ(table.GlobalDepth(), 2);                 // NO doubling this time
    EXPECT_NE(table.BucketAt(0b01), table.BucketAt(0b11));
    EXPECT_EQ(ReadBucket(&bpm, table.BucketAt(0b01)).keys, (std::set<KeyType>{5}));
    EXPECT_EQ(ReadBucket(&bpm, table.BucketAt(0b11)).keys, (std::set<KeyType>{3, 7}));
    EXPECT_EQ(ReadBucket(&bpm, table.BucketAt(0b11)).local_depth, 2);

    EXPECT_EQ(table.NumDoublings(), 2);
    EXPECT_EQ(table.NumSplits(), 3);
    EXPECT_EQ(CheckTable(&bpm, table, IdentityHash), 6);
}


// ---------------------------------------------------------------
// Randomized and larger tests
// ---------------------------------------------------------------

TEST_F(ExtendibleHashTest, RandomInsertsAndRemovesMatchStdMap) {
    DiskManager dm(path_);
    BufferPoolManager bpm(256, &dm);
    ExtendibleHashTable table(&bpm, kInvalidPageId, 8);   // small buckets: many splits

    std::mt19937_64 rng(31);
    std::uniform_int_distribution<KeyType> key_dist(-1000000, 1000000);
    std::uniform_int_distribution<int> op(0, 3);   // 3/4 insert, 1/4 remove

    std::map<KeyType, ValueType> truth;

    for (int i = 0; i < 60000; ++i) {
        KeyType k = key_dist(rng);

        if (op(rng) != 0) {
            ASSERT_EQ(table.Insert(k, ValueFor(k)), truth.emplace(k, ValueFor(k)).second)
                << "insert " << i;
        } else {
            ASSERT_EQ(table.Remove(k), truth.erase(k) == 1) << "remove " << i;
        }
    }

    EXPECT_EQ(CheckTable(&bpm, table, HashKey), static_cast<std::int64_t>(truth.size()));

    for (const auto& [k, v] : truth) {
        ASSERT_EQ(table.Find(k), v) << "key " << k;
    }

    for (int i = 0; i < 20000; ++i) {
        KeyType q = key_dist(rng);
        std::optional<ValueType> expected;

        if (truth.count(q)) {
            expected = truth[q];
        }
        ASSERT_EQ(table.Find(q), expected) << "query " << q;
    }
}


// Real bucket size, enough keys to need more than one directory page
// (global depth > 10 means > 1024 entries).
TEST_F(ExtendibleHashTest, GrowsPastOneDirectoryPage) {
    DiskManager dm(path_);
    BufferPoolManager bpm(1024, &dm);
    ExtendibleHashTable table(&bpm);

    const int kN = 300000;

    for (KeyType k = 0; k < kN; ++k) {
        ASSERT_TRUE(table.Insert(k, ValueFor(k)));
    }

    HashTableStats s = table.Stats();

    EXPECT_EQ(s.num_keys, kN);
    EXPECT_GT(s.global_depth, 10);
    EXPECT_GT(s.directory_pages, 1);

    for (KeyType k = 0; k < kN; k += 7) {
        ASSERT_EQ(table.Find(k), ValueFor(k));
    }
    EXPECT_EQ(table.Find(kN), std::nullopt);

    EXPECT_EQ(CheckTable(&bpm, table, HashKey), kN);
}


// Patterned keys k * 1024 with the identity hash: all share their low 10
// bits, so the directory must grow past depth 10 before buckets can
// separate. With the mixing hash the directory stays small.
TEST_F(ExtendibleHashTest, IdentityHashBlowsUpDirectoryOnPatternedKeys) {
    const int kN = 2000;
    int identity_depth;
    int mixed_depth;

    {
        DiskManager dm(path_);
        BufferPoolManager bpm(256, &dm);
        ExtendibleHashTable table(&bpm, kInvalidPageId, kBucketCapacity, IdentityHash);

        for (KeyType k = 0; k < kN; ++k) {
            table.Insert(k * 1024, 0);
        }
        identity_depth = table.GlobalDepth();
    }

    std::remove(path_.c_str());

    {
        DiskManager dm(path_);
        BufferPoolManager bpm(256, &dm);
        ExtendibleHashTable table(&bpm);

        for (KeyType k = 0; k < kN; ++k) {
            table.Insert(k * 1024, 0);
        }
        mixed_depth = table.GlobalDepth();
    }

    EXPECT_GE(identity_depth, 13);   // 2^13 directory entries for ~8-16 buckets
    EXPECT_LE(mixed_depth, 5);

    std::printf("    [measured] %d keys k*1024: identity hash global depth %d, "
                "mixing hash global depth %d\n", kN, identity_depth, mixed_depth);
}


// Two keys whose hashes agree on the low 20 bits can never be separated
// within kMaxGlobalDepth = 19 bits: must throw, not loop forever.
TEST_F(ExtendibleHashTest, UnsplittableBucketThrowsAtMaxDepth) {
    DiskManager dm(path_);
    BufferPoolManager bpm(64, &dm);
    ExtendibleHashTable table(&bpm, kInvalidPageId, 1, IdentityHash);

    table.Insert(0, 0);

    EXPECT_THROW(table.Insert(KeyType{1} << 20, 0), HashTableFullError);
    EXPECT_EQ(table.GlobalDepth(), kMaxGlobalDepth);
    EXPECT_EQ(table.Find(0), 0);   // existing data still readable
}


TEST_F(ExtendibleHashTest, SurvivesReopen) {
    page_id_t meta;

    {
        DiskManager dm(path_);
        BufferPoolManager bpm(128, &dm);
        ExtendibleHashTable table(&bpm, kInvalidPageId, 16);

        for (KeyType k = 0; k < 5000; ++k) {
            table.Insert(k, ValueFor(k));
        }
        meta = table.MetaPageId();
    }

    DiskManager dm(path_);
    BufferPoolManager bpm(128, &dm);
    ExtendibleHashTable table(&bpm, meta);

    for (KeyType k = 0; k < 5000; ++k) {
        ASSERT_EQ(table.Find(k), ValueFor(k)) << "key " << k;
    }

    for (KeyType k = 5000; k < 10000; ++k) {   // keeps growing after reopen
        table.Insert(k, ValueFor(k));
    }
    EXPECT_EQ(CheckTable(&bpm, table, HashKey), 10000);
}


TEST_F(ExtendibleHashTest, TinyPoolAndNoPinLeaks) {
    DiskManager dm(path_);
    BufferPoolManager bpm(5, &dm);
    ExtendibleHashTable table(&bpm, kInvalidPageId, 4);

    for (KeyType k = 0; k < 20000; ++k) {
        ASSERT_NO_THROW(table.Insert(k, ValueFor(k))) << "insert " << k;
    }

    for (KeyType k = 0; k < 20000; k += 3) {
        table.Remove(k);
    }

    for (std::size_t i = 0; i < bpm.PoolSize(); ++i) {
        page_id_t pid;
        ASSERT_NO_THROW(bpm.NewPage(&pid)) << "frame " << i << " still pinned";
    }
}

}  // namespace
}  // namespace index_engine
