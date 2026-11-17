#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "index_engine/b_plus_tree_page.h"
#include "index_engine/buffer_pool_manager.h"
#include "index_engine/config.h"
#include "index_engine/page_guard.h"

namespace index_engine {

// Shape of the tree, measured by walking every node.
struct TreeStats {
    int height = 0;
    std::int64_t num_keys = 0;
    std::int64_t leaf_pages = 0;
    std::int64_t internal_pages = 0;
    double avg_leaf_fill = 0.0;   // num_keys / (leaf_pages * leaf_max_size)
};


// Forward iterator over the leaf chain, in key order.
// Holds exactly one pin: the leaf it is currently on.
//
//   for (auto it = tree.Begin(lo); it.Valid() && it.Key() < hi; it.Next()) { ... }
class BPlusTreeIterator {
public:
    BPlusTreeIterator() = default;
    BPlusTreeIterator(BufferPoolManager* bpm, PageGuard leaf, int index);

    // False once we have walked past the last key of the last leaf.
    bool Valid() const { return guard_.IsValid(); }

    KeyType Key() const { return guard_.As<LeafNode>()->keys[index_]; }
    ValueType Value() const { return guard_.As<LeafNode>()->values[index_]; }

    void Next();

private:
    // If index_ is past the end of this leaf, hop along next_leaf until a
    // key is found or the chain ends.
    void SkipToValid();

    BufferPoolManager* bpm_ = nullptr;
    PageGuard guard_;
    int index_ = 0;
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

    // Removes key. Returns false if it was not there. Underfull nodes borrow
    // from a sibling or merge with it; the tree shrinks when the root is
    // left with a single child.
    bool Remove(KeyType key);


    // Iterator at the first key >= lo (invalid if there is none).
    BPlusTreeIterator Begin(KeyType lo) const;

    // Iterator at the smallest key.
    BPlusTreeIterator Begin() const;


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
    struct PathEntry {
        page_id_t page_id;   // an internal node on the way down
        int child_index;     // which child we followed from it
    };

    void SetRootPageId(page_id_t root);

    // Drops a page that is no longer part of the tree.
    void FreePage(page_id_t page_id);

    int LeafMinSize() const { return (leaf_max_size_ + 1) / 2; }          // ceil(max / 2)
    int InternalMinSize() const { return (internal_max_size_ + 1) / 2; }  // ceil(max / 2)

    // Fix the underfull child at `index` of `parent` by borrowing from a
    // sibling (returns false) or merging with one (returns true: the parent
    // lost a child and may now be underfull itself).
    bool FixLeafUnderflow(PageGuard& parent, int index);
    bool FixInternalUnderflow(PageGuard& parent, int index);

    // Removes children[index] and keys[index] (index >= 1) from a node.
    static void RemoveFromInternal(InternalNode* node, int index);

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
