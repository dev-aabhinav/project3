#include "index_engine/disk_manager.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace index_engine {
namespace {

// Gives every test its own fresh file and deletes it afterwards.
class DiskManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = "disk_manager_test_" + std::to_string(::getpid()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name() +
                ".db";
        std::remove(path_.c_str());
    }

    void TearDown() override {
        std::remove(path_.c_str());
    }

    std::string path_;
};


// Fill a page with a recognisable pattern that depends on `seed`.
void FillPattern(char* buf, int seed) {
    for (std::size_t i = 0; i < kPageSize; ++i) {
        buf[i] = static_cast<char>((i * 31 + static_cast<std::size_t>(seed)) % 251);
    }
}


TEST_F(DiskManagerTest, NewFileHasNoPages) {
    DiskManager dm(path_);

    EXPECT_EQ(dm.NumPages(), 0);
}


TEST_F(DiskManagerTest, AllocateReturnsSequentialIds) {
    DiskManager dm(path_);

    EXPECT_EQ(dm.AllocatePage(), 0);
    EXPECT_EQ(dm.AllocatePage(), 1);
    EXPECT_EQ(dm.AllocatePage(), 2);
    EXPECT_EQ(dm.NumPages(), 3);
}


TEST_F(DiskManagerTest, WriteThenReadRoundTrip) {
    DiskManager dm(path_);

    char in[kPageSize];
    char out[kPageSize];

    // Write different content to several pages, out of order.
    for (int p = 0; p < 4; ++p) {
        dm.AllocatePage();
    }

    for (int p : {2, 0, 3, 1}) {
        FillPattern(in, p);
        dm.WritePage(p, in);
    }

    // Every page must come back exactly as written.
    for (int p = 0; p < 4; ++p) {
        FillPattern(in, p);
        dm.ReadPage(p, out);
        EXPECT_EQ(std::memcmp(in, out, kPageSize), 0) << "page " << p;
    }

    EXPECT_EQ(dm.NumWrites(), 4u);
    EXPECT_EQ(dm.NumReads(), 4u);
}


TEST_F(DiskManagerTest, UnwrittenPageReadsAsZeros) {
    DiskManager dm(path_);

    page_id_t p = dm.AllocatePage();

    char out[kPageSize];
    std::memset(out, 0x7F, kPageSize);  // garbage that must be overwritten

    dm.ReadPage(p, out);

    for (std::size_t i = 0; i < kPageSize; ++i) {
        ASSERT_EQ(out[i], 0) << "byte " << i;
    }
}


TEST_F(DiskManagerTest, DataSurvivesReopen) {
    char in[kPageSize];
    FillPattern(in, 42);

    {
        DiskManager dm(path_);
        dm.AllocatePage();
        page_id_t p = dm.AllocatePage();
        dm.WritePage(0, in);
        dm.WritePage(p, in);
        dm.Sync();
    }  // destructor closes the file

    DiskManager reopened(path_);

    EXPECT_EQ(reopened.NumPages(), 2);

    char out[kPageSize];
    reopened.ReadPage(1, out);
    EXPECT_EQ(std::memcmp(in, out, kPageSize), 0);
}


TEST_F(DiskManagerTest, InvalidPageIdThrows) {
    DiskManager dm(path_);
    dm.AllocatePage();

    char buf[kPageSize] = {};

    EXPECT_THROW(dm.ReadPage(1, buf), std::out_of_range);
    EXPECT_THROW(dm.ReadPage(-1, buf), std::out_of_range);
    EXPECT_THROW(dm.WritePage(5, buf), std::out_of_range);
}

}  // namespace
}  // namespace index_engine
