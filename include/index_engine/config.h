#pragma once

#include <cstddef>
#include <cstdint>

namespace index_engine {

// Size of one page on disk and in memory. Every layer uses this unit.
constexpr std::size_t kPageSize = 4096;

// A page is identified by its index in the data file.
// Byte offset of page p in the file = p * kPageSize.
using page_id_t = std::int32_t;

constexpr page_id_t kInvalidPageId = -1;

// A frame is one page-sized slot in the buffer pool's memory.
// Frames are numbered 0 .. pool_size - 1 and never change.
// (page = block on disk, frame = slot in RAM that holds one page)
using frame_id_t = std::int32_t;

constexpr frame_id_t kInvalidFrameId = -1;

}  // namespace index_engine
