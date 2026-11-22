#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "index_engine/config.h"
#include "index_engine/replacer.h"

namespace index_engine {

// LRU-K replacement (O'Neil, O'Neil & Weikum, SIGMOD 1993).
//
// Evicts the unpinned frame whose K-th most recent access is oldest.
// A frame with fewer than K accesses has "infinite" backward K-distance and
// is evicted first (oldest first access wins among those). With K = 2, pages
// touched once by a scan go before any page that has been used twice, so a
// big scan cannot flush the hot working set (sequential flooding).
//
// Eviction candidates are kept in two ordered sets, so every operation is
// O(log n): exact LRU-K needs ordering because an access moves a frame to
// an arbitrary position, not to one end. (2Q is the classic O(1) approximation.)
class LRUKReplacer : public Replacer {
public:
    LRUKReplacer(std::size_t num_frames, std::size_t k = 2);

    void RecordAccess(frame_id_t frame_id) override;
    void Pin(frame_id_t frame_id) override;
    void Unpin(frame_id_t frame_id) override;
    std::optional<frame_id_t> Victim() override;
    void Forget(frame_id_t frame_id) override;

    std::size_t Size() const override { return evictable_count_; }

private:
    using Entry = std::pair<std::uint64_t, frame_id_t>;   // (ordering time, frame)

    void CheckFrameId(frame_id_t frame_id) const;

    // Ordering key: earliest access if fewer than K accesses, else the K-th
    // most recent access.
    Entry KeyOf(frame_id_t frame_id) const;

    bool HasKAccesses(frame_id_t frame_id) const { return history_[frame_id].size() >= k_; }

    void InsertCandidate(frame_id_t frame_id);
    void EraseCandidate(frame_id_t frame_id);

    std::size_t k_;
    std::uint64_t clock_ = 0;   // logical time, +1 per access

    std::vector<std::deque<std::uint64_t>> history_;   // last <= K access times, oldest first
    std::vector<std::uint8_t> evictable_;

    std::set<Entry> fewer_than_k_;   // "infinite distance": evicted first
    std::set<Entry> at_least_k_;     // ordered by K-th most recent access

    std::size_t evictable_count_ = 0;
};

}  // namespace index_engine
