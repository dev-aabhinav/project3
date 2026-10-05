#include "index_engine/disk_manager.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace index_engine {

namespace {

[[noreturn]] void ThrowErrno(const std::string& what) {
    throw std::runtime_error(what + ": " + std::strerror(errno));
}

// Byte offset of a page. Computed in 64-bit off_t so that
// page_id * 4096 cannot overflow a 32-bit int for files above 2GB.
off_t PageOffset(page_id_t page_id) {
    return static_cast<off_t>(page_id) * static_cast<off_t>(kPageSize);
}

}  // namespace


DiskManager::DiskManager(const std::string& file_path)
    : file_path_(file_path) {

    fd_ = ::open(file_path_.c_str(), O_RDWR | O_CREAT, 0644);

    if (fd_ < 0) {
        ThrowErrno("open " + file_path_);
    }

    // Re-opening an existing file: recover the page count from its size.
    struct stat st {};

    if (::fstat(fd_, &st) != 0) {
        ::close(fd_);
        ThrowErrno("fstat " + file_path_);
    }

    num_pages_ = static_cast<page_id_t>(st.st_size / static_cast<off_t>(kPageSize));
}


DiskManager::~DiskManager() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}


void DiskManager::CheckPageId(page_id_t page_id) const {
    if (page_id < 0 || page_id >= num_pages_) {
        throw std::out_of_range("invalid page id " + std::to_string(page_id));
    }
}


void DiskManager::ReadPage(page_id_t page_id, char* out) {
    CheckPageId(page_id);

    std::size_t done = 0;

    // pread may return fewer bytes than asked (a "short read"), so loop.
    while (done < kPageSize) {
        ssize_t n = ::pread(fd_, out + done, kPageSize - done,
                            PageOffset(page_id) + static_cast<off_t>(done));

        if (n < 0) {
            if (errno == EINTR) {
                continue;  // interrupted by a signal: just retry
            }
            ThrowErrno("pread");
        }

        if (n == 0) {
            // End of file: page was allocated but never written.
            std::memset(out + done, 0, kPageSize - done);
            break;
        }

        done += static_cast<std::size_t>(n);
    }

    ++num_reads_;
}


void DiskManager::WritePage(page_id_t page_id, const char* data) {
    CheckPageId(page_id);

    std::size_t done = 0;

    while (done < kPageSize) {
        ssize_t n = ::pwrite(fd_, data + done, kPageSize - done,
                             PageOffset(page_id) + static_cast<off_t>(done));

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            ThrowErrno("pwrite");
        }

        done += static_cast<std::size_t>(n);
    }

    ++num_writes_;
}


page_id_t DiskManager::AllocatePage() {
    // The file grows lazily: the first WritePage to this id extends it.
    // Freed pages are never reused (no free list yet).
    return num_pages_++;
}


void DiskManager::Sync() {
    if (::fsync(fd_) != 0) {
        ThrowErrno("fsync");
    }
}

}  // namespace index_engine
