cmake_minimum_required(VERSION 3.13 FATAL_ERROR)
project(PhantasmHardwareInterface)


if (NOT TARGET clean-core)
    message(FATAL_ERROR "[phantasm hardware interface] clean-core must be available")
endif()

# =========================================
# global options

option(PHI_BACKEND_VULKAN "enable Vulkan backend" ON)
option(PHI_BACKEND_D3D12 "enable DirectX 12 backend" ON)

# Enables PIX detection, marker support and capture forcing
# requires WinPixEventRuntime.dll to be available to the executable (in the same folder)
# this dll is included in extern/win32_pix_runtime/bin/ and is automatically copied if enabled
option(PHI_ENABLE_D3D12_PIX "enable DirectX 12 PIX integration" ON)

# Enables D3D12 Agility SDK (allowing use of newer D3D12 runtimes than the one shipped with the OS)
# if enabled, expects an include path visible to PHI with the Agility SDK headers, prefixed with D3D12AgilitySDK/
# meaning PHI will use #include <D3D12AgilitySDK/d3d12.h> instead of #include <d3d12.h>
# (exporting the required symbols and providing the DLLs and headers must be done by the user)
option(PHI_ENABLE_D3D12_AGILITY "enable DirectX 12 Agility SDK integration" OFF)

# Enables support for surface creation from SDL_Window*
option(PHI_ENABLE_SDL2 "enable SDL2 integration" ON)

# Enables Optick profiler GPU integration
# Set GPU scopes via cmd::begin_profile_scope and cmd::end_profile_scope
option(PHI_ENABLE_OPTICK "enable Optick profiler integration" OFF)

# =========================================
# post-process options

macro(_get_vulkan_sdk_version_number_from_folder path_to_check out_version)
    string ( REGEX MATCH ".*([0-9]+)\\.([0-9]+)\\.([0-9]+)\\.([0-9]+)" result "${path_to_check}" )
    if (result)
        # path has a version number
        SET (${out_version} "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}.${CMAKE_MATCH_4}" )
    endif()
endmacro()

if (PHI_BACKEND_VULKAN)
    # use provided .cmake modules in ./cmake/ with the latest FindVulkan.cmake, which is broken out of the box on older CMake versions
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

    find_package(Vulkan)

    if (NOT Vulkan_FOUND)
        message(WARNING "Failed to find Vulkan SDK, ensure Vulkan SDK is installed, and environment variable VULKAN_SDK is set correctly")
        set(PHI_BACKEND_VULKAN OFF)
    else()
        set(phi_found_vulkan_sdk_version "")
        _get_vulkan_sdk_version_number_from_folder(${Vulkan_INCLUDE_DIRS}, phi_found_vulkan_sdk_version)
        message(STATUS "[phantasm hardware interface] found Vulkan SDK '${phi_found_vulkan_sdk_version}' at ${Vulkan_INCLUDE_DIRS}")
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
        "${CMAKE_CURRENT_SOURCE_DIR}/extern/renderdoc_app/include/"
)

target_link_libraries(phantasm-hardware-interface PUBLIC
    clean-core
    typed-geometry
    rich-log
)


# =========================================
# set up compile flags

if (NOT MSVC)
    # Pthread
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)

    target_link_libraries(phantasm-hardware-interface PRIVATE -lX11 Threads::Threads -ldl)

    # Enable SSE 4.2 (available starting from Intel Nehalem (Nov 2008) and AMD Bulldozer (Oct 2011))
    # Used in sse_hash.hh
    # Could be made optional if necessary
    target_compile_options(phantasm-hardware-interface PRIVATE -msse4.2)
else()
    target_compile_options(phantasm-hardware-interface PRIVATE /WX)
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
		# we are on MSVC meaning CMAKE_RUNTIME_OUTPUT_DIRECTORY does not contain the /Debug or /Release ending, add it manually
        set(PHI_D3D12_PIX_DLL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/extern/win32_pix_runtime/bin/WinPixEventRuntime.dll")
        add_custom_command(
            TARGET phantasm-hardware-interface POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PHI_D3D12_PIX_DLL_PATH}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIGURATION>"
        )
		message(STATUS "[phantasm hardware interface] D3D12 PIX integration enabled")
    endif()

	# Agility SDK
	if (PHI_ENABLE_D3D12_AGILITY)
		# set defines
		target_compile_definitions(phantasm-hardware-interface 
		PUBLIC 
			PHI_HAS_D3D12_AGILITY
		)
		message(STATUS "[phantasm hardware interface] D3D12 Agility SDK integration enabled")
	endif()

else()
    message(STATUS "[phantasm hardware interface] D3D12 backend disabled")
endif()

# =========================================
# optional dependencies

if (PHI_ENABLE_SDL2)
    if (NOT TARGET sdl2)
        message(FATAL_ERROR "[phantasm hardware interface] PHI_ENABLE_SDL2 is active, but target sdl2 can't be found (wrong add_subdirectory order?)")
    endif()

    message(STATUS "[phantasm hardware interface] SDL2 support enabled")
    target_link_libraries(phantasm-hardware-interface PUBLIC sdl2)
    target_compile_definitions(phantasm-hardware-interface PUBLIC PHI_HAS_SDL2)
endif()

if (PHI_ENABLE_OPTICK)
    if (NOT TARGET OptickCore)
        message(FATAL_ERROR "[phantasm hardware interface] PHI_ENABLE_OPTICK is active, but target OptickCore can't be found (wrong add_subdirectory order?)")
    endif()
    
    message(STATUS "[phantasm hardware interface] Optick support enabled")
    target_link_libraries(phantasm-hardware-interface PUBLIC OptickCore)
    target_compile_definitions(phantasm-hardware-interface PUBLIC PHI_HAS_OPTICK)
endif()
