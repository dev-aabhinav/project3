#include "index_engine/lru_k_replacer.h"

#include <stdexcept>
#include <string>

namespace index_engine {

LRUKReplacer::LRUKReplacer(std::size_t num_frames, std::size_t k)
    : k_(k), history_(num_frames), evictable_(num_frames, 0) {
    if (k_ == 0) {
        throw std::invalid_argument("LRU-K needs k >= 1");
    }
}


void LRUKReplacer::CheckFrameId(frame_id_t frame_id) const {
    if (frame_id < 0 || static_cast<std::size_t>(frame_id) >= history_.size()) {
        throw std::out_of_range("invalid frame id " + std::to_string(frame_id));
    }
}


LRUKReplacer::Entry LRUKReplacer::KeyOf(frame_id_t frame_id) const {
    // history_ holds at most K times, oldest first: front() is either the
    // earliest access (fewer than K) or exactly the K-th most recent one.
    const auto& h = history_[frame_id];
    return {h.empty() ? 0 : h.front(), frame_id};
}


void LRUKReplacer::InsertCandidate(frame_id_t frame_id) {
    (HasKAccesses(frame_id) ? at_least_k_ : fewer_than_k_).insert(KeyOf(frame_id));
}


void LRUKReplacer::EraseCandidate(frame_id_t frame_id) {
    (HasKAccesses(frame_id) ? at_least_k_ : fewer_than_k_).erase(KeyOf(frame_id));
}


void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
    CheckFrameId(frame_id);

    // The ordering key is about to change: take the frame out first.
    if (evictable_[frame_id]) {
        EraseCandidate(frame_id);
    }

    auto& h = history_[frame_id];
    h.push_back(++clock_);
    if (h.size() > k_) {
        h.pop_front();   // keep only the last K accesses
    }

    if (evictable_[frame_id]) {
        InsertCandidate(frame_id);
    }
}


void LRUKReplacer::Pin(frame_id_t frame_id) {
    CheckFrameId(frame_id);

    if (evictable_[frame_id]) {
        EraseCandidate(frame_id);
        evictable_[frame_id] = 0;
        --evictable_count_;
    }
}


void LRUKReplacer::Unpin(frame_id_t frame_id) {
    CheckFrameId(frame_id);

    if (!evictable_[frame_id]) {
        evictable_[frame_id] = 1;
        ++evictable_count_;
        InsertCandidate(frame_id);
    }
}


std::optional<frame_id_t> LRUKReplacer::Victim() {
    // Frames with fewer than K accesses go first ("infinite" K-distance).
    std::set<Entry>& from = !fewer_than_k_.empty() ? fewer_than_k_ : at_least_k_;

    if (from.empty()) {
        return std::nullopt;
    }

    frame_id_t victim = from.begin()->second;
    from.erase(from.begin());

    evictable_[victim] = 0;
    --evictable_count_;
    history_[victim].clear();   // the frame will hold a different page

    return victim;
}


void LRUKReplacer::Forget(frame_id_t frame_id) {
    Pin(frame_id);
    history_[frame_id].clear();
}

}  // namespace index_engine
