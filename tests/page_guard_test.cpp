#include "index_engine/page_guard.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>

namespace index_engine {
namespace {

class PageGuardTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = "guard_test_" + std::to_string(::getpid()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name() +
                ".db";
        std::remove(path_.c_str());
    }

    void TearDown() override {
        std::remove(path_.c_str());
    }

    std::string path_;
};


TEST_F(PageGuardTest, UnpinsWhenScopeEnds) {
    DiskManager dm(path_);
    BufferPoolManager bpm(1, &dm);  // 1 frame: a leaked pin blocks everything

    page_id_t p0;

    {
        PageGuard guard = NewPageGuard(&bpm, &p0);
        EXPECT_TRUE(guard.IsValid());
    }  // destructor unpins here

    page_id_t p1;
    EXPECT_NO_THROW(bpm.NewPage(&p1));  // frame was free to evict
}


TEST_F(PageGuardTest, UnpinsWhenAnExceptionIsThrown) {
    DiskManager dm(path_);
    BufferPoolManager bpm(1, &dm);

    try {
        page_id_t p0;
        PageGuard guard = NewPageGuard(&bpm, &p0);
        throw std::runtime_error("something failed mid-operation");
    } catch (const std::runtime_error&) {
    }

    page_id_t p1;
    EXPECT_NO_THROW(bpm.NewPage(&p1));
}


TEST_F(PageGuardTest, MoveTransfersOwnershipWithoutExtraUnpin) {
    DiskManager dm(path_);
    BufferPoolManager bpm(2, &dm);

    page_id_t p0;
    Page* raw = nullptr;

    {
        PageGuard a = NewPageGuard(&bpm, &p0);
        raw = bpm.FetchPage(p0);   // pin 2 (guard + this raw fetch)

        PageGuard b = std::move(a);

        EXPECT_FALSE(a.IsValid());
        EXPECT_TRUE(b.IsValid());
        EXPECT_EQ(raw->GetPinCount(), 2);  // moving did not unpin
    }  // only b unpins

    EXPECT_EQ(raw->GetPinCount(), 1);  // the raw fetch is still pinned
    bpm.UnpinPage(p0, false);
}


TEST_F(PageGuardTest, MoveAssignReleasesTheOldPage) {
    DiskManager dm(path_);
    BufferPoolManager bpm(2, &dm);

    page_id_t p0;
    page_id_t p1;

    PageGuard guard = NewPageGuard(&bpm, &p0);
    Page* first = bpm.FetchPage(p0);
    bpm.UnpinPage(p0, false);          // back to pin 1 (only the guard)

    guard = NewPageGuard(&bpm, &p1);   // must unpin p0

    EXPECT_EQ(first->GetPinCount(), 0);
    EXPECT_EQ(guard.PageId(), p1);
}


TEST_F(PageGuardTest, ReleaseTwiceUnpinsOnce) {
    DiskManager dm(path_);
    BufferPoolManager bpm(2, &dm);

    page_id_t p0;
    Page* raw = bpm.NewPage(&p0);      // pin 1

    PageGuard guard = FetchPageGuard(&bpm, p0);  // pin 2

    guard.Release();
    guard.Release();                   // no-op

    EXPECT_EQ(raw->GetPinCount(), 1);
    bpm.UnpinPage(p0, false);
}


TEST_F(PageGuardTest, MarkDirtyChangeSurvivesEviction) {
    DiskManager dm(path_);
    BufferPoolManager bpm(1, &dm);

    page_id_t p0;
    page_id_t p1;

    { PageGuard g = NewPageGuard(&bpm, &p0); }
    bpm.FlushAllPages();               // p0 is clean on disk now

    {
        PageGuard g = FetchPageGuard(&bpm, p0);
        std::strcpy(g.Data(), "saved");
        g.MarkDirty();
    }

    { PageGuard g = NewPageGuard(&bpm, &p1); }   // evicts p0

    PageGuard g = FetchPageGuard(&bpm, p0);
    EXPECT_STREQ(g.Data(), "saved");
}


// Documents WHY MarkDirty matters: without it the pool thinks the page is
// clean and drops the change on eviction.
TEST_F(PageGuardTest, ForgettingMarkDirtyLosesTheChange) {
    DiskManager dm(path_);
    BufferPoolManager bpm(1, &dm);

    page_id_t p0;
    page_id_t p1;

    { PageGuard g = NewPageGuard(&bpm, &p0); }
    bpm.FlushAllPages();

    {
        PageGuard g = FetchPageGuard(&bpm, p0);
        std::strcpy(g.Data(), "lost");  // modified, but not marked dirty
    }

    { PageGuard g = NewPageGuard(&bpm, &p1); }   // evicts p0, no write-back

    PageGuard g = FetchPageGuard(&bpm, p0);
    EXPECT_STREQ(g.Data(), "");        // disk still has the zeroed page
}

}  // namespace
}  // namespace index_engine
