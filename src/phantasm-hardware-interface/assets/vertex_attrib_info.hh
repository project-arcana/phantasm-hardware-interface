#pragma once

#ifdef PHI_HAS_REFLECTOR

#include <clean-core/capped_vector.hh>
#include <clean-core/typedefs.hh>

#include <reflector/members.hh>

#include <typed-geometry/detail/comp_traits.hh>

#include <phantasm-hardware-interface/types.hh>

namespace phi::assets
{
/// Returns a capped vector of vertex attribute infos
template <class VertT>
[[nodiscard]] auto get_vertex_attributes();


namespace detail
{
template <class T>
constexpr format to_attribute_format()
{
    using af = format;

    if constexpr (tg::is_comp_like<T, 4, cc::float32>)
        return af::rgba32f;
    else if constexpr (tg::is_comp_like<T, 3, cc::float32>)
        return af::rgb32f;
    else if constexpr (tg::is_comp_like<T, 2, cc::float32>)
        return af::rg32f;
    else if constexpr (tg::is_comp_like<T, 1, cc::float32>)
        return af::r32f;

    else if constexpr (tg::is_comp_like<T, 4, cc::int32>)
        return af::rgba32i;
    else if constexpr (tg::is_comp_like<T, 3, cc::int32>)
        return af::rgb32i;
    else if constexpr (tg::is_comp_like<T, 2, cc::int32>)
        return af::rg32i;
    else if constexpr (tg::is_comp_like<T, 1, cc::int32>)
        return af::r32i;

    else if constexpr (tg::is_comp_like<T, 4, cc::uint32>)
        return af::rgba32u;
    else if constexpr (tg::is_comp_like<T, 3, cc::uint32>)
        return af::rgb32u;
    else if constexpr (tg::is_comp_like<T, 2, cc::uint32>)
        return af::rg32u;
    else if constexpr (tg::is_comp_like<T, 1, cc::uint32>)
        return af::r32u;

    else if constexpr (tg::is_comp_like<T, 4, cc::int16>)
        return af::rgba16i;
    else if constexpr (tg::is_comp_like<T, 2, cc::int16>)
        return af::rg16i;
    else if constexpr (tg::is_comp_like<T, 1, cc::int16>)
        return af::r16i;

    else if constexpr (tg::is_comp_like<T, 4, cc::uint16>)
        return af::rgba16u;
    else if constexpr (tg::is_comp_like<T, 2, cc::uint16>)
        return af::rg16u;
    else if constexpr (tg::is_comp_like<T, 1, cc::uint16>)
        return af::r16u;

    else if constexpr (tg::is_comp_like<T, 4, cc::int8>)
        return af::rgba8i;
    else if constexpr (tg::is_comp_like<T, 2, cc::int8>)
        return af::rg8i;
    else if constexpr (tg::is_comp_like<T, 1, cc::int8>)
        return af::r8i;

    else if constexpr (tg::is_comp_like<T, 4, cc::uint8>)
        return af::rgba8u;
    else if constexpr (tg::is_comp_like<T, 2, cc::uint8>)
        return af::rg8u;
    else if constexpr (tg::is_comp_like<T, 1, cc::uint8>)
        return af::r8u;

    else
    {
        static_assert(sizeof(T) == 0, "incompatbile attribute type");
        return af::rgba32f;
    }
}

template <class T>
static constexpr format as_attribute_format = detail::to_attribute_format<T>();

template <class VertT>
struct vertex_visitor
{
    static constexpr auto num_attributes = rf::member_count<VertT>;
    cc::capped_vector<vertex_attribute_info, num_attributes> attributes;

    template <class T>
    void operator()(T const& ref, char const* name)
    {
        vertex_attribute_info& attr = attributes.emplace_back();
        attr.semantic_name = name;
        // This call assumes that the original VertT& in get_vertex_attributes is a dereferenced nullptr
        attr.offset = uint32_t(reinterpret_cast<size_t>(&ref));
        attr.format = as_attribute_format<T>;
    }
};
}


template <class VertT>
[[nodiscard]] auto get_vertex_attributes()
{
    detail::vertex_visitor<VertT> visitor;
    VertT* volatile dummy_ptr = nullptr;
    rf::do_introspect(visitor, *dummy_ptr);
    return visitor.attributes;
}

}

#endif
