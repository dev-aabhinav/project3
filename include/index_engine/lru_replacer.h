#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "index_engine/config.h"

namespace index_engine {

// Chooses which buffer-pool frame to evict, in LRU order.
//
// It only ever holds UNPINNED frames (frames nobody is using right now),
// so Victim() just takes the tail: O(1), never skips over pinned frames.
//
// Storage: a doubly linked list kept in two pre-allocated arrays,
// prev_[f] and next_[f], indexed by frame id. Links are frame numbers,
// not pointers, so there is no heap allocation after construction.
//
//   head_ = most recently unpinned   ...   tail_ = least recent (victim)
class LRUReplacer {
public:
    explicit LRUReplacer(std::size_t num_frames);


    // Frame became evictable (its pin count dropped to 0).
    // Added as most recent. No-op if it is already in the replacer.
    void Unpin(frame_id_t frame_id);

    // Frame is in use again: remove it from the candidates.
    // No-op if it is not in the replacer.
    void Pin(frame_id_t frame_id);

    // Removes and returns the least recently unpinned frame,
    // or std::nullopt if every frame is pinned.
    std::optional<frame_id_t> Victim();


    // Number of evictable frames right now.
    std::size_t Size() const { return size_; }

private:
    void CheckFrameId(frame_id_t frame_id) const;

    void LinkAtHead(frame_id_t frame_id);
    void Unlink(frame_id_t frame_id);

    std::vector<frame_id_t> prev_;
    std::vector<frame_id_t> next_;

    // in_list_[f] == 1 if frame f is currently evictable.
    // (std::vector<bool> is avoided: it is a packed bitset, not a real array.)
    std::vector<std::uint8_t> in_list_;

    frame_id_t head_ = kInvalidFrameId;
    frame_id_t tail_ = kInvalidFrameId;

    std::size_t size_ = 0;
};

}  // namespace index_engine
