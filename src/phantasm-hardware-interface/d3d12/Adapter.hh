#pragma once

#include <phantasm-hardware-interface/fwd.hh>

#include "common/d3d12_fwd.hh"

#include "common/shared_com_ptr.hh"

namespace phi::d3d12
{
/// Represents a IDXGIAdapter, the uppermost object in the D3D12 hierarchy
class Adapter
{
public:
    void initialize(backend_config const& config);
    void destroy();

    bool isValid() const { return mAdapter != nullptr; }

    [[nodiscard]] IDXGIAdapter& getAdapter() const { return *mAdapter; }
    [[nodiscard]] IDXGIFactory4& getFactory() const { return *mFactory; }

private:
    IDXGIAdapter* mAdapter = nullptr;
    IDXGIFactory4* mFactory = nullptr;
    IDXGIInfoQueue* mInfoQueue = nullptr;
};

}
