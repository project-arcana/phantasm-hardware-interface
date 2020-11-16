#pragma once

#include <cstring>

#include <phantasm-hardware-interface/types.hh>

namespace phi
{
#define PHI_LIST_PRIMITIVE_TOPOLOGY(X) X(triangles) X(lines) X(points) X(patches)
#define PHI_LIST_DEPTH_FUNCTION(X) X(none) X(less) X(less_equal) X(greater) X(greater_equal) X(equal) X(not_equal) X(always) X(never)
#define PHI_LIST_CULL_MODE(X) X(none) X(back) X(front)


#define PHI_X_TOSTRING_CASE(Val, ...) \
    case rf_xlist_enum::Val:          \
        return #Val;

#define PHI_X_FROMSTRING_IF(Val, ...)   \
    if (std::strcmp(str, #Val) == 0)    \
    {                                   \
        out_value = rf_xlist_enum::Val; \
        return true;                    \
    }

// clang-format off
#define PHI_DECLARE_TOSTRING(FuncName, Type, List)              \
    [[maybe_unused]] constexpr char const* FuncName(Type value) \
    {                                                           \
        using rf_xlist_enum = Type;                             \
        switch (value)                                          \
        {                                                       \
            List(PHI_X_TOSTRING_CASE)                           \
        default:                                                \
            return "unknown " #Type;                            \
        }                                                       \
    }
// clang-format on

// clang-format off
#define PHI_DECLARE_FROMSTRING(FuncName, Type, List)                        \
    [[maybe_unused]] inline bool FuncName(char const* str, Type& out_value) \
    {                                                                       \
        using rf_xlist_enum = Type;                                         \
        List(PHI_X_FROMSTRING_IF)                                           \
        return false;                                                       \
    }
// clang-format on

PHI_DECLARE_TOSTRING(enum_to_string, primitive_topology, PHI_LIST_PRIMITIVE_TOPOLOGY);
PHI_DECLARE_FROMSTRING(enum_from_string, primitive_topology, PHI_LIST_PRIMITIVE_TOPOLOGY);

PHI_DECLARE_TOSTRING(enum_to_string, depth_function, PHI_LIST_DEPTH_FUNCTION);
PHI_DECLARE_FROMSTRING(enum_from_string, depth_function, PHI_LIST_DEPTH_FUNCTION);

PHI_DECLARE_TOSTRING(enum_to_string, cull_mode, PHI_LIST_CULL_MODE);
PHI_DECLARE_FROMSTRING(enum_from_string, cull_mode, PHI_LIST_CULL_MODE);
}
