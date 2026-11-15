#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "index_engine/b_plus_tree_page.h"
#include "index_engine/buffer_pool_manager.h"
#include "index_engine/config.h"

namespace index_engine {

// Shape of the tree, measured by walking every node.
struct TreeStats {
    int height = 0;
    std::int64_t num_keys = 0;
    std::int64_t leaf_pages = 0;
    std::int64_t internal_pages = 0;
    double avg_leaf_fill = 0.0;   // num_keys / (leaf_pages * leaf_max_size)
};


// Disk-backed B+ tree mapping int64 keys to int64 values (unique keys).
//
// The root page id and node size limits live in a header page, so the
// tree can be reopened later from just the header page id.
//
// Every node access goes through the buffer pool; each Find touches
// exactly Height() pages plus the header. Not thread-safe.
class BPlusTree {
public:
    // header_page_id == kInvalidPageId: create a new, empty tree with the
    // given node size limits (defaults = what physically fits in a page).
    // Otherwise: open an existing tree; its stored limits are used and the
    // size arguments are ignored.
    explicit BPlusTree(BufferPoolManager* bpm,
                       page_id_t header_page_id = kInvalidPageId,
                       int leaf_max_size = kLeafCapacity,
                       int internal_max_size = kInternalCapacity);


    // Point lookup. nullopt if the key is not in the tree.
    std::optional<ValueType> Find(KeyType key) const;

    // Inserts key -> value. Returns false (and changes nothing) if the key
    // already exists. Splits full nodes on the way back up.
    bool Insert(KeyType key, ValueType value);


    bool IsEmpty() const;

    // Number of levels: 0 = empty tree, 1 = root is a leaf.
    int Height() const;

    // Walks the whole tree. O(number of pages): for tests and reports only.
    TreeStats Stats() const;


    page_id_t HeaderPageId() const { return header_page_id_; }
    page_id_t RootPageId() const;

    int LeafMaxSize() const { return leaf_max_size_; }
    int InternalMaxSize() const { return internal_max_size_; }

private:
    void SetRootPageId(page_id_t root);

    // After `left` split into left + right, tell the parent (the last
    // entry of `path`) that keys >= `separator` now live in `right`.
    // Recurses upward if the parent is full; makes a new root at the top.
    void InsertIntoParent(std::vector<page_id_t>& path, page_id_t left,
                          KeyType separator, page_id_t right);

    BufferPoolManager* bpm_;
    page_id_t header_page_id_;

    int leaf_max_size_;
    int internal_max_size_;
};

}  // namespace index_engine
