#pragma once

#include <stdint.h>

#include <clean-core/span.hh>

namespace phi
{
struct byte_reader
{
    byte_reader() = default;
    byte_reader(cc::span<std::byte const> buffer) : _buffer(buffer.data()), _head(buffer.data()), _buffer_end(buffer.data() + buffer.size()) {}

    template <class T>
    void read_t(T& out_t)
    {
        static_assert(std::is_trivially_copyable_v<T>, "T not memcpyable");
        CC_ASSERT(_head + sizeof(T) <= _buffer_end && "read OOB");
        std::memcpy(&out_t, _head, sizeof(T));
        _head += sizeof(T);
    }

    void read(cc::span<std::byte> out_data)
    {
        CC_ASSERT(_head + out_data.size() <= _buffer_end && "read OOB");
        std::memcpy(out_data.data(), _head, out_data.size());
        _head += out_data.size();
    }

    void const* read_size_and_skip(uint64_t& out_size, size_t skip_multiplier = 1)
    {
        read_t(out_size);
        auto* const res = head();
        skip(out_size * skip_multiplier);
        return res;
    }

    // in memory: [size_t: num] [T] [T] .. x num .. [T]
    template <class T>
    cc::span<T const> read_sized_array()
    {
        static_assert(sizeof(T) > 0, "requires complete T");
        uint64_t num_elems;
        void const* const res = read_size_and_skip(num_elems, sizeof(T));
        return cc::span{static_cast<T const*>(res), num_elems};
    }

    // in memory: [T] [T] .. x num .. [T]
    template <class T>
    cc::span<T const> read_unsized_array(size_t num_elems)
    {
        static_assert(sizeof(T) > 0, "requires complete T");
        auto* const res = head();
        skip(num_elems * sizeof(T));
        return cc::span{reinterpret_cast<T const*>(res), num_elems};
    }

    std::byte const* skip(size_t size)
    {
        auto* const res = _head;
        CC_ASSERT(_head + size <= _buffer_end && "skip OOB");
        _head += size;

        return res;
    }

    void reset() { _head = _buffer; }

    size_t size() const { return _buffer == nullptr ? 0 : _buffer_end - _buffer; }
    size_t size_left() const { return _buffer == nullptr ? 0 : _buffer_end - _head; }

    std::byte const* head() const { return _head; }

private:
    std::byte const* _buffer = nullptr;
    std::byte const* _head = nullptr;
    std::byte const* _buffer_end = nullptr;
};
} // namespace phi
