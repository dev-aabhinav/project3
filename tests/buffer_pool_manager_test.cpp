#include "index_engine/buffer_pool_manager.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

namespace index_engine {
namespace {

class BufferPoolManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = "bpm_test_" + std::to_string(::getpid()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name() +
                ".db";
        std::remove(path_.c_str());
    }

    void TearDown() override {
        std::remove(path_.c_str());
    }

    std::string path_;
};


// Write a small marker string at the start of a page.
void WriteMarker(Page* page, const std::string& text) {
    std::strcpy(page->GetData(), text.c_str());
}

std::string ReadMarker(Page* page) {
    return std::string(page->GetData());
}


TEST_F(BufferPoolManagerTest, NewPageIsPinnedDirtyAndZeroed) {
    DiskManager dm(path_);
    BufferPoolManager bpm(2, &dm);

    page_id_t pid;
    Page* page = bpm.NewPage(&pid);

    EXPECT_EQ(pid, 0);
    EXPECT_EQ(page->GetPageId(), 0);
    EXPECT_EQ(page->GetPinCount(), 1);
    EXPECT_TRUE(page->IsDirty());

    for (std::size_t i = 0; i < kPageSize; ++i) {
        ASSERT_EQ(page->GetData()[i], 0);
    }
}


TEST_F(BufferPoolManagerTest, HitDoesNotTouchDisk) {
    DiskManager dm(path_);
    BufferPoolManager bpm(2, &dm);

    page_id_t pid;
    bpm.NewPage(&pid);
    bpm.UnpinPage(pid, true);

    std::uint64_t reads_before = dm.NumReads();

    Page* page = bpm.FetchPage(pid);

    EXPECT_EQ(page->GetPinCount(), 1);
    EXPECT_EQ(dm.NumReads(), reads_before);  // served from memory
    EXPECT_EQ(bpm.Hits(), 1u);
    EXPECT_EQ(bpm.Misses(), 0u);
}


TEST_F(BufferPoolManagerTest, DirtyVictimIsWrittenBackAndReadAgain) {
    DiskManager dm(path_);
    BufferPoolManager bpm(2, &dm);  // only 2 frames

    // Create 3 pages: the third forces page 0 out of memory.
    for (int i = 0; i < 3; ++i) {
        page_id_t pid;
        Page* page = bpm.NewPage(&pid);
        WriteMarker(page, "page-" + std::to_string(pid));
        bpm.UnpinPage(pid, true);
    }

    EXPECT_GE(dm.NumWrites(), 1u);  // page 0 was written back on eviction

    // Page 0 must come back from disk with its data intact.
    Page* page = bpm.FetchPage(0);

    EXPECT_EQ(ReadMarker(page), "page-0");
    EXPECT_EQ(bpm.Misses(), 1u);

    bpm.UnpinPage(0, false);
}


TEST_F(BufferPoolManagerTest, CleanVictimIsNotWrittenBack) {
    DiskManager dm(path_);
    BufferPoolManager bpm(1, &dm);  // 1 frame: every new page evicts

    page_id_t p0;
    page_id_t p1;

    bpm.NewPage(&p0);
    bpm.UnpinPage(p0, true);
    bpm.NewPage(&p1);              // evicts p0 (dirty): 1 write
    bpm.UnpinPage(p1, true);
    bpm.FlushAllPages();           // writes p1: 2 writes

    bpm.FetchPage(p0);             // evicts p1 (clean now): no write
    bpm.UnpinPage(p0, false);      // read only
    bpm.FetchPage(p1);             // evicts p0 (clean): no write
    bpm.UnpinPage(p1, false);

    EXPECT_EQ(dm.NumWrites(), 2u);
}


TEST_F(BufferPoolManagerTest, PinnedPageIsNeverEvicted) {
    DiskManager dm(path_);
    BufferPoolManager bpm(2, &dm);

    page_id_t p0;
    page_id_t p1;
    page_id_t p2;

    Page* pinned = bpm.NewPage(&p0);    // keep pinned
    WriteMarker(pinned, "keep-me");

    bpm.NewPage(&p1);
    bpm.UnpinPage(p1, true);

    bpm.NewPage(&p2);                   // must evict p1, not p0
    bpm.UnpinPage(p2, true);

    EXPECT_EQ(ReadMarker(pinned), "keep-me");
    EXPECT_EQ(pinned->GetPageId(), p0);

    std::uint64_t reads_before = dm.NumReads();
    bpm.FetchPage(p0);                  // still in memory: a hit
    EXPECT_EQ(dm.NumReads(), reads_before);
}


// A page that was evictable and is fetched again (a hit) must leave the
// replacer. Otherwise it is still the LRU victim while someone is using it.
TEST_F(BufferPoolManagerTest, HitOnEvictablePageMakesItUnevictable) {
    DiskManager dm(path_);
    BufferPoolManager bpm(2, &dm);

    page_id_t p0;
    page_id_t p1;
    page_id_t p2;

    WriteMarker(bpm.NewPage(&p0), "p0");
    bpm.UnpinPage(p0, true);            // replacer: [p0]
    bpm.NewPage(&p1);
    bpm.UnpinPage(p1, true);            // replacer: [p0, p1], p0 is LRU

    Page* in_use = bpm.FetchPage(p0);   // hit: p0 pinned again

    bpm.NewPage(&p2);                   // must evict p1, NOT p0

    EXPECT_EQ(in_use->GetPageId(), p0);
    EXPECT_EQ(ReadMarker(in_use), "p0");
}


TEST_F(BufferPoolManagerTest, AllPinnedFailsFastWithoutLeakingPageIds) {
    DiskManager dm(path_);
    BufferPoolManager bpm(2, &dm);

    page_id_t p0;
    page_id_t p1;
    page_id_t p2;

    bpm.NewPage(&p0);  // pinned
    bpm.NewPage(&p1);  // pinned

    EXPECT_THROW(bpm.NewPage(&p2), BufferPoolFullError);
    EXPECT_EQ(dm.NumPages(), 2);  // no page id wasted by the failed call

    // After one unpin, there is room again.
    bpm.UnpinPage(p0, true);
    EXPECT_NO_THROW(bpm.NewPage(&p2));
}


TEST_F(BufferPoolManagerTest, FetchFailsFastWhenAllPinned) {
    DiskManager dm(path_);
    BufferPoolManager bpm(1, &dm);

    page_id_t p0;
    page_id_t p1;

    bpm.NewPage(&p0);
    bpm.UnpinPage(p0, true);
    bpm.NewPage(&p1);  // evicts p0; p1 stays pinned

    EXPECT_THROW(bpm.FetchPage(p0), BufferPoolFullError);
}


TEST_F(BufferPoolManagerTest, PinCountTracksMultipleUsers) {
    DiskManager dm(path_);
    BufferPoolManager bpm(1, &dm);

    page_id_t p0;
    page_id_t p1;

    bpm.NewPage(&p0);           // pin 1
    Page* page = bpm.FetchPage(p0);  // pin 2

    EXPECT_EQ(page->GetPinCount(), 2);

    bpm.UnpinPage(p0, false);   // pin 1: still in use
    EXPECT_THROW(bpm.NewPage(&p1), BufferPoolFullError);

    bpm.UnpinPage(p0, false);   // pin 0: evictable
    EXPECT_NO_THROW(bpm.NewPage(&p1));
}


TEST_F(BufferPoolManagerTest, DirtyFlagIsNotClearedByALaterCleanUnpin) {
    DiskManager dm(path_);
    BufferPoolManager bpm(1, &dm);

    page_id_t p0;
    Page* page = bpm.NewPage(&p0);
    bpm.UnpinPage(p0, true);
    bpm.FlushAllPages();                 // now clean on disk

    page = bpm.FetchPage(p0);            // user A
    bpm.FetchPage(p0);                   // user B
    WriteMarker(page, "changed-by-A");
    bpm.UnpinPage(p0, true);             // A modified it
    bpm.UnpinPage(p0, false);            // B only read it

    EXPECT_TRUE(page->IsDirty());        // A's change must not be forgotten

    page_id_t p1;
    bpm.NewPage(&p1);                    // evicts p0: must write it back
    bpm.UnpinPage(p1, true);

    page = bpm.FetchPage(p0);
    EXPECT_EQ(ReadMarker(page), "changed-by-A");
}


TEST_F(BufferPoolManagerTest, UnpinErrorsReturnFalse) {
    DiskManager dm(path_);
    BufferPoolManager bpm(2, &dm);

    page_id_t p0;
    bpm.NewPage(&p0);

    EXPECT_TRUE(bpm.UnpinPage(p0, false));
    EXPECT_FALSE(bpm.UnpinPage(p0, false));  // already at pin count 0
    EXPECT_FALSE(bpm.UnpinPage(99, false));  // not in memory
}


TEST_F(BufferPoolManagerTest, DeletePageOnlyWhenUnpinned) {
    DiskManager dm(path_);
    BufferPoolManager bpm(1, &dm);

    page_id_t p0;
    page_id_t p1;

    bpm.NewPage(&p0);
    EXPECT_FALSE(bpm.DeletePage(p0));   // pinned

    bpm.UnpinPage(p0, true);
    EXPECT_TRUE(bpm.DeletePage(p0));    // frame goes back to the free list

    std::uint64_t writes_before = dm.NumWrites();
    EXPECT_NO_THROW(bpm.NewPage(&p1));  // reuses the freed frame
    EXPECT_EQ(dm.NumWrites(), writes_before);  // deleted page not written back
}


TEST_F(BufferPoolManagerTest, DataSurvivesRestart) {
    page_id_t pid;

    {
        DiskManager dm(path_);
        BufferPoolManager bpm(4, &dm);

        Page* page = bpm.NewPage(&pid);
        WriteMarker(page, "persisted");
        bpm.UnpinPage(pid, true);
    }  // bpm destructor flushes; dm destructor closes the file

    DiskManager dm(path_);
    BufferPoolManager bpm(4, &dm);

    Page* page = bpm.FetchPage(pid);
    EXPECT_EQ(ReadMarker(page), "persisted");
}


// Stress test: 200 pages through an 8-frame pool, so pages are evicted
// and re-read constantly. A std::map holds what each page SHOULD contain.
TEST_F(BufferPoolManagerTest, ContentsCorrectUnderHeavyEviction) {
    const int kPages = 200;
    const int kOps = 50000;

    DiskManager dm(path_);
    BufferPoolManager bpm(8, &dm);

    std::map<page_id_t, std::string> expected;

    for (int i = 0; i < kPages; ++i) {
        page_id_t pid;
        Page* page = bpm.NewPage(&pid);
        expected[pid] = "v0-" + std::to_string(pid);
        WriteMarker(page, expected[pid]);
        bpm.UnpinPage(pid, true);
    }

    std::mt19937 rng(2024);
    std::uniform_int_distribution<int> page_dist(0, kPages - 1);
    std::uniform_int_distribution<int> write_dist(0, 2);  // 1 in 3 is a write

    for (int i = 0; i < kOps; ++i) {
        page_id_t pid = page_dist(rng);
        Page* page = bpm.FetchPage(pid);

        ASSERT_EQ(ReadMarker(page), expected[pid]) << "op " << i;

        bool write = (write_dist(rng) == 0);

        if (write) {
            expected[pid] = "v" + std::to_string(i) + "-" + std::to_string(pid);
            WriteMarker(page, expected[pid]);
        }

        ASSERT_TRUE(bpm.UnpinPage(pid, write));
    }

    EXPECT_GT(bpm.Misses(), 0u);  // proves eviction really happened
}

}  // namespace
}  // namespace index_engine
