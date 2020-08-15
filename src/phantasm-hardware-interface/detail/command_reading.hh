#pragma once

#include <type_traits>

#include <clean-core/utility.hh>

#include <phantasm-hardware-interface/commands.hh>

namespace phi::cmd::detail
{
#define PHI_X(_val_)                                                                                                      \
    static_assert(std::is_trivially_copyable_v<::phi::cmd::_val_> && std::is_trivially_destructible_v<::phi::cmd::_val_>, \
                  #_val_ " is not trivially copyable / destructible");
PHI_CMD_TYPE_VALUES
#undef PHI_X

    /// returns the size in bytes of the given command
    [[nodiscard]] inline size_t
    get_command_size(detail::cmd_type type)
{
    switch (type)
    {
#define PHI_X(_val_)              \
    case detail::cmd_type::_val_: \
        return sizeof(::phi::cmd::_val_);
        PHI_CMD_TYPE_VALUES
#undef PHI_X
    }
    CC_UNREACHABLE("invalid command type");
    return 0; // suppress warnings
}

/// returns a string literal corresponding to the command type
[[nodiscard]] inline char const* to_string(detail::cmd_type type)
{
    switch (type)
    {
#define PHI_X(_val_)              \
    case detail::cmd_type::_val_: \
        return #_val_;
        PHI_CMD_TYPE_VALUES
#undef PHI_X
    }
    CC_UNREACHABLE("invalid command type");
    return ""; // suppress warnings
}

/// calls F::execute() with the apropriately downcasted command object as a const&
/// (F should have an execute method with overloads for all command objects)
template <class F>
void dynamic_dispatch(detail::cmd_base const& base, F& callback)
{
    switch (base.s_internal_type)
    {
#define PHI_X(_val_)              \
    case detail::cmd_type::_val_: \
        return callback.execute(static_cast<::phi::cmd::_val_ const&>(base));
        PHI_CMD_TYPE_VALUES
#undef PHI_X
    }
    CC_UNREACHABLE("invalid command");
}

[[nodiscard]] constexpr size_t compute_max_command_size()
{
    size_t res = 0;
#define PHI_X(_val_) res = cc::max(res, sizeof(::phi::cmd::_val_));
    PHI_CMD_TYPE_VALUES
#undef PHI_X
    return res;
}
}

namespace phi
{
struct command_stream_parser
{
public:
    struct iterator_end
    {
    };

    struct iterator
    {
        iterator(std::byte* pos, size_t size)
          : _pos(reinterpret_cast<cmd::detail::cmd_base*>(pos)), _remaining_size(_pos == nullptr ? 0 : static_cast<int64_t>(size))
        {
        }

        bool operator!=(iterator_end) const noexcept { return _remaining_size > 0; }

        iterator& operator++()
        {
            auto const advance = cmd::detail::get_command_size(_pos->s_internal_type);
            _pos = reinterpret_cast<cmd::detail::cmd_base*>(reinterpret_cast<std::byte*>(_pos) + advance);
            _remaining_size -= advance;
            return *this;
        }

        cmd::detail::cmd_base& operator*() const { return *(_pos); }

    private:
        cmd::detail::cmd_base* _pos = nullptr;
        int64_t _remaining_size = 0;
    };

public:
    command_stream_parser() = default;
    command_stream_parser(std::byte* buffer, size_t size) : _in_buffer(buffer), _size(buffer == nullptr ? 0 : size) {}

    void set_buffer(std::byte* buffer, size_t size)
    {
        _in_buffer = buffer;
        _size = (buffer == nullptr ? 0 : size);
    }

    iterator begin() const { return iterator(_in_buffer, _size); }
    iterator_end end() const { return iterator_end(); }

private:
    std::byte* _in_buffer = nullptr;
    size_t _size = 0;
};
}
