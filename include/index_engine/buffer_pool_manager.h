#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "index_engine/config.h"
#include "index_engine/disk_manager.h"
#include "index_engine/lru_replacer.h"
#include "index_engine/page.h"

namespace index_engine {

// Thrown when a page must be brought into memory but every frame is pinned.
// We fail fast instead of waiting: in a single-threaded engine nobody else
// could ever unpin a frame, so waiting would hang forever.
class BufferPoolFullError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};


// Caches disk pages in a fixed number of in-memory frames.
//
//   page_table_ : page id -> frame id, for pages currently in memory
//   free_list_  : frames never used yet (always used before evicting)
//   replacer_   : unpinned frames in LRU order (eviction candidates)
//
// Every FetchPage / NewPage must be matched by exactly one UnpinPage.
// Not thread-safe.
class BufferPoolManager {
public:
    BufferPoolManager(std::size_t pool_size, DiskManager* disk_manager);

    // Writes back all dirty pages.
    ~BufferPoolManager();

    BufferPoolManager(const BufferPoolManager&) = delete;
    BufferPoolManager& operator=(const BufferPoolManager&) = delete;


    // Returns the page pinned (pin count + 1). Reads it from disk on a miss.
    // Throws BufferPoolFullError if it is not in memory and all frames are pinned.
    Page* FetchPage(page_id_t page_id);

    // Allocates a brand-new zeroed page, pinned, and stores its id in *page_id.
    // Throws BufferPoolFullError if all frames are pinned.
    Page* NewPage(page_id_t* page_id);

    // Caller is done with the page. is_dirty = "I modified it".
    // Returns false if the page is not in memory or was not pinned.
    bool UnpinPage(page_id_t page_id, bool is_dirty);

    // Writes the page to disk (if in memory) and marks it clean.
    bool FlushPage(page_id_t page_id);

    // Writes every dirty page in memory to disk.
    void FlushAllPages();

    // Drops the page from memory and frees its frame.
    // Returns false if the page is pinned (someone is still using it).
    // Note: the disk page id is not reused (DiskManager has no free list).
    bool DeletePage(page_id_t page_id);


    std::size_t PoolSize() const { return pages_.size(); }

    // Hit/miss counters, used later to measure buffer-pool hit rate.
    std::uint64_t Hits() const { return hits_; }
    std::uint64_t Misses() const { return misses_; }

private:
    // Returns a frame to load a new page into: from the free list,
    // otherwise an evicted victim (written back first if dirty).
    frame_id_t GetFreeFrame();

    DiskManager* disk_manager_;

    std::vector<char> memory_;   // pool_size * kPageSize bytes, one slab
    std::vector<Page> pages_;    // metadata, pages_[f] describes frame f

    std::unordered_map<page_id_t, frame_id_t> page_table_;
    std::vector<frame_id_t> free_list_;
    LRUReplacer replacer_;

    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
};

}  // namespace index_engine
