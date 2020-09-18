#pragma once

#include <cstdint>
#include <cstring>

#include <clean-core/alloc_vector.hh>
#include <clean-core/allocator.hh>
#include <clean-core/bit_cast.hh>
#include <clean-core/bits.hh>
#include <clean-core/new.hh>

namespace phi
{
void radix_sort(uint32_t* a, uint32_t* temp, size_t n);

/// Fixed-size object pool
/// Uses an in-place linked list in free nodes, for O(1) acquire, release and size overhead
/// Pointers remain stable
template <class T, bool GenCheckEnabled = false>
struct linked_pool
{
    using handle_t = uint32_t;

    linked_pool() = default;
    explicit linked_pool(size_t size, cc::allocator* allocator = cc::system_allocator) { initialize(size, allocator); }
    ~linked_pool() { _destroy(); }

    void initialize(size_t size, cc::allocator* allocator = cc::system_allocator)
    {
        static_assert(sizeof(T) >= sizeof(T*), "linked_pool element type must be large enough to accomodate a pointer");

        if (size == 0)
            return;

        if constexpr (sc_enable_gen_check)
        {
            CC_ASSERT(size < 1u << sc_num_index_bits && "linked_pool size too large for index type");
        }
        else
        {
            CC_ASSERT(size < (1u << (32 - sc_num_padding_bits)) && "linked_pool size too large for index type");
        }

        CC_ASSERT(_pool == nullptr && "re-initialized linked_pool");
        CC_CONTRACT(allocator != nullptr);
        _alloc = allocator;

        _pool_size = size;
        _pool = reinterpret_cast<T*>(_alloc->alloc(sizeof(T) * _pool_size, alignof(T)));

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

        if constexpr (sc_enable_gen_check)
        {
            // initialize generation handles
            _generation = reinterpret_cast<internal_handle_t*>(_alloc->alloc(sizeof(internal_handle_t) * _pool_size, alignof(internal_handle_t)));
            std::memset(_generation, 0, sizeof(internal_handle_t) * _pool_size);
        }

        _first_free_node = &_pool[0];
    }

    void destroy() { _destroy(); }

    [[nodiscard]] handle_t acquire()
    {
        CC_ASSERT(!is_full() && "linked_pool full");

        T* const acquired_node = _first_free_node;
        // read the in-place next pointer of this node
        _first_free_node = *reinterpret_cast<T**>(acquired_node);
        // call the constructor
        if constexpr (!std::is_trivially_constructible_v<T>)
            new (cc::placement_new, acquired_node) T();

        uint32_t const res_index = uint32_t(acquired_node - _pool);
        return _acquire_handle(res_index);
    }

    void release(handle_t handle)
    {
        uint32_t real_index = _read_index_on_release(handle);

        T* const released_node = &_pool[real_index];
        // call the destructor
        if constexpr (!std::is_trivially_destructible_v<T>)
            released_node->~T();
        // write the in-place next pointer of this node
        new (cc::placement_new, released_node) T*(_first_free_node);
        _first_free_node = released_node;
    }

    void release_node(T* node)
    {
        CC_ASSERT(node >= &_pool[0] && node < &_pool[_pool_size] && "node outside of pool");

        if constexpr (sc_enable_gen_check)
        {
            // release not based on handle, so we can't check the generation
            ++_generation[node - _pool].generation; // increment generation on release
        }

        // call the destructor
        if constexpr (!std::is_trivially_destructible_v<T>)
            node->~T();
        // write the in-place next pointer of this node
        new (cc::placement_new, node) T*(_first_free_node);

        _first_free_node = node;
    }

    CC_FORCE_INLINE T& get(handle_t handle)
    {
        uint32_t index = _read_index(handle);
        CC_CONTRACT(index < _pool_size);
        return _pool[index];
    }

    CC_FORCE_INLINE T const& get(handle_t handle) const
    {
        uint32_t index = _read_index(handle);
        CC_CONTRACT(index < _pool_size);
        return _pool[index];
    }

    CC_FORCE_INLINE uint32_t get_node_index(T const* node) const
    {
        CC_ASSERT(node >= &_pool[0] && node < &_pool[_pool_size] && "node outside of pool");
        return node - _pool;
    }

    bool is_alive(handle_t handle) const
    {
        // NOTE: sc_enable_gen_check is always true in debug, but this method requires
        // "hard enabled" generational checks via the template arguments (as it would otherwise fail in release)
        if constexpr (GenCheckEnabled)
        {
            CC_ASSERT(handle != uint32_t(-1) && "accessed null handle");
            internal_handle_t const parsed_handle = cc::bit_cast<internal_handle_t>(handle);
            return (parsed_handle.generation == _generation[parsed_handle.index].generation);
        }
        else
        {
            static_assert(GenCheckEnabled, "is_alive requires hard-enabled generational checks");
            return false;
        }
    }

    uint32_t get_handle_index(handle_t handle) const { return _read_index(handle); }

