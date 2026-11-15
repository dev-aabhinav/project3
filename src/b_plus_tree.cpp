#include "index_engine/b_plus_tree.h"

#include "index_engine/page_guard.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace index_engine {

BPlusTree::BPlusTree(BufferPoolManager* bpm, page_id_t header_page_id,
                     int leaf_max_size, int internal_max_size)
    : bpm_(bpm),
      header_page_id_(header_page_id),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size) {

    if (header_page_id_ == kInvalidPageId) {
        // A split needs room for at least 2 keys / 3 children, and a node
        // can never hold more than physically fits in its page.
        if (leaf_max_size_ < 2 || leaf_max_size_ > kLeafCapacity ||
            internal_max_size_ < 3 || internal_max_size_ > kInternalCapacity) {
            throw std::invalid_argument("invalid B+ tree node size limits");
        }

        // New tree: a header page that says "no root yet".
        PageGuard header = NewPageGuard(bpm_, &header_page_id_);
        TreeHeaderPage* h = header.As<TreeHeaderPage>();

        h->root_page_id = kInvalidPageId;
        h->leaf_max_size = leaf_max_size_;
        h->internal_max_size = internal_max_size_;

        header.MarkDirty();
    } else {
        // Existing tree: use the limits it was created with.
        PageGuard header = FetchPageGuard(bpm_, header_page_id_);
        leaf_max_size_ = header.As<TreeHeaderPage>()->leaf_max_size;
        internal_max_size_ = header.As<TreeHeaderPage>()->internal_max_size;
    }
}


page_id_t BPlusTree::RootPageId() const {
    PageGuard header = FetchPageGuard(bpm_, header_page_id_);
    return header.As<TreeHeaderPage>()->root_page_id;
}


void BPlusTree::SetRootPageId(page_id_t root) {
    PageGuard header = FetchPageGuard(bpm_, header_page_id_);
    header.As<TreeHeaderPage>()->root_page_id = root;
    header.MarkDirty();
}


bool BPlusTree::IsEmpty() const {
    return RootPageId() == kInvalidPageId;
}


std::optional<ValueType> BPlusTree::Find(KeyType key) const {
    page_id_t root = RootPageId();

    if (root == kInvalidPageId) {
        return std::nullopt;
    }

    PageGuard guard = FetchPageGuard(bpm_, root);

    // Walk down: at each internal node, binary-search for the child.
    while (GetNodeType(guard.Data()) == NodeType::kInternal) {
        page_id_t child = guard.As<InternalNode>()->ChildFor(key);

        // The right side runs first: the child is pinned, THEN the
        // move-assignment unpins the parent. So 2 pages are pinned for a
        // moment (the same order latch crabbing needs later).
        guard = FetchPageGuard(bpm_, child);
    }

    return guard.As<LeafNode>()->Lookup(key);
}


// ---------------------------------------------------------------
// Insert
// ---------------------------------------------------------------

bool BPlusTree::Insert(KeyType key, ValueType value) {
    page_id_t root = RootPageId();

    // Empty tree: the first key gets a leaf, which is also the root.
    if (root == kInvalidPageId) {
        page_id_t leaf_pid;
        PageGuard guard = NewPageGuard(bpm_, &leaf_pid);

        LeafNode* leaf = guard.As<LeafNode>();
        leaf->Init();
        leaf->keys[0] = key;
        leaf->values[0] = value;
        leaf->header.size = 1;

        guard.MarkDirty();
        SetRootPageId(leaf_pid);
        return true;
    }

    // 1) Walk down to the leaf, remembering every internal node on the way.
    //    path.back() is always the parent of the node we are looking at.
    std::vector<page_id_t> path;
    PageGuard guard = FetchPageGuard(bpm_, root);

    while (GetNodeType(guard.Data()) == NodeType::kInternal) {
        path.push_back(guard.PageId());
        guard = FetchPageGuard(bpm_, guard.As<InternalNode>()->ChildFor(key));
    }

    LeafNode* leaf = guard.As<LeafNode>();
    int n = leaf->Size();
    int pos = leaf->LowerBound(key);

    // 2) Unique keys: refuse duplicates.
    if (pos < n && leaf->keys[pos] == key) {
        return false;
    }

    // 3) Room in the leaf: shift the tail right by one, drop the key in.
    if (n < leaf_max_size_) {
        std::copy_backward(leaf->keys + pos, leaf->keys + n, leaf->keys + n + 1);
        std::copy_backward(leaf->values + pos, leaf->values + n, leaf->values + n + 1);

        leaf->keys[pos] = key;
        leaf->values[pos] = value;
        ++leaf->header.size;

        guard.MarkDirty();
        return true;
    }

    // 4) Leaf full: SPLIT.
    //    Get the new page FIRST: if the pool is full this throws before we
    //    have changed anything, so the tree stays valid.
    page_id_t right_pid;
    PageGuard right_guard = NewPageGuard(bpm_, &right_pid);

    // All n + 1 entries in sorted order, including the new one.
    std::vector<KeyType> tmp_keys(leaf->keys, leaf->keys + n);
    std::vector<ValueType> tmp_values(leaf->values, leaf->values + n);
    tmp_keys.insert(tmp_keys.begin() + pos, key);
    tmp_values.insert(tmp_values.begin() + pos, value);

    int total = n + 1;
    int left_count = (total + 1) / 2;   // left gets the bigger half
    int right_count = total - left_count;

    LeafNode* right = right_guard.As<LeafNode>();
    right->Init();

    std::copy(tmp_keys.begin() + left_count, tmp_keys.end(), right->keys);
    std::copy(tmp_values.begin() + left_count, tmp_values.end(), right->values);
    right->header.size = right_count;

    std::copy(tmp_keys.begin(), tmp_keys.begin() + left_count, leaf->keys);
    std::copy(tmp_values.begin(), tmp_values.begin() + left_count, leaf->values);
    leaf->header.size = left_count;

    // Keep the leaf chain intact: left -> right -> (old right neighbour).
    right->header.next_leaf = leaf->header.next_leaf;
    leaf->header.next_leaf = right_pid;

    // COPY UP: the right leaf's first key goes to the parent as a separator,
    // and it also stays in the leaf (leaves must hold every key).
    KeyType separator = right->keys[0];
    page_id_t left_pid = guard.PageId();

    guard.MarkDirty();
    right_guard.MarkDirty();
    guard.Release();
    right_guard.Release();

    InsertIntoParent(path, left_pid, separator, right_pid);
    return true;
}


