#pragma once

#include <guiddef.h> // for __uuidof; this header is tame, only other include is string.h
#undef FAR
#undef _far

namespace phi::d3d12
{
/// A wrapper for WIN32 COM reference counted pointers (custom WRL::ComPtr)
/// Does not incur additional overhead as the refcounting part is already implemented d3d12-side
template <class T>
class shared_com_ptr
{
public:
    explicit shared_com_ptr() : _pointer(nullptr) {}
    explicit shared_com_ptr(T* ptr, bool add_ref = true) : _pointer(ptr)
    {
        if (_pointer && add_ref)
            _pointer->AddRef();
    }

    ~shared_com_ptr()
    {
        if (_pointer)
            _pointer->Release();
    }

    /// Copy ctor
    shared_com_ptr(shared_com_ptr const& rhs) noexcept
    {
        _pointer = rhs._pointer;
        if (_pointer)
            _pointer->AddRef();
    }

    /// Copy assign
    shared_com_ptr& operator=(shared_com_ptr const& rhs) noexcept { return *this = rhs._pointer; }

    /// Move ctor
    shared_com_ptr(shared_com_ptr&& rhs) noexcept
    {
        _pointer = rhs._pointer;
        rhs._pointer = nullptr;
    }

    /// Move assign
    shared_com_ptr& operator=(shared_com_ptr&& rhs) noexcept
    {
        if (this != &rhs)
        {
            T* const old_ptr = _pointer;
            _pointer = rhs._pointer;
            rhs._pointer = nullptr;
            if (old_ptr)
                old_ptr->Release();
        }

        return *this;
    }

    /// Raw pointer assign
    shared_com_ptr& operator=(T* ptr) noexcept
    {
        T* const old_ptr = _pointer;
        _pointer = ptr;
        if (_pointer)
            _pointer->AddRef();
        if (old_ptr)
            old_ptr->Release();
        return *this;
    }

    /// Optimization for nullptr literals
    shared_com_ptr& operator=(decltype(nullptr)) noexcept
    {
        if (_pointer)
            _pointer->Release();
        _pointer = nullptr;
        return *this;
    }


    T* operator->() const { return _pointer; }
    operator T*() const& { return _pointer; }
    operator T*() const&& = delete;

    /// Safely release possibly stored pointer, then return a pointer to the inner T* for reassignemnt
    /// Regularly used in D3D12 API interop, T** arguments are common
    [[nodiscard]] T** override() noexcept
    {
        *this = nullptr;
        return &_pointer;
        // ->AddRef() is not required after this operation, as the assigned COM ptr starts out with refcount 1
        // If the API call fails to assign, this object's state remains valid as well
    }

    [[nodiscard]] T* get() const& noexcept { return _pointer; }
    [[nodiscard]] T* get() const&& noexcept = delete;

    template <class U>
    [[nodiscard]] auto get_interface(shared_com_ptr<U>& rhs) const noexcept
    {
        return _pointer->QueryInterface(__uuidof(U), reinterpret_cast<void**>(rhs.override()));
    }

    bool is_valid() const { return _pointer != nullptr; }

private:
    T* _pointer;
};

template <class T>
bool operator==(shared_com_ptr<T> const& lhs, shared_com_ptr<T> const& rhs)
{
    return lhs.get() == rhs.get();
}
}

/// Shorthand for the commonly occuring IID_PPV_ARGS(my_com_ptr.override()) argument in D3D12 APIs
#define PHI_COM_WRITE(_com_ptr_) IID_PPV_ARGS(_com_ptr_.override())