    bool is_full() const { return _first_free_node == nullptr; }

    size_t max_size() const { return _pool_size; }

    /// pass a lambda that is called with a T& of each allocated node
    /// acquire CAN be called from within the lambda
    /// release CAN be called from within the lambda ONLY for nodes already iterated (including the current one)
    /// this operation is slow and should not occur in normal operation
    template <class F>
    unsigned iterate_allocated_nodes(F&& func, cc::allocator* temp_alloc = cc::system_allocator)
    {
        if (_pool == nullptr)
            return 0;

        auto const free_indices = _get_free_node_indices(temp_alloc);

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
    unsigned release_all(cc::allocator* temp_alloc = cc::system_allocator)
    {
        return iterate_allocated_nodes([this](T& node) { release_node(&node); }, temp_alloc);
    }

    /// NOTE: advanced feature, returns a valid handle for the index
    /// without checking if it is allocated, bypassing future checks
    handle_t unsafe_construct_handle_for_index(uint32_t index) const { return _acquire_handle(index); }

private:
    // internally, generational checks are active in debug even if disabled via template argument
    // explicitly enabling allows for public ::is_alive() functionality
#ifdef CC_ENABLE_ASSERTIONS
    static constexpr bool sc_enable_gen_check = true;
#else
    static constexpr bool sc_enable_gen_check = GenCheckEnabled;
#endif

    static constexpr size_t sc_num_padding_bits = 3;
    static constexpr size_t sc_num_index_bits = 16;
    struct internal_handle_t
    {
        uint32_t index : sc_num_index_bits;
        uint32_t generation : 32 - (sc_num_padding_bits + sc_num_index_bits);
        uint32_t padding : sc_num_padding_bits;
    };
    static_assert(sizeof(internal_handle_t) == sizeof(handle_t));

private:
    // NOTE: Adding these isn't trivial because pointers in the linked list would have to be readjusted
    linked_pool(linked_pool const&) = delete;
    linked_pool(linked_pool&&) noexcept = delete;
    linked_pool& operator=(linked_pool const&) = delete;
    linked_pool& operator=(linked_pool&&) noexcept = delete;

private:
    /// returns indices of unallocated slots, sorted ascending
    cc::alloc_vector<handle_t> _get_free_node_indices(cc::allocator* temp_alloc) const
    {
        cc::alloc_vector<handle_t> free_indices(temp_alloc);
        free_indices.reserve(_pool_size);

        T* cursor = _first_free_node;
        while (cursor != nullptr)
        {
            free_indices.push_back(static_cast<handle_t>(cursor - _pool));
            cursor = *reinterpret_cast<T**>(cursor);
        }
        // sort ascending
        auto temp_sortvec = cc::alloc_vector<handle_t>::uninitialized(free_indices.size(), temp_alloc);
        radix_sort(free_indices.data(), temp_sortvec.data(), free_indices.size());

        return free_indices;
    }

    handle_t _acquire_handle(uint32_t real_index) const
    {
        if constexpr (sc_enable_gen_check)
        {
            internal_handle_t res;
            res.padding = 0;
            res.index = real_index;
            res.generation = _generation[real_index].generation;
            return cc::bit_cast<uint32_t>(res);
        }
        else
        {
            return real_index;
        }
    }

    CC_FORCE_INLINE handle_t _read_index(uint32_t handle) const
    {
        if constexpr (sc_enable_gen_check)
        {
            CC_ASSERT(handle != uint32_t(-1) && "accessed null handle");
            internal_handle_t const parsed_handle = cc::bit_cast<internal_handle_t>(handle);
            uint32_t const real_index = parsed_handle.index;
            CC_ASSERT(parsed_handle.generation == _generation[real_index].generation && "accessed a stale handle");
            return real_index;
        }
        else
        {
            // we use the handle as-is, but mask out the padding
            // mask is
            // 0b00<..#sc_num_padding_bits..>00111<..rest of uint32..>1111
            constexpr uint32_t mask = ((uint32_t(1) << (32 - sc_num_padding_bits)) - 1);
            return handle & mask;
        }
    }

    handle_t _read_index_on_release(uint32_t handle) const
    {
        if constexpr (sc_enable_gen_check)
        {
            uint32_t const real_index = _read_index(handle);
            ++_generation[real_index].generation; // increment generation on release
            return real_index;
        }
        else
        {
            return _read_index(handle);
        }
    }

    void _destroy()
    {
        if (_pool)
        {
            _alloc->free(_pool);
            _pool = nullptr;
            _pool_size = 0;
            if constexpr (sc_enable_gen_check)
            {
                _alloc->free(_generation);
                _generation = nullptr;
            }
        }
    }

private:
    T* _pool = nullptr;
    size_t _pool_size = 0;

    T* _first_free_node = nullptr;
    cc::allocator* _alloc = nullptr;

    // this field is useless for instances without generational checks,
    // but the impact is likely not worth the trouble of conditional inheritance
    internal_handle_t* _generation = nullptr;
};
}