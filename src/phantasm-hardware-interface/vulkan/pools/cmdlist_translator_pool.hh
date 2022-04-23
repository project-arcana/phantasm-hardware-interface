#pragma once

#include <clean-core/alloc_array.hh>
#include <clean-core/atomic_linked_pool.hh>

#include <phantasm-hardware-interface/handles.hh>

#include <phantasm-hardware-interface/fwd.hh>
#include <phantasm-hardware-interface/vulkan/fwd.hh>

namespace phi::vk
{
class CmdlistTranslatorPool
{
public:
    void initialize(VkDevice device,
                    ShaderViewPool* sv_pool,
                    ResourcePool* resource_pool,
                    PipelinePool* pso_pool,
                    CommandListPool* cmd_pool,
                    QueryPool* query_pool,
                    AccelStructPool* as_pool,
                    bool has_rt,
                    cc::allocator* pStaticAlloc,
                    uint32_t maxNumTranslators);

    void destroy();

public:
    handle::live_command_list createLiveCmdList(handle::command_list backing,
                                                VkCommandBuffer pRawCmdBuf,
                                                queue_type queue,
                                                vk_incomplete_state_cache* pStateCache,
                                                cmd::set_global_profile_scope const* pOptGlobalProfileScope = nullptr);

    handle::command_list freeLiveCmdList(handle::live_command_list list, bool bDoClose);

    command_list_translator* getTranslator(handle::live_command_list list) const { return mPool.get(list._value).pTranslator; }

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
} // namespace phi::vk
