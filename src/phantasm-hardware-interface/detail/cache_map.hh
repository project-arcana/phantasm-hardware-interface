#pragma once

#include <cstdint>

#include <clean-core/array.hh>
#include <clean-core/utility.hh>

namespace phi::detail
{
/// Flat hash table with linear probing
/// Does not store or care for the key type
/// Unsynchronized, fixed-size, lookup results remain stable
template <class ValT, class HashT = cc::hash_t>
struct cache_map
{
public:
    static constexpr auto tombstone_hash = HashT(-1);

    void initialize(size_t size)
    {
        _hashes = cc::array<HashT>(size);
        _values = cc::array<ValT>(size);

        for (auto& h : _hashes)
            h = tombstone_hash;
    }

    [[nodiscard]] bool contains(HashT hash) const { return find_hash(hash) != _hashes.size(); }

    /// returns a pointer which remains stable (unless ::initialize is called again)
    [[nodiscard]] ValT* look_up(HashT hash)
    {
        auto const found_index = find_hash(hash);

        if (found_index != _hashes.size())
        {
            return &_values[found_index];
        }
        else
        {
            return nullptr;
        }
    }

    ValT* insert(HashT hash, ValT const& value)
    {
        CC_ASSERT(hash != tombstone_hash && "illegal hash value");

        auto index = hash % _hashes.size();
        for (auto _ = 0u; _ < _hashes.size(); ++_)
        {
            index = cc::wrapped_increment(index, _hashes.size());

            if (_hashes[index] == tombstone_hash)
            {
                _hashes[index] = hash;
                _values[index] = value;
                return &_values[index];
            }
        }

        CC_ASSERT(false && "cache_map full");
        return nullptr;
    }

    template <class F>
    void iterate_elements(F&& func)
    {
        for (auto i = 0u; i < _hashes.size(); ++i)
        {
            if (_hashes[i] != tombstone_hash)
                func(_values[i]);
        }
    }

    void clear()
    {
        for (auto& h : _hashes)
            h = tombstone_hash;
    }

private:
    [[nodiscard]] size_t find_hash(HashT hash) const
    {
        CC_ASSERT(hash != tombstone_hash && "illegal hash value");
        auto index = hash % _hashes.size();
        for (auto _ = 0u; _ < _hashes.size(); ++_)
        {
            index = cc::wrapped_increment(index, _hashes.size());

            if (_hashes[index] == hash)
            {
                return index;
            }
            else if (_hashes[index] == tombstone_hash)
            {
                return _hashes.size();
            }
        }
        return _hashes.size();
    }

    cc::array<HashT> _hashes;
    cc::array<ValT> _values;
};

}
