#pragma once

#include <clean-core/capped_vector.hh>
#include <clean-core/vector.hh>

namespace pr::backend::detail
{
/// very simple, purpose built, flat associative container
template <class KeyT, class ValT>
struct flat_map
{
public:
    flat_map(size_t reserve_size) { _nodes.reserve(reserve_size); }

    [[nodiscard]] ValT& get_value(KeyT const& key, ValT const& default_val = ValT{})
    {
        for (auto& node : _nodes)
        {
            if (node.key == key)
                return node.val;
        }

        auto& new_node = _nodes.emplace_back(key, default_val);
        return new_node.val;
    }

    [[nodiscard]] bool contains(KeyT const& key) const
    {
        for (auto const& node : _nodes)
            if (node.key == key)
                return true;

        return false;
    }

    void clear() { _nodes.clear(); }
    void shrink_to_fit() { _nodes.shrink_to_fit(); }
    void reserve(size_t size) { _nodes.reserve(size); }

    struct map_node
    {
        KeyT key;
        ValT val;

        map_node(KeyT const& key, ValT const& val) : key(key), val(val) {}
    };

    cc::vector<map_node> _nodes;
};

template <class KeyT, class ValT, size_t N>
struct capped_flat_map
{
public:
    [[nodiscard]] ValT& get_value(KeyT const& key, ValT const& default_val = ValT{})
    {
        for (auto& node : _nodes)
        {
            if (node.key == key)
                return node.val;
        }

        auto& new_node = _nodes.emplace_back(key, default_val);
        return new_node.val;
    }

    [[nodiscard]] bool contains(KeyT const& key) const
    {
        for (auto const& node : _nodes)
            if (node.key == key)
                return true;

        return false;
    }

    void clear() { _nodes.clear(); }

    struct map_node
    {
        KeyT key;
        ValT val;

        map_node(KeyT const& key, ValT const& val) : key(key), val(val) {}
    };

    cc::capped_vector<map_node, N> _nodes;
};
}
