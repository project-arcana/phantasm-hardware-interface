#pragma once

#include <stdint.h>

#include <clean-core/alloc_array.hh>
#include <clean-core/utility.hh>

namespace phi
{
struct page_allocator
{
    void initialize(uint64_t num_elements, uint64_t num_elems_per_page, cc::allocator* static_alloc)
    {
        auto const num_pages = cc::int_div_ceil(num_elements, num_elems_per_page);
        _page_size = num_elems_per_page;
        _pages = cc::alloc_array<uint64_t>::filled(num_pages, 0, static_alloc);
    }

    // allocate a block of the given size, returns the resulting page index or -1u
    [[nodiscard]] uint64_t allocate(uint64_t size)
    {
        uint64_t const num_pages = cc::int_div_ceil(size, _page_size);

        uint64_t num_contiguous_free_pages = 0;
        for (uint64_t i = 0u; i < _pages.size(); ++i)
        {
            auto const page_val = _pages[i];
            if (page_val > 0)
            {
                // allocated block, skip forward
                i = i + (page_val - 1);
                num_contiguous_free_pages = 0;
            }
            else
            {
                // free block
                ++num_contiguous_free_pages;
                if (num_contiguous_free_pages == num_pages)
                {
                    // contiguous space sufficient, return start of page
                    auto const allocation_start = i - (num_pages - 1);
                    _pages[allocation_start] = num_pages;
                    return allocation_start;
                }
            }
        }

        // no block found
        return uint64_t(-1);
    }

    // free the given page
    void free(uint64_t page)
    {
        if (page == uint64_t(-1))
        {
            return;
        }

        CC_ASSERT(_pages[page] != 0 && "freed a page that was already free");
        _pages[page] = 0;
    }

    void free_all() { std::memset(_pages.data(), 0, _pages.size_bytes()); }

public:
    // returns amount of elements per page
    uint64_t get_page_size() const { return _page_size; }

    // returns amount of pages
    uint64_t get_num_pages() const { return uint64_t(_pages.size()); }

    // returns amount of elements in total
    uint64_t get_num_elements() const { return get_page_size() * get_num_pages(); }

    // returns the size of the given allocation in elements
    uint64_t get_allocation_size_in_elements(uint64_t page) const
    {
        if (page == uint64_t(-1))
        {
            return 0;
        }
        return _pages[page] * _page_size;
    }

    // returns the offset of the given allocation to the start in elements
    uint64_t get_allocation_start_in_elements(uint64_t page) const { return page * _page_size; }

    // returns the page of the given allocation
    uint64_t get_page_from_allocation_start(uint64_t allocation_start) const { return allocation_start / _page_size; }

private:
    // pages, each element is a natural number n
    // n > 0: this and the following n-1 pages are allocated
    // each page not allocated is free (free implies 0, but 0 does not imply free)
    cc::alloc_array<uint64_t> _pages;

    // amount of elements per page
    uint64_t _page_size;
};


} // namespace phi
