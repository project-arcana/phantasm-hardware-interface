#pragma once

#include <clean-core/span.hh>

namespace phi
{
struct byte_reader
{
    byte_reader() = default;
    byte_reader(cc::span<cc::byte const> buffer) : _buffer(buffer.data()), _head(buffer.data()), _buffer_end(buffer.data() + buffer.size()) {}

    template <class T>
    void read_t(T& out_t)
    {
        static_assert(std::is_trivially_copyable_v<T>, "T not memcpyable");
        CC_ASSERT(_head + sizeof(T) <= _buffer_end && "read OOB");
        std::memcpy(&out_t, _head, sizeof(T));
        _head += sizeof(T);
    }

    void read(cc::span<cc::byte> out_data)
    {
        CC_ASSERT(_head + out_data.size() <= _buffer_end && "read OOB");
        std::memcpy(out_data.data(), _head, out_data.size());
        _head += out_data.size();
    }

    void skip(size_t size)
    {
        CC_ASSERT(_head + size <= _buffer_end && "skip OOB");
        _head += size;
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
}
