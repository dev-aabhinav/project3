#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

#include "index_engine/buffer_pool_manager.h"
#include "index_engine/config.h"
#include "index_engine/hash_table_page.h"

namespace index_engine {

// Thrown when a bucket would need to split beyond kMaxGlobalDepth.
class HashTableFullError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};


struct HashTableStats {
    int global_depth = 0;
    std::int64_t directory_size = 0;     // 2^global_depth entries
    std::int64_t num_buckets = 0;        // distinct bucket pages
    std::int64_t num_keys = 0;
    std::int64_t directory_pages = 0;
    double avg_bucket_fill = 0.0;        // num_keys / (num_buckets * bucket_max_size)
    std::vector<std::int64_t> buckets_by_local_depth;   // index = local depth
};


// Disk-backed extendible hash table: int64 key -> int64 value, unique keys.
//
//   meta page       : global depth + ids of the directory pages
//   directory pages : 2^global_depth bucket page ids, 1024 per page
//   bucket pages    : up to bucket_max_size unsorted key/value pairs
//
// Lookup = hash, take the low global_depth bits, read 1 directory page and
// 1 bucket page. The meta page is cached in memory and written on change.
//
// Splits buckets and doubles the directory; never merges or shrinks.
// Not thread-safe.
class ExtendibleHashTable {
public:
    // meta_page_id == kInvalidPageId: create a new table with one bucket.
    // Otherwise open an existing one (its stored bucket size is used).
    // The SAME hash function must be used every time a table is opened.
    explicit ExtendibleHashTable(BufferPoolManager* bpm,
                                 page_id_t meta_page_id = kInvalidPageId,
                                 int bucket_max_size = kBucketCapacity,
                                 HashFunction hash = HashKey);


    std::optional<ValueType> Find(KeyType key) const;

    // false if the key already exists. Splits / doubles as needed.
    // Throws HashTableFullError past kMaxGlobalDepth.
    bool Insert(KeyType key, ValueType value);

    // false if the key was not there. Buckets never merge.
    bool Remove(KeyType key);


    int GlobalDepth() const { return global_depth_; }
    std::int64_t DirectorySize() const { return std::int64_t{1} << global_depth_; }
    page_id_t MetaPageId() const { return meta_page_id_; }

    // Walks every bucket. For tests and reports only.
    HashTableStats Stats() const;

    // Counters for the growth experiment (M12).
    std::int64_t NumSplits() const { return num_splits_; }
    std::int64_t NumDoublings() const { return num_doublings_; }

    // Bucket page id that directory entry `index` points to (tests).
    page_id_t BucketAt(std::int64_t index) const;

private:
    // Directory index for a key: its low global_depth hash bits.
    std::int64_t DirIndex(KeyType key) const;

    void SetBucketAt(std::int64_t index, page_id_t bucket);

    // Doubles the directory: entries [0, 2^g) are copied to [2^g, 2^(g+1)).
    void DoubleDirectory();

    // Splits the bucket that directory entry `index` points to.
    void SplitBucket(std::int64_t index);

    void SaveMeta();

    BufferPoolManager* bpm_;
    page_id_t meta_page_id_;
    HashFunction hash_;

    int bucket_max_size_;
    int global_depth_ = 0;
    std::vector<page_id_t> dir_pages_;   // cached copy of the meta page

    std::int64_t num_splits_ = 0;
    std::int64_t num_doublings_ = 0;
};

}  // namespace index_engine
