#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "index_engine/config.h"

namespace index_engine {

using KeyType = std::int64_t;
using ValueType = std::int64_t;


enum class NodeType : std::int32_t {
    kInvalid = 0,  // a zeroed page reads as kInvalid
    kLeaf = 1,
    kInternal = 2,
};


// First 16 bytes of every tree node page.
struct NodeHeader {
    NodeType type;
    std::int32_t size;       // leaf: number of keys; internal: number of children
    page_id_t next_leaf;     // leaf only: right sibling, kInvalidPageId at the end
    std::int32_t reserved;   // padding, keeps the arrays 8-byte aligned
};

static_assert(sizeof(NodeHeader) == 16, "header must be 16 bytes");


// Leaf capacity: 16 + N * (8 + 8) <= 4096  ->  N = 255
constexpr int kLeafCapacity =
    static_cast<int>((kPageSize - sizeof(NodeHeader)) / (sizeof(KeyType) + sizeof(ValueType)));

// Internal capacity: 16 + N * 8 (keys) + N * 4 (child ids) <= 4096  ->  N = 340
constexpr int kInternalCapacity =
    static_cast<int>((kPageSize - sizeof(NodeHeader)) / (sizeof(KeyType) + sizeof(page_id_t)));


// Leaf node: sorted keys[0..size-1], values[i] belongs to keys[i].
// Keys and values are separate arrays so binary search only touches keys.
struct LeafNode {
    NodeHeader header;
    KeyType keys[kLeafCapacity];
    ValueType values[kLeafCapacity];

    void Init() {
        header.type = NodeType::kLeaf;
        header.size = 0;
        header.next_leaf = kInvalidPageId;
        header.reserved = 0;
    }

    int Size() const { return header.size; }

    // Index of the first key >= key (where key is, or would be inserted).
    int LowerBound(KeyType key) const {
        return static_cast<int>(std::lower_bound(keys, keys + header.size, key) - keys);
    }

    std::optional<ValueType> Lookup(KeyType key) const {
        int i = LowerBound(key);

        if (i < header.size && keys[i] == key) {
            return values[i];
        }
        return std::nullopt;
    }
};


// Internal node with `size` children and size-1 separator keys.
// keys[0] is unused. children[i] holds keys in [keys[i], keys[i+1]).
//
//   children:   c0     c1      c2      c3
//   keys:          k1      k2      k3
//   c0 < k1 <= c1 < k2 <= c2 < k3 <= c3
struct InternalNode {
    NodeHeader header;
    KeyType keys[kInternalCapacity];
    page_id_t children[kInternalCapacity];

    void Init() {
        header.type = NodeType::kInternal;
        header.size = 0;
        header.next_leaf = kInvalidPageId;
        header.reserved = 0;
    }

    int Size() const { return header.size; }

    // Which child to follow for `key`: the last i with keys[i] <= key
    // (0 if key is smaller than every separator). A key equal to a
    // separator goes RIGHT.
    int ChildIndex(KeyType key) const {
        const KeyType* first = keys + 1;
        const KeyType* last = keys + header.size;

        return static_cast<int>(std::upper_bound(first, last, key) - keys) - 1;
    }

    page_id_t ChildFor(KeyType key) const { return children[ChildIndex(key)]; }
};


// Header page (one per tree): remembers where the root is.
struct TreeHeaderPage {
    page_id_t root_page_id;
};


// The node structs are read straight out of raw 4KB page buffers, so they
// must fit in a page and have a plain C-like memory layout.
static_assert(sizeof(LeafNode) <= kPageSize, "leaf must fit in one page");
static_assert(sizeof(InternalNode) <= kPageSize, "internal node must fit in one page");
static_assert(std::is_standard_layout_v<LeafNode> && std::is_trivially_copyable_v<LeafNode>);
static_assert(std::is_standard_layout_v<InternalNode> && std::is_trivially_copyable_v<InternalNode>);


// Peek at a node's type without knowing which struct it is.
inline NodeType GetNodeType(const char* page_data) {
    return reinterpret_cast<const NodeHeader*>(page_data)->type;
}

}  // namespace index_engine
