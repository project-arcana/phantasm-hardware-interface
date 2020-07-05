#include "gpu_stats.hh"

#include <type_traits>

#ifdef __linux__
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>
#else
#include <clean-core/native/win32_sanitized.hh>
#endif

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/detail/log.hh>

namespace
{
#ifdef CC_OS_WINDOWS

using platform_dll_t = ::HMODULE;

void* load_address(platform_dll_t dll, char const* funcname) { return reinterpret_cast<void*>(GetProcAddress(dll, funcname)); }
void close_dll(platform_dll_t dll) { FreeLibrary(dll); }

#elif defined(CC_OS_LINUX)

using platform_dll_t = void*;

void* load_address(platform_dll_t dll, char const* funcname) { return ::dlsym(dll, funcname); }
void close_dll(platform_dll_t dll) { ::dlclose(dll); }

#else
#error "Unsupported platform"
#endif

template <class T>
bool load_address_t(platform_dll_t dll, char const* name, T& out_func_ptr)
{
    static_assert(std::is_pointer_v<T>, "provide a reference to the function pointer");
    void* const loaded_raw = load_address(dll, name);
    if (!loaded_raw)
    {
        LOG_WARN("failed to load dll function {}", name);
        return false;
    }
    else
    {
        out_func_ptr = reinterpret_cast<T>(loaded_raw);
        return true;
    }
}


typedef struct nvmlDevice_st* nvmlDevice_t;
typedef struct nvmlPciInfo_st* nvmlPciInfo_t;
typedef int nvmlTemperatureSensors_t;
typedef int nvmlReturn_t;

struct nvml_dll_state
{
    platform_dll_t _dll = nullptr;
    char* (*_nvmlErrorString)();
    nvmlReturn_t (*_nvmlInit)();
    nvmlReturn_t (*_nvmlDeviceGetCount)(unsigned*);
    nvmlReturn_t (*_nvmlDeviceGetHandleByIndex)(unsigned, nvmlDevice_t*);
    nvmlReturn_t (*_nvmlDeviceGetName)(nvmlDevice_t, char*, unsigned);
    nvmlReturn_t (*_nvmlDeviceGetPciInfo)(nvmlDevice_t, nvmlPciInfo_t*);
    nvmlReturn_t (*_nvmlDeviceGetTemperature)(nvmlDevice_t, nvmlTemperatureSensors_t, unsigned*);
    nvmlReturn_t (*_nvmlDeviceGetFanSpeed)(nvmlDevice_t, unsigned*);
    nvmlReturn_t (*_nvmlShutdown)();

    bool load()
    {
        // early out on double init
        if (_dll)
            return true;

#ifdef CC_OS_WINDOWS
        // Try locally or in PATH (would be an override)
        _dll = LoadLibrary("nvml.dll");
        if (!_dll)
        {
            // Try in the canonical install directory (this is the more likely one)
            char expanded_path[512];
            ::ExpandEnvironmentStringsA("%ProgramW6432%\\NVIDIA Corporation\\NVSMI\\nvml.dll", expanded_path, sizeof(expanded_path));
            _dll = LoadLibrary(expanded_path);
        }
#elif defined(CC_OS_LINUX)
        // Available globally
        _dll = ::dlopen("libnvidia-ml.so", RTLD_LAZY | RTLD_GLOBAL);
#endif

        if (!_dll)
        {
            LOG_WARN("Unable to load NVML .dll/.so");
            return false;
        }


        auto const success = load_address_t(_dll, "nvmlInit_v2", _nvmlInit);
        if (!success)
        {
            // V1 API, differences are marginal
            auto const success = load_address_t(_dll, "nvmlInit", _nvmlInit);
            if (!success)
            {
                LOG_WARN("Unable to load nvmlInit address");
                return false;
            }
            else
            {
                load_address_t(_dll, "nvmlDeviceGetCount", _nvmlDeviceGetCount);
                load_address_t(_dll, "nvmlDeviceGetHandleByIndex", _nvmlDeviceGetHandleByIndex);
                load_address_t(_dll, "nvmlDeviceGetPciInfo", _nvmlDeviceGetPciInfo);
            }
        }
        else
        {
            // V2 API, recommended
            load_address_t(_dll, "nvmlDeviceGetCount_v2", _nvmlDeviceGetCount);
            load_address_t(_dll, "nvmlDeviceGetHandleByIndex_v2", _nvmlDeviceGetHandleByIndex);
            load_address_t(_dll, "nvmlDeviceGetPciInfo_v2", _nvmlDeviceGetPciInfo);
        }

        load_address_t(_dll, "nvmlShutdown", _nvmlShutdown);
        load_address_t(_dll, "nvmlErrorString", _nvmlErrorString);
        load_address_t(_dll, "nvmlDeviceGetName", _nvmlDeviceGetName);
        load_address_t(_dll, "nvmlDeviceGetTemperature", _nvmlDeviceGetTemperature);
        load_address_t(_dll, "nvmlDeviceGetFanSpeed", _nvmlDeviceGetFanSpeed);
        load_address_t(_dll, "nvmlDeviceGetCount_v2", _nvmlDeviceGetCount);

        auto const ret = _nvmlInit();
        if (ret != 0)
        {
            LOG_WARN("nvmlInit call unsuccessful (returned {})", ret);
            return false;
        }

        return true;
    }

    bool unload()
    {
        if (!_dll)
            return true;

        auto const ret = _nvmlShutdown();
        if (ret != 0)
        {
            LOG_WARN("nvmlShutdown unsuccessful");
        }

        close_dll(_dll);
        _dll = nullptr;
        return true;
    }
};

nvml_dll_state g_nvml;
}

bool phi::gpustats::initialize() { return g_nvml.load(); }

phi::gpustats::gpu_handle_t phi::gpustats::get_gpu_by_index(unsigned index)
{
    CC_ASSERT(g_nvml._dll != nullptr && "gpustats not initialized");
    nvmlDevice_t ret;
    if (g_nvml._nvmlDeviceGetHandleByIndex(index, &ret) == 0)
    {
        return static_cast<void*>(ret);
    }
    else
    {
        return nullptr;
    }
}

int phi::gpustats::get_temperature(phi::gpustats::gpu_handle_t handle)
{
    CC_ASSERT(g_nvml._dll != nullptr && "gpustats not initialized");

    if (!handle)
        return -1;

    unsigned ret;
    // magical 0 as second arg: only valid enum value at time of writing, represents main GPU die sensor
    g_nvml._nvmlDeviceGetTemperature(static_cast<nvmlDevice_t>(handle), 0, &ret);
    return int(ret);
}

int phi::gpustats::get_fanspeed_percent(phi::gpustats::gpu_handle_t handle)
{
    CC_ASSERT(g_nvml._dll != nullptr && "gpustats not initialized");

    if (!handle)
        return -1;

    unsigned ret;
    g_nvml._nvmlDeviceGetFanSpeed(static_cast<nvmlDevice_t>(handle), &ret);
    return int(ret);
}

void phi::gpustats::shutdown() { g_nvml.unload(); }
