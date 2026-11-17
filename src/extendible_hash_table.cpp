#include "index_engine/extendible_hash_table.h"

#include <algorithm>
#include <unordered_set>

#include "index_engine/page_guard.h"

namespace index_engine {

ExtendibleHashTable::ExtendibleHashTable(BufferPoolManager* bpm, page_id_t meta_page_id,
                                         int bucket_max_size, HashFunction hash)
    : bpm_(bpm),
      meta_page_id_(meta_page_id),
      hash_(hash),
      bucket_max_size_(bucket_max_size) {

    if (meta_page_id_ != kInvalidPageId) {
        // Open an existing table: load the cached copy of the meta page.
        PageGuard meta = FetchPageGuard(bpm_, meta_page_id_);
        const HashMetaPage* m = meta.As<HashMetaPage>();

        global_depth_ = m->global_depth;
        bucket_max_size_ = m->bucket_max_size;
        dir_pages_.assign(m->dir_pages, m->dir_pages + m->num_dir_pages);
        return;
    }

    if (bucket_max_size_ < 1 || bucket_max_size_ > kBucketCapacity) {
        throw std::invalid_argument("invalid bucket size");
    }

    // New table: global depth 0, so the directory has 1 entry pointing to
    // 1 bucket with local depth 0 (every key goes there).
    { PageGuard meta = NewPageGuard(bpm_, &meta_page_id_); }

    page_id_t bucket_pid;
    PageGuard bucket = NewPageGuard(bpm_, &bucket_pid);
    bucket.As<HashBucketPage>()->Init(0);
    bucket.MarkDirty();

    page_id_t dir_pid;
    PageGuard dir = NewPageGuard(bpm_, &dir_pid);
    dir.As<HashDirectoryPage>()->buckets[0] = bucket_pid;
    dir.MarkDirty();

    dir_pages_.push_back(dir_pid);
    SaveMeta();
}


void ExtendibleHashTable::SaveMeta() {
    PageGuard meta = FetchPageGuard(bpm_, meta_page_id_);
    HashMetaPage* m = meta.As<HashMetaPage>();

    m->global_depth = global_depth_;
    m->bucket_max_size = bucket_max_size_;
    m->num_dir_pages = static_cast<std::int32_t>(dir_pages_.size());
    std::copy(dir_pages_.begin(), dir_pages_.end(), m->dir_pages);

    meta.MarkDirty();
}


std::int64_t ExtendibleHashTable::DirIndex(KeyType key) const {
    std::uint64_t mask = (std::uint64_t{1} << global_depth_) - 1;
    return static_cast<std::int64_t>(hash_(key) & mask);
}


page_id_t ExtendibleHashTable::BucketAt(std::int64_t index) const {
    PageGuard dir = FetchPageGuard(bpm_, dir_pages_[index / kDirEntriesPerPage]);
    return dir.As<HashDirectoryPage>()->buckets[index % kDirEntriesPerPage];
}


void ExtendibleHashTable::SetBucketAt(std::int64_t index, page_id_t bucket) {
    PageGuard dir = FetchPageGuard(bpm_, dir_pages_[index / kDirEntriesPerPage]);
    dir.As<HashDirectoryPage>()->buckets[index % kDirEntriesPerPage] = bucket;
    dir.MarkDirty();
}


// ---------------------------------------------------------------
// Lookup / remove
// ---------------------------------------------------------------

std::optional<ValueType> ExtendibleHashTable::Find(KeyType key) const {
    PageGuard bucket = FetchPageGuard(bpm_, BucketAt(DirIndex(key)));
    const HashBucketPage* b = bucket.As<HashBucketPage>();

    int i = b->IndexOf(key);

    if (i < 0) {
        return std::nullopt;
    }
    return b->values[i];
}


bool ExtendibleHashTable::Remove(KeyType key) {
    PageGuard bucket = FetchPageGuard(bpm_, BucketAt(DirIndex(key)));
    HashBucketPage* b = bucket.As<HashBucketPage>();

    int i = b->IndexOf(key);

    if (i < 0) {
        return false;
    }

    // Unsorted bucket: move the last entry into the hole.
    b->keys[i] = b->keys[b->size - 1];
    b->values[i] = b->values[b->size - 1];
    --b->size;

    bucket.MarkDirty();
    return true;
}


// ---------------------------------------------------------------
// Insert, split, doubling
// ---------------------------------------------------------------

bool ExtendibleHashTable::Insert(KeyType key, ValueType value) {
    // Each pass either inserts or splits the target bucket once; splitting
    // looks at one more hash bit, so the loop always ends (or throws at
    // kMaxGlobalDepth).
    while (true) {
        std::int64_t index = DirIndex(key);
        PageGuard bucket = FetchPageGuard(bpm_, BucketAt(index));
        HashBucketPage* b = bucket.As<HashBucketPage>();

        if (b->IndexOf(key) >= 0) {
            return false;  // unique keys
        }

        if (b->size < bucket_max_size_) {
            b->keys[b->size] = key;
            b->values[b->size] = value;
            ++b->size;

            bucket.MarkDirty();
            return true;
        }

        bucket.Release();
        SplitBucket(index);   // then retry: the key may now fit
    }
}


void ExtendibleHashTable::DoubleDirectory() {
    if (global_depth_ == kMaxGlobalDepth) {
        throw HashTableFullError("extendible hash: directory at maximum depth");
    }

    std::int64_t old_size = DirectorySize();

    if (old_size < kDirEntriesPerPage) {
        // Still fits in directory page 0: copy [0, old) to [old, 2 * old).
        PageGuard dir = FetchPageGuard(bpm_, dir_pages_[0]);
        page_id_t* entries = dir.As<HashDirectoryPage>()->buckets;

        std::copy(entries, entries + old_size, entries + old_size);
        dir.MarkDirty();
    } else {
        // Whole pages: new directory page k + old_pages is a copy of page k.
        std::size_t old_pages = dir_pages_.size();

        for (std::size_t k = 0; k < old_pages; ++k) {
            page_id_t new_pid;
            PageGuard copy = NewPageGuard(bpm_, &new_pid);
            PageGuard source = FetchPageGuard(bpm_, dir_pages_[k]);

            std::copy(source.Data(), source.Data() + kPageSize, copy.Data());
            copy.MarkDirty();

            dir_pages_.push_back(new_pid);
        }
    }

    // Both halves point to the same buckets: no key moved, the directory
    // can simply tell one more bit apart now.
    ++global_depth_;
    ++num_doublings_;
    SaveMeta();
}


void ExtendibleHashTable::SplitBucket(std::int64_t index) {
    page_id_t old_pid = BucketAt(index);
    PageGuard old_guard = FetchPageGuard(bpm_, old_pid);
    HashBucketPage* old_bucket = old_guard.As<HashBucketPage>();

    int d = old_bucket->local_depth;

    // The directory cannot tell the two halves apart yet: double it first.
    if (d == global_depth_) {
        DoubleDirectory();
    }

    page_id_t new_pid;
    PageGuard new_guard = NewPageGuard(bpm_, &new_pid);
    HashBucketPage* new_bucket = new_guard.As<HashBucketPage>();

    new_bucket->Init(d + 1);
    old_bucket->local_depth = d + 1;

    // Redistribute by hash bit d: 0 stays, 1 moves to the new bucket.
    int kept = 0;

    for (int i = 0; i < old_bucket->size; ++i) {
        KeyType k = old_bucket->keys[i];
        ValueType v = old_bucket->values[i];

        if ((hash_(k) >> d) & 1) {
            new_bucket->keys[new_bucket->size] = k;
            new_bucket->values[new_bucket->size] = v;
            ++new_bucket->size;
        } else {
            old_bucket->keys[kept] = k;
            old_bucket->values[kept] = v;
            ++kept;
        }
    }
    old_bucket->size = kept;

    old_guard.MarkDirty();
    new_guard.MarkDirty();
    old_guard.Release();
    new_guard.Release();

    // Every directory entry whose low d bits match pointed to the old bucket
    // (there are 2^(g-d) of them, spaced 2^d apart). Those with bit d = 1
    // now point to the new bucket.
    std::int64_t low_bits = index & ((std::int64_t{1} << d) - 1);
    std::int64_t step = std::int64_t{1} << d;

    for (std::int64_t i = low_bits; i < DirectorySize(); i += step) {
        if ((i >> d) & 1) {
            SetBucketAt(i, new_pid);
        }
    }

    ++num_splits_;
}


// ---------------------------------------------------------------
// Stats
// ---------------------------------------------------------------

HashTableStats ExtendibleHashTable::Stats() const {
    HashTableStats stats;
    stats.global_depth = global_depth_;
    stats.directory_size = DirectorySize();
    stats.directory_pages = static_cast<std::int64_t>(dir_pages_.size());
    stats.buckets_by_local_depth.assign(global_depth_ + 1, 0);

    std::unordered_set<page_id_t> seen;

    for (std::int64_t i = 0; i < DirectorySize(); ++i) {
        page_id_t pid = BucketAt(i);

        if (!seen.insert(pid).second) {
            continue;
        }

        PageGuard bucket = FetchPageGuard(bpm_, pid);
        const HashBucketPage* b = bucket.As<HashBucketPage>();

        ++stats.num_buckets;
        stats.num_keys += b->size;
        ++stats.buckets_by_local_depth[b->local_depth];
    }

    stats.avg_bucket_fill = static_cast<double>(stats.num_keys) /
                            (static_cast<double>(stats.num_buckets) * bucket_max_size_);
    return stats;
}

}  // namespace index_engine
