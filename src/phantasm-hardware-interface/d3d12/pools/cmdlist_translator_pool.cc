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
    CC_ASSERT(maxNumTranslators > 0);

    mTranslatorContext.initialize(device, sv_pool, resource_pool, pso_pool, as_pool, query_pool);
    mTranslatorLocals.reset(pStaticAlloc, maxNumTranslators);
    for (auto i = 0u; i < maxNumTranslators; ++i)
    {
        mTranslatorLocals[i].initialize(*device);
    }

    mPool.initialize(maxNumTranslators, pStaticAlloc);
}

void phi::d3d12::CmdlistTranslatorPool::destroy()
{
    mPool.iterate_allocated_nodes([](Node& node) { node.Translator.endTranslation(true); });

    for (TranslatorLocals& locals : mTranslatorLocals)
    {
        locals.destroy();
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
    uint32_t const TranslatorIndex = mPool.get_handle_index(res);
    CC_ASSERT(TranslatorIndex < mTranslatorLocals.size() && "Translator index OOB");

    Node& node = mPool.get(res);
    node.hBackingList = backing;
    node.Translator.initialize(&mTranslatorContext, &mTranslatorLocals[TranslatorIndex]);
    CC_ASSERT(node.Translator._context->device != nullptr && "Translator has invalid ID3D12Device*");

    node.Translator.beginTranslation(pRawList, queue, pStateCache, pOptGlobalProfileScope);

    return {res};
}

phi::handle::command_list phi::d3d12::CmdlistTranslatorPool::freeLiveCmdList(handle::live_command_list hLiveList, bool bDoClose)
{
    CommandListTranslator* const pTranslator = getTranslator(hLiveList);
    CC_ASSERT(pTranslator->_context->device != nullptr && "Translator has invalid ID3D12Device*");
    pTranslator->endTranslation(bDoClose);

    auto const hBackingList = getBackingList(hLiveList);

    mPool.release(hLiveList._value);

    return hBackingList;
}
