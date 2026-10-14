#pragma once

#include "index_engine/config.h"

namespace index_engine {

// Metadata for one buffer-pool frame, plus a pointer to its 4KB of data.
//
// Callers (B+ tree, hash table) read and write the bytes via GetData().
// Only BufferPoolManager may change page_id_, pin_count_ and is_dirty_.
class Page {
public:
    char* GetData() { return data_; }
    const char* GetData() const { return data_; }

    page_id_t GetPageId() const { return page_id_; }
    int GetPinCount() const { return pin_count_; }
    bool IsDirty() const { return is_dirty_; }

private:
    friend class BufferPoolManager;

    char* data_ = nullptr;                 // points into the pool's memory slab
    page_id_t page_id_ = kInvalidPageId;   // which disk page is in this frame
    int pin_count_ = 0;                    // how many users are using it now
    bool is_dirty_ = false;                // modified since read from disk?
};

}  // namespace index_engine
