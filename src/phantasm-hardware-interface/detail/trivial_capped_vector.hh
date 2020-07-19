#pragma once

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <type_traits>

#include <clean-core/assert.hh>
#include <clean-core/forward.hh>
#include <clean-core/move.hh>
#include <clean-core/new.hh>

namespace phi::detail
{
/// cc::capped_vector, but trivial (no move/copy, no dtor)
/// For serialization purposes
template <class T, uint8_t N>
struct trivial_capped_vector
{
    static_assert(std::is_trivially_copyable_v<T>, "T not trivial enough");
    static_assert(N > 0, "empty capped vector not allowed");

    // properties
public:
    constexpr T* begin() { return &_vals[0]; }
    constexpr T const* begin() const { return &_vals[0]; }
    constexpr T* end() { return &_vals[0] + _size; }
    constexpr T const* end() const { return &_vals[0] + _size; }

    constexpr uint8_t size() const { return _size; }
    constexpr uint8_t capacity() const { return N; }
    constexpr bool empty() const { return _size == 0; }
    constexpr bool full() const { return _size == N; }

    constexpr T* data() { return &_vals[0]; }
    constexpr T const* data() const { return &_vals[0]; }

    constexpr T& front()
    {
        CC_CONTRACT(_size > 0);
        return _vals[0];
    }
    constexpr T const& front() const
    {
        CC_CONTRACT(_size > 0);
        return _vals[0];
    }

    constexpr T& back()
    {
        CC_CONTRACT(_size > 0);
        return _vals[_size - 1];
    }
    constexpr T const& back() const
    {
        CC_CONTRACT(_size > 0);
        return _vals[_size - 1];
    }

    constexpr T const& operator[](uint8_t pos) const
    {
        CC_CONTRACT(pos < _size);
        return _vals[pos];
    }

    constexpr T& operator[](uint8_t pos)
    {
        CC_CONTRACT(pos < _size);
        return _vals[pos];
    }

public:
    void push_back(T const& t)
    {
        CC_CONTRACT(_size < N);
        _vals[_size] = t;
        ++_size;
    }

    void pop_back()
    {
        CC_CONTRACT(_size > 0);
        --_size;
    }

    template <class... Args>
    T& emplace_back(Args&&... args)
    {
        CC_CONTRACT(_size < N);
        new (cc::placement_new, &_vals[_size]) T(cc::forward<Args>(args)...);
        ++_size;
        return _vals[_size - 1];
    }

    void clear() { _size = 0; }

public:
    trivial_capped_vector() = default;
    trivial_capped_vector(std::initializer_list<T> data)
    {
        CC_ASSERT(data.size() <= N && "initializer list too large");
        std::memcpy(_vals, data.begin(), sizeof(T) * data.size());
        _size = uint8_t(data.size());
    }

private:
    T _vals[N];
    uint8_t _size = 0;
};

static_assert(std::is_trivially_copyable_v<trivial_capped_vector<int, 5>>);
static_assert(std::is_trivially_destructible_v<trivial_capped_vector<int, 5>>);
static_assert(std::is_trivially_copy_assignable_v<trivial_capped_vector<int, 5>>);
static_assert(std::is_trivially_move_assignable_v<trivial_capped_vector<int, 5>>);

}
