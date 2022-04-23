#pragma once

#include <phantasm-hardware-interface/common/common_fwd.hh>

typedef struct VkDevice_T* VkDevice;
typedef struct VkQueue_T* VkQueue;
typedef struct VkCommandBuffer_T* VkCommandBuffer;
typedef struct VkInstance_T* VkInstance;

namespace phi::vk
{
class BackendVulkan;

class ShaderViewPool;
class ResourcePool;
class PipelinePool;
class CommandListPool;
class AccelStructPool;
class QueryPool;

struct command_list_translator;
struct vk_incomplete_state_cache;
}
