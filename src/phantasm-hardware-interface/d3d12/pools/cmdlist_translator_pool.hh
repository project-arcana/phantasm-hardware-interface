#pragma once

#include <clean-core/alloc_array.hh>
#include <clean-core/atomic_linked_pool.hh>

#include <phantasm-hardware-interface/handles.hh>

#include <phantasm-hardware-interface/d3d12/fwd.hh>

namespace phi::d3d12
{
class CmdlistTranslatorPool
{
public:
    void initialize(ID3D12Device* device,
                    ShaderViewPool* sv_pool,
                    ResourcePool* resource_pool,
                    PipelineStateObjectPool* pso_pool,
                    AccelStructPool* as_pool,
                    QueryPool* query_pool,
                    cc::allocator* pStaticAlloc,
                    uint32_t maxNumTranslators);

    void destroy();

public:
    handle::live_command_list createLiveCmdList(handle::command_list backing,
                                                ID3D12GraphicsCommandList5* pRawList,
                                                queue_type queue,
                                                incomplete_state_cache* pStateCache,
                                                cmd::set_global_profile_scope const* pOptGlobalProfileScope = nullptr);

    handle::command_list freeLiveCmdList(handle::live_command_list list, bool bDoClose);

    command_list_translator* getTranslator(handle::live_command_list list) const
    {
        CC_ASSERT(list.is_valid());
        auto const& node = mPool.get(list._value);
        CC_ASSERT(node.pTranslator && "Accessed uninitialized command list translator");
        return node.pTranslator;
    }

    handle::command_list getBackingList(handle::live_command_list list) const { return mPool.get(list._value).hBackingList; }

private:
    struct Node
    {
        command_list_translator* pTranslator = nullptr;
        handle::command_list hBackingList = {};
    };

    cc::atomic_linked_pool<Node> mPool;
    cc::alloc_array<command_list_translator*> mTranslators;
    cc::allocator* mBackingAlloc = nullptr;
};
} // namespace phi::d3d12