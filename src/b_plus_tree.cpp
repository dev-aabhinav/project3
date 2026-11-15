#include "index_engine/b_plus_tree.h"

#include "index_engine/page_guard.h"

namespace index_engine {

BPlusTree::BPlusTree(BufferPoolManager* bpm, page_id_t header_page_id)
    : bpm_(bpm), header_page_id_(header_page_id) {

    if (header_page_id_ == kInvalidPageId) {
        // New tree: a header page that says "no root yet".
        PageGuard header = NewPageGuard(bpm_, &header_page_id_);
        header.As<TreeHeaderPage>()->root_page_id = kInvalidPageId;
        header.MarkDirty();
    }
}


page_id_t BPlusTree::RootPageId() const {
    PageGuard header = FetchPageGuard(bpm_, header_page_id_);
    return header.As<TreeHeaderPage>()->root_page_id;
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

}  // namespace index_engine
