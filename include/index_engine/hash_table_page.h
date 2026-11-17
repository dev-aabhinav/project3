#pragma once

#include <cstdint>
#include <type_traits>

#include "index_engine/b_plus_tree_page.h"   // KeyType, ValueType
#include "index_engine/config.h"

namespace index_engine {

// 64-bit mixing hash (splitmix64 finalizer). Every input bit affects every
// output bit, so the LOW bits we use are well spread even for patterned keys.
// (std::hash<int64_t> in libstdc++ is the identity function: keys 0, 1024,
// 2048, ... would all share the same low 10 bits.)
inline std::uint64_t HashKey(KeyType key) {
    std::uint64_t x = static_cast<std::uint64_t>(key);
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

// Only for whiteboard-style tests where the split pattern must be predictable.
inline std::uint64_t IdentityHash(KeyType key) {
    return static_cast<std::uint64_t>(key);
}

using HashFunction = std::uint64_t (*)(KeyType);


// Bucket: unsorted key/value pairs, all sharing the same low `local_depth`
// hash bits.  8 + 255 * 16 = 4088 <= 4096.
constexpr int kBucketCapacity = static_cast<int>(
    (kPageSize - 2 * sizeof(std::int32_t)) / (sizeof(KeyType) + sizeof(ValueType)));

struct HashBucketPage {
    std::int32_t local_depth;
    std::int32_t size;
    KeyType keys[kBucketCapacity];
    ValueType values[kBucketCapacity];

    void Init(int depth) {
        local_depth = depth;
        size = 0;
    }

    // Linear scan: buckets are unsorted (255 keys fit in a few cache lines).
    int IndexOf(KeyType key) const {
        for (int i = 0; i < size; ++i) {
            if (keys[i] == key) {
                return i;
            }
        }
        return -1;
    }
};


// Directory page: one slice of the directory array of bucket page ids.
constexpr int kDirEntriesPerPage = static_cast<int>(kPageSize / sizeof(page_id_t));   // 1024

struct HashDirectoryPage {
    page_id_t buckets[kDirEntriesPerPage];
};


// Meta page: global depth and where the directory pages are.
constexpr int kMaxDirPages = static_cast<int>(
    (kPageSize - 4 * sizeof(std::int32_t)) / sizeof(page_id_t));                       // 1020

// 2^19 entries = 512 directory pages fit; 2^20 would need 1024 > 1020.
constexpr int kMaxGlobalDepth = 19;

struct HashMetaPage {
    std::int32_t global_depth;
    std::int32_t bucket_max_size;
    std::int32_t num_dir_pages;
    std::int32_t reserved;
    page_id_t dir_pages[kMaxDirPages];
};


static_assert(kBucketCapacity == 255);
static_assert(kDirEntriesPerPage == 1024);
static_assert((1 << kMaxGlobalDepth) / kDirEntriesPerPage <= kMaxDirPages);
static_assert((1 << (kMaxGlobalDepth + 1)) / kDirEntriesPerPage > kMaxDirPages);

static_assert(sizeof(HashBucketPage) <= kPageSize);
static_assert(sizeof(HashDirectoryPage) == kPageSize);
static_assert(sizeof(HashMetaPage) <= kPageSize);
static_assert(std::is_standard_layout_v<HashBucketPage> && std::is_trivially_copyable_v<HashBucketPage>);
static_assert(std::is_standard_layout_v<HashMetaPage> && std::is_trivially_copyable_v<HashMetaPage>);

}  // namespace index_engine
