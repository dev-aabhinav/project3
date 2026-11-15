#pragma once

#include <optional>

#include "index_engine/b_plus_tree_page.h"
#include "index_engine/buffer_pool_manager.h"
#include "index_engine/config.h"

namespace index_engine {

// Disk-backed B+ tree mapping int64 keys to int64 values (unique keys).
//
// The tree's root page id lives in a header page, so the tree can be
// reopened later from just the header page id.
//
// Every node access goes through the buffer pool; each Find touches
// exactly Height() pages. Not thread-safe.
class BPlusTree {
public:
    // header_page_id == kInvalidPageId: create a new, empty tree
    // (allocates a header page). Otherwise: open an existing tree.
    explicit BPlusTree(BufferPoolManager* bpm,
                       page_id_t header_page_id = kInvalidPageId);


    // Point lookup. nullopt if the key is not in the tree.
    std::optional<ValueType> Find(KeyType key) const;

    bool IsEmpty() const;

    // Number of levels: 0 = empty tree, 1 = root is a leaf.
    int Height() const;


    page_id_t HeaderPageId() const { return header_page_id_; }

    page_id_t RootPageId() const;

private:
    BufferPoolManager* bpm_;
    page_id_t header_page_id_;
};

}  // namespace index_engine
