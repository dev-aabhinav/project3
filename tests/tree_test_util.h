#pragma once

// Shared helpers for B+ tree tests: the value convention, an invariant
// checker that walks the whole tree, and small node readers for
// whiteboard-style shape checks.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "index_engine/b_plus_tree.h"
#include "index_engine/page_guard.h"

namespace index_engine {
namespace tree_test {

inline ValueType ValueFor(KeyType key) {
    return key * 10 + 7;
}


// ---------------------------------------------------------------
// Invariant checker: walks the whole tree and verifies every B+ tree rule.
// ---------------------------------------------------------------

class TreeChecker {
public:
    TreeChecker(BufferPoolManager* bpm, const BPlusTree& tree)
        : bpm_(bpm), tree_(tree) {}

    // Returns the total number of keys. Fails the test on any violation.
    std::int64_t Check() {
        page_id_t root = tree_.RootPageId();

        if (root == kInvalidPageId) {
            return 0;
        }

        leaf_depth_ = -1;
        leaves_in_order_.clear();
        keys_in_order_.clear();

        CheckNode(root, std::numeric_limits<KeyType>::min(),
                  std::numeric_limits<KeyType>::max(), 1, true);

        CheckLeafChain();

        return static_cast<std::int64_t>(keys_in_order_.size());
    }

private:
    // Every key in this subtree must be in [low, high).
    void CheckNode(page_id_t pid, KeyType low, KeyType high, int depth, bool is_root) {
        PageGuard guard = FetchPageGuard(bpm_, pid);
        NodeType type = GetNodeType(guard.Data());

        if (type == NodeType::kLeaf) {
            const LeafNode* leaf = guard.As<LeafNode>();
            int n = leaf->Size();

            // All leaves at the same depth (the tree is perfectly balanced).
            if (leaf_depth_ == -1) {
                leaf_depth_ = depth;
            }
            ASSERT_EQ(depth, leaf_depth_) << "leaf " << pid << " at wrong depth";

            ASSERT_LE(n, tree_.LeafMaxSize()) << "leaf " << pid << " overfull";
            ASSERT_GE(n, 1) << "empty leaf " << pid;

            // After inserts only, a non-root leaf is at least half full.
            if (!is_root) {
                ASSERT_GE(n, (tree_.LeafMaxSize() + 1) / 2) << "leaf " << pid << " underfull";
            }

            for (int i = 0; i < n; ++i) {
                ASSERT_GE(leaf->keys[i], low) << "leaf " << pid;
                ASSERT_LT(leaf->keys[i], high) << "leaf " << pid;

                if (i > 0) {
                    ASSERT_LT(leaf->keys[i - 1], leaf->keys[i]) << "leaf " << pid << " unsorted";
                }

                ASSERT_EQ(leaf->values[i], ValueFor(leaf->keys[i]));
                keys_in_order_.push_back(leaf->keys[i]);
            }

            leaves_in_order_.push_back(pid);
            return;
        }

        ASSERT_EQ(type, NodeType::kInternal) << "page " << pid << " is not a node";

        const InternalNode* node = guard.As<InternalNode>();
        int n = node->Size();

        ASSERT_LE(n, tree_.InternalMaxSize()) << "internal " << pid << " overfull";
        ASSERT_GE(n, 2) << "internal " << pid << " has fewer than 2 children";

        if (!is_root) {
            ASSERT_GE(n, (tree_.InternalMaxSize() + 1) / 2) << "internal " << pid << " underfull";
        }

        for (int i = 1; i < n; ++i) {
            ASSERT_GE(node->keys[i], low);
            ASSERT_LT(node->keys[i], high);

            if (i > 1) {
                ASSERT_LT(node->keys[i - 1], node->keys[i]) << "internal " << pid << " unsorted";
            }
        }

        // Copy the child list before recursing (recursion fetches other pages).
        std::vector<page_id_t> children(node->children, node->children + n);
        std::vector<KeyType> keys(node->keys, node->keys + n);
        guard.Release();

        for (int i = 0; i < n; ++i) {
            KeyType child_low = (i == 0) ? low : keys[i];
            KeyType child_high = (i == n - 1) ? high : keys[i + 1];

            CheckNode(children[i], child_low, child_high, depth + 1, false);
        }
    }

    // Following next_leaf from the leftmost leaf must visit every leaf
    // in the same left-to-right order as the tree walk, then stop.
    void CheckLeafChain() {
        page_id_t pid = leaves_in_order_.front();

        for (std::size_t i = 0; i < leaves_in_order_.size(); ++i) {
            ASSERT_EQ(pid, leaves_in_order_[i]) << "leaf chain broken at position " << i;

            PageGuard guard = FetchPageGuard(bpm_, pid);
            pid = guard.As<LeafNode>()->header.next_leaf;
        }

        ASSERT_EQ(pid, kInvalidPageId) << "last leaf must end the chain";
    }

    BufferPoolManager* bpm_;
    const BPlusTree& tree_;

    int leaf_depth_ = -1;
    std::vector<page_id_t> leaves_in_order_;
    std::vector<KeyType> keys_in_order_;
};


// Read one node's keys (for the whiteboard tests).
inline std::vector<KeyType> LeafKeys(BufferPoolManager* bpm, page_id_t pid) {
    PageGuard guard = FetchPageGuard(bpm, pid);
    const LeafNode* leaf = guard.As<LeafNode>();
    return std::vector<KeyType>(leaf->keys, leaf->keys + leaf->Size());
}

inline std::vector<KeyType> SeparatorKeys(BufferPoolManager* bpm, page_id_t pid) {
    PageGuard guard = FetchPageGuard(bpm, pid);
    const InternalNode* node = guard.As<InternalNode>();
    return std::vector<KeyType>(node->keys + 1, node->keys + node->Size());
}

inline page_id_t ChildAt(BufferPoolManager* bpm, page_id_t pid, int i) {
    PageGuard guard = FetchPageGuard(bpm, pid);
    return guard.As<InternalNode>()->children[i];
}

}  // namespace tree_test
}  // namespace index_engine
