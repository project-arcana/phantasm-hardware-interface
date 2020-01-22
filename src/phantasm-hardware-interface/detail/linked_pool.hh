#pragma once

#include <cstdlib>

#include <clean-core/array.hh>
#include <clean-core/new.hh>
#include <clean-core/vector.hh>

namespace pr::backend::detail
{
/// Fixed-size object pool
/// Uses an in-place linked list in free nodes, for O(1) acquire, release and size overhead
template <class T, class IdxT = size_t>
struct linked_pool
{
    static_assert(sizeof(T) >= sizeof(T*), "linked_pool element type must be large enough to accomodate a pointer");
    using index_t = IdxT;

    linked_pool() = default;
    linked_pool(linked_pool const&) = delete;
    linked_pool(linked_pool&&) noexcept = delete;
    linked_pool& operator=(linked_pool const&) = delete;
    linked_pool& operator=(linked_pool&&) noexcept = delete;

    ~linked_pool()
    {
        if (_pool)
            std::free(_pool);
    }

    void initialize(size_t size)
    {
        if (size == 0)
            return;

        CC_ASSERT(size < index_t(-1) && "linked_pool size too large for index type");
        CC_ASSERT(_pool == nullptr && "re-initialized linked_pool");

        _pool_size = size;
        _pool = static_cast<T*>(std::malloc(sizeof(T) * _pool_size));

        // initialize linked list
        for (auto i = 0u; i < _pool_size - 1; ++i)
        {
            T* node_ptr = &_pool[i];
            new (cc::placement_new, node_ptr) T*(&_pool[i + 1]);
        }

        // initialize linked list tail
        {
            T* tail_ptr = &_pool[_pool_size - 1];
            new (cc::placement_new, tail_ptr) T*(nullptr);
        }

        _first_free_node = &_pool[0];
    }

    [[nodiscard]] index_t acquire()
    {
        CC_ASSERT(!is_full() && "linked_pool full");

        T* const acquired_node = _first_free_node;
        // read the in-place next pointer of this node
        _first_free_node = *reinterpret_cast<T**>(acquired_node);
        // call the constructor
        new (cc::placement_new, acquired_node) T();

        return static_cast<index_t>(acquired_node - _pool);
    }

    void release(index_t index)
    {
        T* const released_node = &_pool[static_cast<size_t>(index)];
        // call the destructor
        released_node->~T();
        // write the in-place next pointer of this node
        new (cc::placement_new, released_node) T*(_first_free_node);

        _first_free_node = released_node;
    }

    [[nodiscard]] T& get(index_t index) { return _pool[static_cast<size_t>(index)]; }
    [[nodiscard]] T const& get(index_t index) const { return _pool[static_cast<size_t>(index)]; }

    [[nodiscard]] bool is_full() const { return _first_free_node == nullptr; }

    /// pass a lambda that is called with a T& of each allocated node
    /// acquire and release CAN be called from within the lambda
    /// This operation is slow (n squared) and should not occur in normal operation
    template <class F>
    void iterate_allocated_nodes(F&& func)
    {
        auto const free_indices = get_free_node_indices();

        // NOTE: this could be sped up with a sort

        for (index_t i = 0u; i < _pool_size; ++i)
        {
            bool is_index_free = false;

            for (auto const free_i : free_indices)
            {
                if (free_i == i)
                {
                    is_index_free = true;
                    break;
                }
            }

            if (!is_index_free)
            {
                func(_pool[i], i);
            }
        }
    }

private:
    [[nodiscard]] cc::vector<index_t> get_free_node_indices() const
    {
        cc::vector<index_t> free_indices;
        free_indices.reserve(_pool_size);

        T* cursor = _first_free_node;
        while (cursor != nullptr)
        {
            free_indices.push_back(static_cast<index_t>(cursor - _pool));
            cursor = *reinterpret_cast<T**>(cursor);
        }

        return free_indices;
    }

    T* _first_free_node = nullptr;
    T* _pool = nullptr;
    size_t _pool_size = 0;
};

}
