
# Phantasm Hardware Interface

Phantasm hardware interface (phi) is an abstraction over Vulkan and D3D12, aiming to be succinct without restricting access to the underlying APIs within reason. Primary goals:

- Small API surface
- High performance
- Explicit lifetimes
- Heavy multithreading
- Low boilerplate

## Objects

The core of phi is the backend. This is the only type in the library with methods<sup>[1](#footnote1)</sup>. Everything happening with regards to the real graphics API is instructed through the backend. The two backends  (`BackendD3D12` and `BackendVulkan`) share a virtual interface and can be used interchangably.

phi has four main objects, accessed using lightweight, POD handles.

1. `handle::resource`

    A texture, render target, or buffer of any form

2. `handle::pipeline_state`

    Shaders, their input shape, and various pipeline configuration options form a pipeline_state. Graphics, compute, or raytracing.

3. `handle::shader_view`

    Shader views are a combination of resource views, for access in a shader.

4. `handle::command_list`

    Represents a recorded command list ready for GPU submission.

### Commands

Commands do the heavy lifting in communicating with a phi backend. They are simple structs, which are written contiguously into a piece of memory ("software command buffer"). This memory is then passed to the backend,

```C++
handle::command_list recordCommandList(std::byte* buffer, size_t size);
```

creating a `handle::command_list`. Command list recording is CPU-only, and entirely free-threaded. The received handle is eventually submitted to the GPU using `Backend::submit`, or freed using `Backend::discard`. Both of these calls consume the handle, command lists cannot be submitted multiple times.

Commands live in the `cmd` namespace, found in `commands.hh`. For easy command buffer writing, `command_stream_writer` is provided in the same header.

Commands are almost entirely stateless. The only state is the currently active render pass, marked by `cmd::begin_render_pass` and `cmd::end_render_pass` respectively. Other commands like `cmd::draw`, or `cmd::dispatch` (compute) contain all of the state they require, including their `handle::pipeline_state`.

### Shader Arguments

There are four shader input types, following D3D12 conventions:

1. CBVs - Constant Buffer Views

    A read-only buffer of limited size. HLSL `b` register.

    ```HLSL
    struct camera_constants {
        float4x4 view_proj;
        float3 cam_pos;
    };

    ConstantBuffer<camera_constants> g_frame_data       : register(b0, space0);
    ```

2. SRVs - Shader Resource Views

    A read-only texture, or a large, strided buffer. HLSL `t` register.

    ```HLSL
    StructuredBuffer<float4x4> g_model_matrices         : register(t0, space0);
    Texture2D g_albedo                                  : register(t1, space0);
    Texture2D g_normal                                  : register(t2, space0);
    ```

3. UAVs - Unordered Access Views

    A read-write texture or buffer. HLSL `u` register.

    ```HLSL
    RWTexture2DArray<float4> g_output                   : register(u0, space0);
    ```

4. Samplers

    State regarding the way to sample textures. HLSL `s` register.

    ```HLSL
    SamplerState g_sampler                              : register(s0, space0);
    ```

Shaders in phi can be fed with up to 4 "shader arguments". Each shader argument consists of SRVs, UAVs and samplers, all of which combine into a single `handle::shader_view`, and a separate CBV (`handle::resource`). Shader arguments correspond to HLSL spaces. Argument 0 is `space0`, 1 is `space1`, and so forth. The supplied order of inputs corresponds to the HLSL registers.

Shader argument "shapes", as in the amount of arguments, and the amount of inputs per argument type, are supplied when creating a `handle::pipeline_state`. The actual argument values are supplied in commands, like `cmd::draw`.

Inputs are not strictly typed, for example, a `Texture2D` can be supplied by simply "viewing" a single array slice of a texture array. Details regarding this process can be inferred from the creation of `handle::shader_view`, and the `shader_view_element` type.

## Threading

With one exception, phi is entirely free-threaded and internally synchronized. The synchronization is minimal, and parallel recording of command lists is encouraged, which takes place on thread-local components. The exception is the `Backend::submit` method, which must only be called on one thread at a time.

## Resource States

phi exposes a simplified version of resource states, especially when compared to Vulkan. Additionally, resource transitions only ever specify the after-state, the before-state is internally tracked (including thread- and record-order safety).

When transitioning towards `shader_resource` or `unordered_access`, the shader stage(s) which will use the resource next must also be specified.

## Main Loop

In the steady state, there is very little interaction with the backend apart from `command_list` recording and submission. A prototypical phi main loop looks like this:

```C++
while (window_open)
{
    if (window_resized)
        backend.onResize({w, h});

    if (backend.clearPendingResize())
    { // the backbuffer has resized, re-create size dependant resources
        // think of this event as entirely independent from a window resize, do all your resize logic here
        // use backend.getBackbufferSize()
    }

    // ... write and record main command lists ...

    auto const backbuffer = backend.acquireBackbuffer(); // as late as possible

    if (!backbuffer.is_valid())
    { // backbuffer acquire has failed, discard this frame
        backend.discard(cmdlists);
        continue;
    }

    // ... write and record final present command list ...
    // ... transition backbuffer to resource_state::present ...

    backend.submit(cmdlists);
    backend.present();
}
```

## Vulkan and D3D12 Specifics

### Shader Compilation

All shaders passed to phi must be in binary format: DXIL for D3D12, SPIR-V for Vulkan. Shaders are to be compiled from HLSL, using [DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler/releases) (find Linux binaries [here](https://github.com/project-arcana/arcana-samples/tree/develop/res/pr/liveness_sample/shader/dxc_bin/linux) or use [docker](https://hub.docker.com/r/gwihlidal/dxc/)).

When compiling HLSL to SPIR-V for Vulkan, use these flags: `-spirv -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all`. If it is a vertex, geometry or domain shader, the `-fvk-invert-y` must also be added.

For Vulkan, the shader `main` function must be named as such: Vertex - `main_vs`, hull - `main_hs`, domain - `main_ds`, geometry - `main_gs`, pixel - `main_ps`, compute - `main_cs`.

### D3D12 MIP alignment

As phi is a relatively thin layer over the native APIs, memory access to mapped buffers is unchanged from usual behavior. In D3D12, texture MIP pixel rows are aligned by 256 bytes, which must be respected. See arcana-samples for texture upload examples.

### D3D12 Relaxed API

Some parts of the API require less care when using D3D12:

1. Resource state transitions can omit shader dependencies.
2. `Backend::flushMappedMemory` does nothing and can be ignored.
3. `Backend::acquireBackbuffer` will never fail.

Some other fields of `cmd` structs might also be optional, which is noted in comments.

## Additional Details

### Dependencies

- clean-core
- typed-geometry
- rich-log
- reflector (optional)
- SDL2 (optional)

The Vulkan backend requires Vulkan SDK 1.1.260 or newer. The D3D12 backend requires Windows 1903 or newer, and the corresponding Windows SDK.

### Root Constants

`cmd::draw` and `cmd::dispatch` can additionally supply up to 8 bytes of data as root constants (push constants in Vulkan). Whether shaders take in root constants is specified at creation of `handle::pipeline_state`.

In HLSL, root constants are simply CBVs, always in register `b1`, `space0`. When compiling to SPIR-V, the CBV must be attributed like this:

```HLSL
[[vk::push_constant]] ConstantBuffer<my_struct> g_settings          : register(b1, space0);
```

### Window Handles

phi currently does not support a headless mode and requires a `native_window_handle` at initialization for swapchain creation. Supported types are Win32 windows (`HWND`), SDL2 windows (`SDL_Window*`), and Xlib windows (`Window` and `Display*`).

### Render Diagnostic Integration

phi detects RenderDoc and PIX, and can force a capture, using `Backend::startForcedDiagnosticCapture` and `Backend::endForcedDiagnosticCapture` respectively. PIX integration requires enabling the cmake option `PR_ENABLE_D3D12_PIX`, and requires having the PIX DLL available (next to) the executable. It is included here: `extern/win32_pix_runtime/bin/WinPixEventRuntime.dll`

### Backend Configuration

All backend configurability occurs at initialization using `backend_config`. Most default settings can be used without adjustment, but `validation`, `num_threads` and possibly `adapter_preference` should likely be adjusted.

### Raytracing

Work in progress. The features are functional in D3D12, but API is very likely to change in the future.

---

<a name="footnote1">1</a>: Methods with side-effects, other types do have convenience initializers and so forth.
