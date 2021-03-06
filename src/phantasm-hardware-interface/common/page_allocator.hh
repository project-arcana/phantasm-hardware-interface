#pragma once

#include <clean-core/alloc_array.hh>
#include <clean-core/utility.hh>

namespace phi
{
struct page_allocator
{
    void initialize(unsigned num_elements, unsigned num_elems_per_page, cc::allocator* static_alloc)
    {
        auto const num_pages = cc::int_div_ceil(num_elements, num_elems_per_page);
        _page_size = static_cast<int>(num_elems_per_page);
        _pages = cc::alloc_array<int>::filled(num_pages, 0, static_alloc);
    }

    /// allocate a block of the given size, returns the resulting page or -1
    [[nodiscard]] int allocate(int size)
    {
        int const num_pages = cc::int_div_ceil(size, _page_size);

        int num_contiguous_free_pages = 0;
        for (auto i = 0u; i < _pages.size(); ++i)
        {
            auto const page_val = _pages[i];
            if (page_val > 0)
            {
                // allocated block, skip forward
                i = unsigned(int(i) + (page_val - 1));
                num_contiguous_free_pages = 0;
            }
            else
            {
                // free block
                ++num_contiguous_free_pages;
                if (num_contiguous_free_pages == num_pages)
                {
                    // contiguous space sufficient, return start of page
                    auto const allocation_start = int(i) - (num_pages - 1);
                    _pages[unsigned(allocation_start)] = num_pages;
                    return allocation_start;
                }
            }
        }

        // no block found
        return -1;
    }

    /// free the given page
    void free(int page)
    {
        if (page >= 0)
            _pages[unsigned(page)] = 0;
    }

    void free_all() { std::memset(_pages.data(), 0, _pages.size_bytes()); }

public:
    /// returns amount of elements per page
    int get_page_size() const { return _page_size; }

    /// returns amount of pages
    int get_num_pages() const { return int(_pages.size()); }

    /// returns amount of elements in total
    int get_num_elements() const { return get_page_size() * get_num_pages(); }

    /// NOTE: this is the size given to ::allocate, ceiled to _page_size
    int get_allocation_size_in_elements(int page) const { return _pages[unsigned(page)] * _page_size; }

private:
    // pages, each element is a natural number n
    // n > 0: this and the following n-1 pages are allocated
    // each page not allocated is free (free implies 0, but 0 does not imply free)
    cc::alloc_array<int> _pages;
    int _page_size; // the amount of elements per page
};


}
