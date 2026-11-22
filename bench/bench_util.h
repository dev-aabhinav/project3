#pragma once

// Helpers shared by the benchmarks: timing, median/spread, raw-run printing
// (BENCH_VERBOSE=1) and an OS page-cache residency check.

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace bench {

using Clock = std::chrono::steady_clock;


inline double Median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}


// (max - min) / median across runs.
inline double Spread(const std::vector<double>& v) {
    auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    return (*hi - *lo) / Median(v);
}


inline bool Verbose() {
    static const bool verbose = std::getenv("BENCH_VERBOSE") != nullptr;
    return verbose;
}


// With BENCH_VERBOSE=1, prints every run of one cell to stderr.
inline void PrintRuns(const char* label, std::int64_t param, const std::vector<double>& v) {
    if (!Verbose()) {
        return;
    }
    std::fprintf(stderr, "  [runs] %-8lld %-22s", static_cast<long long>(param), label);
    for (double t : v) {
        std::fprintf(stderr, " %9.3f ms", t * 1e3);
    }
    std::fprintf(stderr, "\n");
}


// Times one call of fn(), which returns a checksum.
template <typename Fn>
double TimeOnce(Fn&& fn, std::uint64_t* checksum) {
    auto t0 = Clock::now();
    std::uint64_t sum = fn();
    auto t1 = Clock::now();

    *checksum = sum;
    return std::chrono::duration<double>(t1 - t0).count();
}


// Fraction of the file's pages currently in the OS page cache (mincore).
inline double ResidentFraction(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    struct stat st {};
    ::fstat(fd, &st);

    std::size_t len = static_cast<std::size_t>(st.st_size);
    void* addr = ::mmap(nullptr, len, PROT_READ, MAP_SHARED, fd, 0);

    std::size_t os_page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    std::vector<unsigned char> vec((len + os_page - 1) / os_page);
    ::mincore(addr, len, vec.data());

    std::size_t resident = 0;
    for (unsigned char c : vec) {
        resident += (c & 1);
    }

    ::munmap(addr, len);
    ::close(fd);
    return static_cast<double>(resident) / static_cast<double>(vec.size());
}

}  // namespace bench
