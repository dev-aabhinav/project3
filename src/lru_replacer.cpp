#include "index_engine/lru_replacer.h"

#include <stdexcept>
#include <string>

namespace index_engine {

LRUReplacer::LRUReplacer(std::size_t num_frames)
    : prev_(num_frames, kInvalidFrameId),
      next_(num_frames, kInvalidFrameId),
      in_list_(num_frames, 0) {}


void LRUReplacer::CheckFrameId(frame_id_t frame_id) const {
    if (frame_id < 0 || static_cast<std::size_t>(frame_id) >= prev_.size()) {
        throw std::out_of_range("invalid frame id " + std::to_string(frame_id));
    }
}


// Insert f at the head (most recent end). f must not be in the list.
void LRUReplacer::LinkAtHead(frame_id_t f) {
    prev_[f] = kInvalidFrameId;
    next_[f] = head_;

    if (head_ != kInvalidFrameId) {
        prev_[head_] = f;
    } else {
        tail_ = f;  // list was empty: f is also the tail
    }

    head_ = f;

    in_list_[f] = 1;
    ++size_;
}


// Remove f from wherever it is in the list. f must be in the list.
void LRUReplacer::Unlink(frame_id_t f) {
    frame_id_t p = prev_[f];
    frame_id_t n = next_[f];

    if (p != kInvalidFrameId) {
        next_[p] = n;   // previous node now skips over f
    } else {
        head_ = n;      // f was the head
    }

    if (n != kInvalidFrameId) {
        prev_[n] = p;   // next node now points back past f
    } else {
        tail_ = p;      // f was the tail
    }

    prev_[f] = kInvalidFrameId;
    next_[f] = kInvalidFrameId;

    in_list_[f] = 0;
    --size_;
}


void LRUReplacer::Unpin(frame_id_t frame_id) {
    CheckFrameId(frame_id);

    if (in_list_[frame_id]) {
        return;  // already evictable: keep its current position
    }

    LinkAtHead(frame_id);
}


void LRUReplacer::Pin(frame_id_t frame_id) {
    CheckFrameId(frame_id);

    if (!in_list_[frame_id]) {
        return;  // not evictable anyway: nothing to remove
    }

    Unlink(frame_id);
}


std::optional<frame_id_t> LRUReplacer::Victim() {
    if (tail_ == kInvalidFrameId) {
        return std::nullopt;  // every frame is pinned (or none unpinned yet)
    }

    frame_id_t victim = tail_;
    Unlink(victim);

    return victim;
}

}  // namespace index_engine