int BPlusTree::Height() const {
    page_id_t root = RootPageId();

    if (root == kInvalidPageId) {
        return 0;
    }

    int height = 1;
    PageGuard guard = FetchPageGuard(bpm_, root);

    // Every leaf is at the same depth, so following child 0 is enough.
    while (GetNodeType(guard.Data()) == NodeType::kInternal) {
        guard = FetchPageGuard(bpm_, guard.As<InternalNode>()->children[0]);
        ++height;
    }

    return height;
}

void BPlusTree::InsertIntoParent(std::vector<page_id_t>& path, page_id_t left,
                                 KeyType separator, page_id_t right) {
    // No parent: `left` was the root. Make a new root above both halves.
    // This is the ONLY place the tree grows taller (it grows at the top,
    // so every leaf stays at the same depth).
    if (path.empty()) {
        page_id_t root_pid;
        PageGuard root_guard = NewPageGuard(bpm_, &root_pid);

        InternalNode* root = root_guard.As<InternalNode>();
        root->Init();
        root->children[0] = left;
        root->children[1] = right;
        root->keys[1] = separator;
        root->header.size = 2;

        root_guard.MarkDirty();
        SetRootPageId(root_pid);
        return;
    }

    page_id_t parent_pid = path.back();
    path.pop_back();

    PageGuard guard = FetchPageGuard(bpm_, parent_pid);
    InternalNode* node = guard.As<InternalNode>();

    int n = node->Size();

    // `left` is the child whose range contained `separator`, so the new
    // child goes right after it.
    int pos = node->ChildIndex(separator) + 1;

    // Room in the parent: shift children[pos..] and keys[pos..] right.
    if (n < internal_max_size_) {
        std::copy_backward(node->children + pos, node->children + n, node->children + n + 1);
        std::copy_backward(node->keys + pos, node->keys + n, node->keys + n + 1);

        node->children[pos] = right;
        node->keys[pos] = separator;
        ++node->header.size;

        guard.MarkDirty();
        return;
    }

    // Parent full: SPLIT the internal node. New page first (see leaf split).
    page_id_t new_pid;
    PageGuard new_guard = NewPageGuard(bpm_, &new_pid);

    // n + 1 children and their separators, in order. tmp_keys[0] is unused,
    // exactly like keys[0] in the node.
    std::vector<page_id_t> tmp_children(node->children, node->children + n);
    std::vector<KeyType> tmp_keys(node->keys, node->keys + n);
    tmp_children.insert(tmp_children.begin() + pos, right);
    tmp_keys.insert(tmp_keys.begin() + pos, separator);

    int total = n + 1;
    int left_count = (total + 1) / 2;

    // PUSH UP: the key between the two halves moves to the parent and is
    // NOT kept in either half (it is only a signpost).
    KeyType push_up = tmp_keys[left_count];

    InternalNode* sibling = new_guard.As<InternalNode>();
    sibling->Init();

    for (int j = 0; j < total - left_count; ++j) {
        sibling->children[j] = tmp_children[left_count + j];
        sibling->keys[j] = (j == 0) ? 0 : tmp_keys[left_count + j];  // keys[0] unused
    }
    sibling->header.size = total - left_count;

    std::copy(tmp_children.begin(), tmp_children.begin() + left_count, node->children);
    std::copy(tmp_keys.begin(), tmp_keys.begin() + left_count, node->keys);
    node->header.size = left_count;

    guard.MarkDirty();
    new_guard.MarkDirty();
    guard.Release();
    new_guard.Release();

    // The split may ripple further up.
    InsertIntoParent(path, parent_pid, push_up, new_pid);
}


TreeStats BPlusTree::Stats() const {
    TreeStats stats;
    page_id_t root = RootPageId();

    if (root == kInvalidPageId) {
        return stats;
    }

    // Level-by-level walk (BFS) from the root.
    std::vector<page_id_t> level{root};

    while (!level.empty()) {
        ++stats.height;
        std::vector<page_id_t> next_level;

        for (page_id_t pid : level) {
            PageGuard guard = FetchPageGuard(bpm_, pid);

            if (GetNodeType(guard.Data()) == NodeType::kInternal) {
                const InternalNode* node = guard.As<InternalNode>();
                ++stats.internal_pages;
                next_level.insert(next_level.end(), node->children,
                                  node->children + node->Size());
            } else {
                ++stats.leaf_pages;
                stats.num_keys += guard.As<LeafNode>()->Size();
            }
        }

        level = std::move(next_level);
    }

    stats.avg_leaf_fill = static_cast<double>(stats.num_keys) /
                          (static_cast<double>(stats.leaf_pages) * leaf_max_size_);
    return stats;
}

}  // namespace index_engine
