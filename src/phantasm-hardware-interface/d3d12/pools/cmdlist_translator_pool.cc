#include "cmdlist_translator_pool.hh"

#include <phantasm-hardware-interface/d3d12/cmd_list_translation.hh>

void phi::d3d12::CmdlistTranslatorPool::initialize(ID3D12Device* device,
                                                   ShaderViewPool* sv_pool,
                                                   ResourcePool* resource_pool,
                                                   PipelineStateObjectPool* pso_pool,
                                                   AccelStructPool* as_pool,
                                                   QueryPool* query_pool,
                                                   cc::allocator* pStaticAlloc,
                                                   uint32_t maxNumTranslators)
{
    mPool.initialize(maxNumTranslators, pStaticAlloc);

    mTranslators.reset(pStaticAlloc, maxNumTranslators);

    for (auto i = 0u; i < maxNumTranslators; ++i)
    {
        auto* const pNewTranslator = pStaticAlloc->new_t<command_list_translator>();
        pNewTranslator->initialize(device, sv_pool, resource_pool, pso_pool, as_pool, query_pool);
        mTranslators[i] = pNewTranslator;
    }

    mBackingAlloc = pStaticAlloc;
}

void phi::d3d12::CmdlistTranslatorPool::destroy()
{
    mPool.iterate_allocated_nodes([](Node& node) { node.pTranslator->endTranslation(true); });

    for (auto* const pTranslator : mTranslators)
    {
        pTranslator->destroy();
        mBackingAlloc->delete_t(pTranslator);
    }
}

phi::handle::live_command_list phi::d3d12::CmdlistTranslatorPool::createLiveCmdList(handle::command_list backing,
                                                                                    ID3D12GraphicsCommandList5* pRawList,
                                                                                    queue_type queue,
                                                                                    incomplete_state_cache* pStateCache,
                                                                                    cmd::set_global_profile_scope const* pOptGlobalProfileScope)
{
    CC_ASSERT(!mPool.is_full() && "Maximum amount of live commandlists reached - increase max_num_live_commandlists in config");

    auto res = mPool.acquire();

    Node& node = mPool.get(res);
    node.hBackingList = backing;
    node.pTranslator = mTranslators[mPool.get_handle_index(res)];

    node.pTranslator->beginTranslation(pRawList, queue, pStateCache, pOptGlobalProfileScope);

    return {res};
}

phi::handle::command_list phi::d3d12::CmdlistTranslatorPool::freeLiveCmdList(handle::live_command_list list, bool bDoClose)
{
    this->getTranslator(list)->endTranslation(bDoClose);
    auto const res = this->getBackingList(list);

    mPool.release(list._value);

    return res;
}
