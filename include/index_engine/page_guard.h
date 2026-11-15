#pragma once

#include <utility>

#include "index_engine/buffer_pool_manager.h"
#include "index_engine/config.h"
#include "index_engine/page.h"

namespace index_engine {

// RAII wrapper for one pinned page: unpins it automatically when the guard
// goes out of scope (normal return, early return or exception).
//
// Move-only: a copy would unpin the same page twice.
class PageGuard {
public:
    PageGuard() = default;

    PageGuard(BufferPoolManager* bpm, Page* page) : bpm_(bpm), page_(page) {}

    ~PageGuard() { Release(); }


    PageGuard(const PageGuard&) = delete;
    PageGuard& operator=(const PageGuard&) = delete;

    // Move: take over the other guard's page; the other guard becomes empty.
    PageGuard(PageGuard&& other) noexcept
        : bpm_(other.bpm_), page_(other.page_), dirty_(other.dirty_) {
        other.page_ = nullptr;
    }

    PageGuard& operator=(PageGuard&& other) noexcept {
        if (this != &other) {
            Release();  // unpin the page we held before taking the new one

            bpm_ = other.bpm_;
            page_ = other.page_;
            dirty_ = other.dirty_;

            other.page_ = nullptr;
        }
        return *this;
    }


    // Unpin now instead of waiting for the destructor. Safe to call twice.
    void Release() {
        if (page_ != nullptr) {
            bpm_->UnpinPage(page_->GetPageId(), dirty_);
            page_ = nullptr;
            dirty_ = false;
        }
    }

    // "I modified this page": it will be written back before eviction.
    void MarkDirty() { dirty_ = true; }


    bool IsValid() const { return page_ != nullptr; }

    page_id_t PageId() const { return page_->GetPageId(); }

    char* Data() { return page_->GetData(); }
    const char* Data() const { return page_->GetData(); }

    // View the page bytes as a node struct, e.g. guard.As<LeafNode>().
    template <typename T>
    T* As() {
        return reinterpret_cast<T*>(page_->GetData());
    }

    template <typename T>
    const T* As() const {
        return reinterpret_cast<const T*>(page_->GetData());
    }

private:
    BufferPoolManager* bpm_ = nullptr;
    Page* page_ = nullptr;
    bool dirty_ = false;
};


// Convenience: fetch / create a page and wrap it in a guard straight away.
inline PageGuard FetchPageGuard(BufferPoolManager* bpm, page_id_t page_id) {
    return PageGuard(bpm, bpm->FetchPage(page_id));
}

inline PageGuard NewPageGuard(BufferPoolManager* bpm, page_id_t* page_id) {
    return PageGuard(bpm, bpm->NewPage(page_id));
}

}  // namespace index_engine
