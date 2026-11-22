#pragma once

#include <cstddef>
#include <optional>

#include "index_engine/config.h"

namespace index_engine {

// Page-replacement policy used by the buffer pool. It only ever holds
// UNPINNED frames as eviction candidates.
class Replacer {
public:
    virtual ~Replacer() = default;

    // The page in `frame_id` was just accessed (fetch hit, fetch miss or new
    // page). LRU ignores this (it orders by unpin time); LRU-K needs it.
    virtual void RecordAccess(frame_id_t frame_id) { (void)frame_id; }

    // Frame is in use: not an eviction candidate. No-op if absent.
    virtual void Pin(frame_id_t frame_id) = 0;

    // Frame's pin count reached 0: it becomes an eviction candidate.
    virtual void Unpin(frame_id_t frame_id) = 0;

    // Removes and returns the frame to evict, or nullopt if none is evictable.
    virtual std::optional<frame_id_t> Victim() = 0;

    // The frame's page was deleted: drop it and any access history.
    virtual void Forget(frame_id_t frame_id) { Pin(frame_id); }

    // Number of evictable frames.
    virtual std::size_t Size() const = 0;
};

}  // namespace index_engine
