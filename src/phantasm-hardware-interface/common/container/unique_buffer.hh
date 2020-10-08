#pragma once

#include <cstddef>
#include <cstdlib>

#include <clean-core/bit_cast.hh>

namespace phi
{
struct unique_buffer
{
    explicit unique_buffer() = default;
    explicit unique_buffer(size_t size) : _ptr(size > 0 ? static_cast<std::byte*>(std::malloc(size)) : nullptr), _size(size) {}

    void allocate(size_t size)
    {
        std::free(_ptr);
        _ptr = (size > 0) ? static_cast<std::byte*>(std::malloc(size)) : nullptr;
    }

    unique_buffer(unique_buffer const&) = delete;
    unique_buffer& operator=(unique_buffer const&) = delete;

    unique_buffer(unique_buffer&& rhs) noexcept
    {
        _ptr = rhs._ptr;
        _size = rhs._size;
        rhs._ptr = nullptr;
    }
    unique_buffer& operator=(unique_buffer&& rhs) noexcept
    {
        if (this != &rhs)
        {
            std::free(_ptr);
            _ptr = rhs._ptr;
            _size = rhs._size;
            rhs._ptr = nullptr;
        }

        return *this;
    }

    ~unique_buffer() { std::free(_ptr); }

    std::byte* data() const { return _ptr; }
    std::byte* get() const { return _ptr; }
    char* get_as_char() const { return reinterpret_cast<char*>(_ptr); }
    size_t size() const { return _size; }

    bool is_valid() const { return _ptr != nullptr; }

    operator std::byte*() const& { return _ptr; }
    operator std::byte*() const&& = delete;

    bool operator==(unique_buffer const& rhs) const { return _ptr == rhs._ptr; }
    bool operator!=(unique_buffer const& rhs) const { return _ptr != rhs._ptr; }
    bool operator==(void const* rhs) const { return _ptr == rhs; }
    bool operator!=(void const* rhs) const { return _ptr != rhs; }

    [[nodiscard]] static unique_buffer create_from_binary_file(char const* filename);

    bool write_to_binary_file(char const* filename);

private:
    std::byte* _ptr = nullptr;
    size_t _size = 0;
};
}
