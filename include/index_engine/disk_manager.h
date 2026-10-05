#pragma once

#include <cstdint>
#include <string>

#include "index_engine/config.h"

namespace index_engine {

// Lowest layer of the engine: reads and writes whole 4KB pages
// to a single data file. It does no caching; that is the buffer pool's job.
//
// Not thread-safe (except that pread/pwrite themselves are safe).
class DiskManager {
public:
    // Opens the file, creating it if it does not exist.
    explicit DiskManager(const std::string& file_path);

    ~DiskManager();

    // Owns a file descriptor, so copying would double-close it.
    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;


    // Copies page `page_id` from disk into `out` (must hold kPageSize bytes).
    // A page that was allocated but never written reads back as all zeros.
    void ReadPage(page_id_t page_id, char* out);

    // Writes kPageSize bytes from `data` to page `page_id`.
    void WritePage(page_id_t page_id, const char* data);

    // Reserves a new page id at the end of the file and returns it.
    page_id_t AllocatePage();

    // Forces written pages to stable storage (fsync).
    void Sync();


    page_id_t NumPages() const { return num_pages_; }

    // I/O counters, used later by benchmarks to prove "cold" runs hit disk.
    std::uint64_t NumReads() const { return num_reads_; }
    std::uint64_t NumWrites() const { return num_writes_; }

private:
    void CheckPageId(page_id_t page_id) const;

    std::string file_path_;
    int fd_ = -1;

    page_id_t num_pages_ = 0;

    std::uint64_t num_reads_ = 0;
    std::uint64_t num_writes_ = 0;
};

}  // namespace index_engine
