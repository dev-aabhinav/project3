#include "index_engine/buffer_pool_manager.h"

#include <cstring>

namespace index_engine {

BufferPoolManager::BufferPoolManager(std::size_t pool_size, DiskManager* disk_manager)
    : disk_manager_(disk_manager),
      memory_(pool_size * kPageSize),
      pages_(pool_size),
      replacer_(pool_size) {

    if (pool_size == 0) {
        throw std::invalid_argument("pool size must be > 0");
    }

    page_table_.reserve(pool_size);
    free_list_.reserve(pool_size);

    // Point each frame at its slice of the slab; all frames start free.
    // Pushed in reverse so frame 0 is handed out first (free_list_ is a stack).
    for (std::size_t f = 0; f < pool_size; ++f) {
        pages_[f].data_ = memory_.data() + f * kPageSize;
    }

    for (std::size_t f = pool_size; f-- > 0;) {
        free_list_.push_back(static_cast<frame_id_t>(f));
    }
}


BufferPoolManager::~BufferPoolManager() {
    // A destructor must not throw. If the final flush fails we can only
    // drop the error; callers who care should call FlushAllPages() first.
    try {
        FlushAllPages();
    } catch (...) {
    }
}


frame_id_t BufferPoolManager::GetFreeFrame() {
    // 1) A never-used frame: no eviction needed.
    if (!free_list_.empty()) {
        frame_id_t f = free_list_.back();
        free_list_.pop_back();
        return f;
    }

    // 2) Otherwise evict the least recently used UNPINNED frame.
    std::optional<frame_id_t> victim = replacer_.Victim();

    if (!victim) {
        throw BufferPoolFullError("buffer pool full: all frames are pinned");
    }

    frame_id_t f = *victim;
    Page& old = pages_[f];

    // Dirty victim: its changes exist only in memory, so save them first.
    if (old.is_dirty_) {
        disk_manager_->WritePage(old.page_id_, old.data_);
    }

    page_table_.erase(old.page_id_);

    old.page_id_ = kInvalidPageId;
    old.pin_count_ = 0;
    old.is_dirty_ = false;

    return f;
}


Page* BufferPoolManager::FetchPage(page_id_t page_id) {
    auto found = page_table_.find(page_id);

    // Hit: already in memory. One more user; not evictable while in use.
    if (found != page_table_.end()) {
        ++hits_;

        Page& page = pages_[found->second];
        ++page.pin_count_;
        replacer_.Pin(found->second);

        return &page;
    }

    // Miss: find a frame, then read the page from disk into it.
    ++misses_;

    frame_id_t f = GetFreeFrame();
    Page& page = pages_[f];

    try {
        disk_manager_->ReadPage(page_id, page.data_);
    } catch (...) {
        free_list_.push_back(f);  // bad page id: give the frame back
        throw;
    }

    page.page_id_ = page_id;
    page.pin_count_ = 1;
    page.is_dirty_ = false;

    page_table_[page_id] = f;

    return &page;
}


Page* BufferPoolManager::NewPage(page_id_t* page_id) {
    // Get the frame FIRST: if the pool is full we throw before allocating,
    // so no disk page id is wasted.
    frame_id_t f = GetFreeFrame();
    Page& page = pages_[f];

    *page_id = disk_manager_->AllocatePage();

    std::memset(page.data_, 0, kPageSize);

    page.page_id_ = *page_id;
    page.pin_count_ = 1;
    page.is_dirty_ = true;  // must reach disk even if the caller never writes

    page_table_[*page_id] = f;

    return &page;
}


bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    auto found = page_table_.find(page_id);

    if (found == page_table_.end()) {
        return false;
    }

    Page& page = pages_[found->second];

    if (page.pin_count_ <= 0) {
        return false;  // unpin without a matching fetch: caller bug
    }

    // OR, not assign: an earlier user's changes must not be forgotten
    // just because this user only read the page.
    page.is_dirty_ = page.is_dirty_ || is_dirty;

    --page.pin_count_;

    if (page.pin_count_ == 0) {
        replacer_.Unpin(found->second);  // now an eviction candidate
    }

    return true;
}


bool BufferPoolManager::FlushPage(page_id_t page_id) {
    auto found = page_table_.find(page_id);

    if (found == page_table_.end()) {
        return false;
    }

    Page& page = pages_[found->second];

    disk_manager_->WritePage(page.page_id_, page.data_);
    page.is_dirty_ = false;

    return true;
}


void BufferPoolManager::FlushAllPages() {
    for (auto& [pid, f] : page_table_) {
        Page& page = pages_[f];

        if (page.is_dirty_) {
            disk_manager_->WritePage(pid, page.data_);
            page.is_dirty_ = false;
        }
    }
}


bool BufferPoolManager::DeletePage(page_id_t page_id) {
    auto found = page_table_.find(page_id);

    if (found == page_table_.end()) {
        return true;  // not in memory: nothing to free
    }

    frame_id_t f = found->second;
    Page& page = pages_[f];

    if (page.pin_count_ > 0) {
        return false;  // still in use
    }

    replacer_.Pin(f);  // remove from eviction candidates
    page_table_.erase(found);

    page.page_id_ = kInvalidPageId;
    page.pin_count_ = 0;
    page.is_dirty_ = false;  // contents are being thrown away

    free_list_.push_back(f);

    return true;
}

}  // namespace index_engine
