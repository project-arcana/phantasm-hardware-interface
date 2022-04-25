#include "cmdlist_translator_pool.hh"

#include <phantasm-hardware-interface/vulkan/cmd_buf_translation.hh>

void phi::vk::CmdlistTranslatorPool::initialize(VkDevice device,
                                                ShaderViewPool* sv_pool,
                                                ResourcePool* resource_pool,
                                                PipelinePool* pso_pool,
                                                CommandListPool* cmd_pool,
                                                QueryPool* query_pool,
                                                AccelStructPool* as_pool,
                                                bool has_rt,
                                                cc::allocator* pStaticAlloc,
                                                uint32_t maxNumTranslators)
{
    mPool.initialize(maxNumTranslators, pStaticAlloc);

    mTranslators.reset(pStaticAlloc, maxNumTranslators);

    for (auto i = 0u; i < maxNumTranslators; ++i)
    {
        auto* const pNewTranslator = pStaticAlloc->new_t<command_list_translator>();
        pNewTranslator->initialize(device, sv_pool, resource_pool, pso_pool, cmd_pool, query_pool, as_pool, has_rt);
        mTranslators[i] = pNewTranslator;
    }

    mBackingAlloc = pStaticAlloc;
}

void phi::vk::CmdlistTranslatorPool::destroy()
{
    mPool.iterate_allocated_nodes([](Node& node) { node.pTranslator->endTranslation(true); });

    for (auto* const pTranslator : mTranslators)
    {
        pTranslator->destroy();
        mBackingAlloc->delete_t(pTranslator);
    }
}

phi::handle::live_command_list phi::vk::CmdlistTranslatorPool::createLiveCmdList(handle::command_list backing,
                                                                                 VkCommandBuffer pRawCmdBuf,
                                                                                 queue_type queue,
                                                                                 vk_incomplete_state_cache* pStateCache,
                                                                                 cmd::set_global_profile_scope const* pOptGlobalProfileScope)
{
    CC_ASSERT(!mPool.is_full() && "Maximum amount of live commandlists reached - increase max_num_live_commandlists in config");

    auto res = mPool.acquire();

    Node& node = mPool.get(res);
    node.hBackingList = backing;
    node.pTranslator = mTranslators[mPool.get_handle_index(res)];

    node.pTranslator->beginTranslation(pRawCmdBuf, backing, queue, pStateCache, pOptGlobalProfileScope);

    return {res};
}

phi::handle::command_list phi::vk::CmdlistTranslatorPool::freeLiveCmdList(handle::live_command_list list, bool bDoClose)
{
    this->getTranslator(list)->endTranslation(bDoClose);
    auto const res = this->getBackingList(list);

    mPool.release(list._value);

    return res;
}
