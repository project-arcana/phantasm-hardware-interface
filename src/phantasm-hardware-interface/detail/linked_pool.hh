#pragma once

#include <cstdint>
#include <cstdlib>

#include <clean-core/new.hh>
#include <clean-core/vector.hh>

#ifdef CC_ENABLE_ASSERTIONS
#define PHI_ENABLE_HANDLE_GEN_CHECK 1
#else
#define PHI_ENABLE_HANDLE_GEN_CHECK 0
#endif

#if PHI_ENABLE_HANDLE_GEN_CHECK
#include <clean-core/bit_cast.hh>
#endif

namespace phi::detail
{
/// Fixed-size object pool
/// Uses an in-place linked list in free nodes, for O(1) acquire, release and size overhead
template <class T>
struct linked_pool
{
    static_assert(sizeof(T) >= sizeof(T*), "linked_pool element type must be large enough to accomodate a pointer");
    using handle_t = uint32_t;

    static constexpr size_t sc_num_padding_bits = 3;
    static constexpr size_t sc_num_index_bits = 16;
    struct internal_handle_t
    {
        uint32_t index : sc_num_index_bits;
        uint32_t generation : 32 - (sc_num_padding_bits + sc_num_index_bits);
        uint32_t padding : sc_num_padding_bits;
    };
    static_assert(sizeof(internal_handle_t) == sizeof(handle_t));

    linked_pool() = default;
    linked_pool(linked_pool const&) = delete;
    linked_pool(linked_pool&&) noexcept = delete;
    linked_pool& operator=(linked_pool const&) = delete;
    linked_pool& operator=(linked_pool&&) noexcept = delete;

    ~linked_pool()
    {
        if (_pool)
        {
            std::free(_pool);
#if PHI_ENABLE_HANDLE_GEN_CHECK
            std::free(_generation);
#endif
        }
    }

    void initialize(size_t size)
    {
        if (size == 0)
            return;

        CC_ASSERT(size < 1u << sc_num_index_bits && "linked_pool size too large for index type");
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

#if PHI_ENABLE_HANDLE_GEN_CHECK
        // initialize generation handles
        _generation = static_cast<internal_handle_t*>(std::malloc(sizeof(internal_handle_t) * _pool_size));
        std::memset(_generation, 0, sizeof(internal_handle_t) * _pool_size);
#endif

        _first_free_node = &_pool[0];
    }

    [[nodiscard]] handle_t acquire()
    {
        CC_ASSERT(!is_full() && "linked_pool full");

        T* const acquired_node = _first_free_node;
        // read the in-place next pointer of this node
        _first_free_node = *reinterpret_cast<T**>(acquired_node);
        // call the constructor
        new (cc::placement_new, acquired_node) T();

        uint32_t const res_index = uint32_t(acquired_node - _pool);
        return acquire_handle(res_index);
    }

    void release(handle_t handle)
    {
        uint32_t real_index = read_index_on_release(handle);

        T* const released_node = &_pool[real_index];
        // call the destructor
        released_node->~T();
        // write the in-place next pointer of this node
        new (cc::placement_new, released_node) T*(_first_free_node);
        _first_free_node = released_node;
    }

    void release_node(T* node)
    {
        CC_ASSERT(node >= &_pool[0] && node < &_pool[_pool_size] && "node outside of pool");
#if PHI_ENABLE_HANDLE_GEN_CHECK
        // release not based on handle, so we can't check the generation
        ++_generation[node - _pool].generation; // increment generation on release
#endif

        // call the destructor
        node->~T();
        // write the in-place next pointer of this node
        new (cc::placement_new, node) T*(_first_free_node);

        _first_free_node = node;
    }

    T& get(handle_t handle)
    {
        uint32_t index = read_index(handle);
        CC_CONTRACT(index < _pool_size);
        return _pool[index];
    }

    T const& get(handle_t handle) const
    {
        uint32_t index = read_index(handle);
        CC_CONTRACT(index < _pool_size);
        return _pool[index];
    }

    uint32_t get_node_index(T const* node) const
    {
        CC_ASSERT(node >= &_pool[0] && node < &_pool[_pool_size] && "node outside of pool");
        return node - _pool;
    }

    uint32_t get_handle_index(handle_t handle) const { return read_index(handle); }

    bool is_full() const { return _first_free_node == nullptr; }

    size_t max_size() const { return _pool_size; }

    /// pass a lambda that is called with a T& of each allocated node
    /// acquire and release CAN be called from within the lambda
    /// This operation is slow and should not occur in normal operation
    template <class F>
    unsigned iterate_allocated_nodes(F&& func)
    {
        auto free_indices = get_free_node_indices();
        // sort ascending
        std::qsort(
            free_indices.data(), free_indices.size(), sizeof(free_indices[0]),
            +[](void const* a, void const* b) -> int { return int(*(handle_t*)a) - int(*(handle_t*)b); });

        unsigned num_iterated_nodes = 0;
        unsigned free_list_index = 0;
        for (handle_t i = 0u; i < _pool_size; ++i)
        {
            if (free_list_index >= free_indices.size() || i < free_indices[free_list_index])
            {
                // no free indices left, or before the next free index
                func(_pool[i]);
                ++num_iterated_nodes;
            }
            else
            {
                // on a free index
                CC_ASSERT(i == free_indices[free_list_index]);
                ++free_list_index;
            }
        }

        return num_iterated_nodes;
    }

    /// This operation is slow and should not occur in normal operation
    unsigned release_all()
    {
        return iterate_allocated_nodes([this](T& node) { release_node(&node); });
    }

private:
    cc::vector<handle_t> get_free_node_indices() const
    {
        cc::vector<handle_t> free_indices;
        free_indices.reserve(_pool_size);

        T* cursor = _first_free_node;
        while (cursor != nullptr)
        {
            free_indices.push_back(static_cast<handle_t>(cursor - _pool));
            cursor = *reinterpret_cast<T**>(cursor);
        }

        return free_indices;
    }

    handle_t acquire_handle(uint32_t real_index)
    {
#if PHI_ENABLE_HANDLE_GEN_CHECK
        internal_handle_t res;
        res.padding = 0;
        res.index = real_index;
        res.generation = _generation[real_index].generation;
        return cc::bit_cast<uint32_t>(res);
#else
        return real_index;
#endif
    }

    handle_t read_index(uint32_t handle) const
    {
#if PHI_ENABLE_HANDLE_GEN_CHECK
        CC_ASSERT(handle != uint32_t(-1) && "accessed null handle");
        internal_handle_t const parsed_handle = cc::bit_cast<internal_handle_t>(handle);
        uint32_t const real_index = parsed_handle.index;
        CC_ASSERT(parsed_handle.generation == _generation[real_index].generation && "accessed a stale handle");
        return real_index;
#else
        // we use the handle as-is, but mask out the padding
        // mask is
        // 0b00<..#sc_num_padding_bits..>00111<..rest of uint32..>1111
        constexpr uint32_t mask = ((uint32_t(1) << (32 - sc_num_padding_bits)) - 1);
        return handle & mask;
#endif
    }

    handle_t read_index_on_release(uint32_t handle) const
    {
#if PHI_ENABLE_HANDLE_GEN_CHECK
        uint32_t const real_index = read_index(handle);
        ++_generation[real_index].generation; // increment generation on release
        return real_index;
#else
        return read_index(handle);
#endif
    }

private:
    T* _first_free_node = nullptr;
    T* _pool = nullptr;
#if PHI_ENABLE_HANDLE_GEN_CHECK
    internal_handle_t* _generation = nullptr;
#endif
    size_t _pool_size = 0;
};

}
