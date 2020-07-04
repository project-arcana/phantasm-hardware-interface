cmake_minimum_required(VERSION 3.8 FATAL_ERROR)
project(PhantasmHardwareInterface)


if (NOT TARGET clean-core)
    message(FATAL_ERROR "[phantasm hardware interface] clean-core must be available")
endif()

# =========================================
# global options

option(PHI_BACKEND_VULKAN "enable Vulkan backend" ON)
option(PHI_BACKEND_D3D12 "enable DirectX 12 backend" ON)

# If disabled, cmd::debug_marker is ignored
option(PHI_ENABLE_DEBUG_MARKERS "enable API debug markers" ON)

# Enables PIX detection, marker support and capture forcing
# requires WinPixEventRuntime.dll to be available to the executable (in the same folder)
# this dll is included in extern/win32_pix_runtime/bin/
option(PHI_ENABLE_D3D12_PIX "enable DirectX 12 PIX integration" OFF)
option(PHI_ENABLE_SDL2 "enable SDL2 integration" ON)

# =========================================
# post-process options

if (PHI_BACKEND_VULKAN)
    # use provided .cmake modules in ./cmake/ with the latest FindVulkan.cmake, which is broken out of the box on older CMake versions
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

    find_package(Vulkan)

    if (NOT Vulkan_FOUND)
        set(PHI_BACKEND_VULKAN OFF)
    endif()
endif()

if (NOT MSVC)
    set(PHI_BACKEND_D3D12 OFF)
endif()


# =========================================
# define library

file(GLOB_RECURSE SOURCES "src/*.cc" "src/*.c")
file(GLOB_RECURSE HEADERS "src/*.hh" "src/*.h")

# disable backends
if (NOT PHI_BACKEND_D3D12)
    foreach(src ${SOURCES})
        if (${src} MATCHES "phantasm-hardware-interface/d3d12/")
            list(REMOVE_ITEM SOURCES ${src})
        endif()
    endforeach()
endif()

if (NOT PHI_BACKEND_VULKAN)
    foreach(src ${SOURCES})
        if (${src} MATCHES "phantasm-hardware-interface/vulkan/")
            list(REMOVE_ITEM SOURCES ${src})
        endif()
    endforeach()
endif()

arcana_add_library(PHI phantasm-hardware-interface SOURCES HEADERS)

target_include_directories(phantasm-hardware-interface
    PUBLIC
        src/
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/extern/renderdoc_app/include/"
)

target_link_libraries(phantasm-hardware-interface PUBLIC
    clean-core
    typed-geometry
    rich-log
)

if (PHI_ENABLE_DEBUG_MARKERS)
    target_compile_definitions(phantasm-hardware-interface PUBLIC PHI_ENABLE_DEBUG_MARKERS)
endif()


# =========================================
# set up compile flags

if (NOT MSVC)
    # Pthread
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)

    target_link_libraries(phantasm-hardware-interface PRIVATE -lX11 Threads::Threads -ldl)
endif()

# =========================================
# backends

if (PHI_BACKEND_VULKAN)
    message(STATUS "[phantasm hardware interface] Vulkan backend enabled")
    target_compile_definitions(phantasm-hardware-interface PUBLIC PHI_BACKEND_VULKAN)
    target_link_libraries(phantasm-hardware-interface PUBLIC ${Vulkan_LIBRARY})
    target_include_directories(phantasm-hardware-interface PUBLIC ${Vulkan_INCLUDE_DIRS})
else()
    message(STATUS "[phantasm hardware interface] Vulkan backend disabled")
endif()

if (PHI_BACKEND_D3D12)
    message(STATUS "[phantasm hardware interface] D3D12 backend enabled")
    target_compile_definitions(phantasm-hardware-interface PUBLIC PHI_BACKEND_D3D12)
    target_link_libraries(phantasm-hardware-interface PRIVATE
        "d3d12.lib" "dxgi.lib" "dxguid.lib"
        )

    # PIX Runtime
    if (PHI_ENABLE_D3D12_PIX)
        # link against pix runtime .lib, add pix include path, set up defines
        target_link_libraries(phantasm-hardware-interface PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/extern/win32_pix_runtime/bin/WinPixEventRuntime.lib")
        target_include_directories(phantasm-hardware-interface PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/extern/win32_pix_runtime/include/")
        target_compile_definitions(phantasm-hardware-interface PUBLIC PHI_HAS_PIX USE_PIX)

        # copy the dll to the runtime output folder post-build
        set(PHI_D3D12_PIX_DLL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/extern/win32_pix_runtime/bin/WinPixEventRuntime.dll")
        add_custom_command(
            TARGET phantasm-hardware-interface POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PHI_D3D12_PIX_DLL_PATH}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        )
    endif()

else()
    message(STATUS "[phantasm hardware interface] D3D12 backend disabled")
endif()

# =========================================
# optional dependencies

if (PHI_ENABLE_SDL2)
    if (NOT TARGET sdl2)
        message(FATAL_ERROR "[phantasm hardware interface] no target sdl2 found (wrong add_subdirectory order?)")
    endif()

    message(STATUS "[phantasm hardware interface] SDL2 support enabled")
    target_link_libraries(phantasm-hardware-interface PUBLIC sdl2)
    target_compile_definitions(phantasm-hardware-interface PUBLIC PHI_HAS_SDL2)
endif()
