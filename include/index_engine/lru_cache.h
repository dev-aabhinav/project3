#pragma once

#include <cstddef>
#include <list>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace index_engine {

// Textbook LRU cache: O(1) Get and Put.
//
//   items_  : doubly linked list of (key, value), front = most recent,
//             back = least recent (next to be evicted).
//   index_  : key -> iterator pointing at that key's node in items_.
//
// This is the interview version. The buffer pool does NOT use it,
// because every list node is a separate heap allocation (see M3).
template <typename K, typename V>
class LRUCache {
public:
    explicit LRUCache(std::size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("LRUCache capacity must be > 0");
        }

        // Size is bounded by capacity, so allocate buckets once:
        // no rehash (a sudden O(n) pause) while the cache fills up.
        index_.reserve(capacity_);
    }


    // Returns the value if present and marks the key most recently used.
    std::optional<V> Get(const K& key) {
        auto found = index_.find(key);

        if (found == index_.end()) {
            return std::nullopt;  // miss
        }

        // Hit: move this node to the front. O(1), no copy, no allocation.
        items_.splice(items_.begin(), items_, found->second);

        return found->second->second;
    }


    // Inserts or updates key. Evicts the least recently used key if full.
    void Put(const K& key, const V& value) {
        auto found = index_.find(key);

        if (found != index_.end()) {
            // Existing key: update value and mark most recent.
            found->second->second = value;
            items_.splice(items_.begin(), items_, found->second);
            return;
        }

        if (items_.size() == capacity_) {
            // Full: evict the back of the list (least recently used).
            const K& victim = items_.back().first;
            index_.erase(victim);
            items_.pop_back();
            ++evictions_;
        }

        items_.emplace_front(key, value);
        index_[key] = items_.begin();
    }


    std::size_t Size() const { return items_.size(); }
    std::size_t Capacity() const { return capacity_; }
    std::size_t Evictions() const { return evictions_; }

private:
    using Node = std::pair<K, V>;

    std::size_t capacity_;
    std::size_t evictions_ = 0;

    std::list<Node> items_;
    std::unordered_map<K, typename std::list<Node>::iterator> index_;
};

}  // namespace index_engine
