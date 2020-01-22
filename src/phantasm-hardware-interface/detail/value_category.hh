#pragma once

namespace phi::detail
{
enum class val_type
{
    prvalue,
    lvalue,
    xvalue
};

template <class T>
struct value_category
{
    static auto constexpr value = val_type::prvalue;
};

template <class T>
struct value_category<T&>
{
    static auto constexpr value = val_type::lvalue;
};

template <class T>
struct value_category<T&&>
{
    static auto constexpr value = val_type::xvalue;
};
}

#define PR_VALUE_CATEGORY(_expr_) ::phi::detail::value_category<decltype((_expr_))>::value

#define PR_IS_PRVALUE_EXPRESSION(_expr_) (PR_VALUE_CATEGORY(_expr_) == ::phi::detail::val_type::prvalue)
#define PR_IS_LVALUE_EXPRESSION(_expr_) (PR_VALUE_CATEGORY(_expr_) == ::phi::detail::val_type::lvalue)
#define PR_IS_XVALUE_EXPRESSION(_expr_) (PR_VALUE_CATEGORY(_expr_) == ::phi::detail::val_type::xvalue)
