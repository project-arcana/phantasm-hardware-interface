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

#define PHI_VALUE_CATEGORY(_expr_) ::phi::detail::value_category<decltype((_expr_))>::value

#define PHI_IS_PRVALUE_EXPRESSION(_expr_) (PHI_VALUE_CATEGORY(_expr_) == ::phi::detail::val_type::prvalue)
#define PHI_IS_LVALUE_EXPRESSION(_expr_) (PHI_VALUE_CATEGORY(_expr_) == ::phi::detail::val_type::lvalue)
#define PHI_IS_XVALUE_EXPRESSION(_expr_) (PHI_VALUE_CATEGORY(_expr_) == ::phi::detail::val_type::xvalue)

#define PHI_IMPLICATION(_premise_, _conclusion_) ((_premise_) ? (_conclusion_) : true)
#define PHI_EQUIVALENCE(_lhs_, _rhs_) (bool(_lhs_) == bool(_rhs_))
