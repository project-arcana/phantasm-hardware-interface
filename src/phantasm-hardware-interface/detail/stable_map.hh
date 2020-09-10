#pragma once

#include <cstring>

#include <clean-core/alloc_array.hh>
#include <clean-core/hash.hh>
#include <clean-core/utility.hh>

namespace phi::detail
{
template <class KeyT, class ValueT, class HashT = cc::hash<KeyT>>
struct stable_map
{
public:
    stable_map() = default;
    stable_map(stable_map const&) = delete;
    stable_map(stable_map&&) noexcept = delete;

    void initialize(size_t size, cc::allocator* alloc)
    {
        _values = cc::alloc_array<ValueT>::defaulted(size, alloc);
        _keys = cc::alloc_array<key_element>::defaulted(size, alloc);
    }

    void memset_values_zero() { std::memset(_values.data(), 0, _values.size_bytes()); }

    template <class T>
    bool contains_key(T const& key) const
    {
        auto idx = this->_get_location(key);
        for (auto _ = 0u; _ < _keys.size(); ++_)
        {
            idx = cc::wrapped_increment<size_t>(idx, _keys.size());

            key_element const& key_elem = _keys[idx];
            if (key_elem.occupied && key_elem.key == key)
            {
                return true;
            }
        }

        return false;
    }

    // operators
public:
    template <class T>
    ValueT& operator[](T const& key)
    {
        auto idx = this->_get_location(key);
        for (auto _ = 0u; _ < _keys.size(); ++_)
        {
            idx = cc::wrapped_increment<size_t>(idx, _keys.size());

            key_element const& key_elem = _keys[idx];
            if (key_elem.occupied)
            {
                if (key_elem.key == key)
                {
                    return _values[idx];
                }
                else
                {
                    // continue linear probing
                    continue;
                }
            }
            else
            {
                // reached a non-occupied field
                break;
            }
        }

        // have to fill non-occupied field
        key_element& new_key = _keys[idx];
        CC_ASSERT(!new_key.occupied && "stable_map full");
        new_key.key = KeyT(key);
        new_key.occupied = true;

        return _values[idx];
    }

    template <class F>
    void iterate_elements(F&& func)
    {
        for (auto i = 0u; i < _keys.size(); ++i)
        {
            if (_keys[i].occupied)
            {
                func(_values[i]);
            }
        }
    }

    void reset()
    {
        auto const size = _values.size();
        for (auto& val : _values)
            val = ValueT();
        for (auto& key : _keys)
            key = key_element();
    }

    // helper
private:
    template <class T>
    size_t _get_location(T const& key) const
    {
        CC_ASSERT(_values.size() > 0 && "empty stable_map");
        auto hash = HashT{}(key);
        hash = cc::hash_combine(hash, 0); // scramble a bit
        return hash % _values.size();
    }

private:
    struct key_element
    {
        KeyT key;
        bool occupied = false;
    };

    cc::alloc_array<ValueT> _values;
    cc::alloc_array<key_element> _keys;
};
}
