# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Vulkan PBR (Physically Based Rendering) demo application written in C++17. Demonstrates real-time PBR rendering with shadow mapping using Vulkan API.

## Build and Run Commands

### Initial Setup (First Time)
```bash
mkdir -p build
cd build
cmake ..
```

### Build
```bash
cd build
make
```

### Run
```bash
./build/vulkan_pbr
```

### Clean Build
```bash
cd build
rm -rf *
cmake ..
make
```

## Shader Compilation

Shaders are compiled from GLSL to SPIR-V using `glslangValidator`:

```bash
glslangValidator -V shaders/shader.vert -o shaders/shader.vert.spv
glslangValidator -V shaders/shader.frag -o shaders/shader.frag.spv
glslangValidator -V shaders/shadow.vert -o shaders/shadow.vert.spv
```

Compiled `.spv` files are in `.gitignore` and must be regenerated after shader source changes.

## Dependencies

- **Vulkan SDK**: Required for Vulkan API
- **GLFW3**: Window creation and input handling (install via `brew install glfw` on macOS)
- **CMake 3.16+**: Build system

On macOS, the application links against OpenGL, Metal, and QuartzCore frameworks (required by MoltenVK).

## Architecture

### Design Pattern: Manager/System Composition

The codebase follows a composition-based architecture where a central `PBRApp` class coordinates multiple specialized manager classes. Each manager encapsulates a specific domain of Vulkan resources and logic:

**Core Managers:**
- **VulkanContext**: Core Vulkan instance, device, and queue management
- **Window**: GLFW window creation, event handling, and camera controls
- **SyncManager**: Frame synchronization (semaphores, fences)
- **SwapchainManager**: Swapchain lifecycle and image management
- **RenderPipeline**: Render passes, graphics pipelines, and framebuffers
- **MeshManager**: Vertex/index buffers and mesh data
- **DescriptorManager**: Descriptor sets and layouts
- **CommandManager**: Command pool and command buffer recording
- **ShadowSystem**: Shadow map rendering (separate render pass and pipeline)
- **MaterialSystem**: Material presets and PBR material state

### Key Files

- **src/pbr_app.h**: Main application class declaration showing all manager instances
- **src/types.h**: Core data structures (Vertex, UBOs) and Vulkan constants
- **src/math_utils.h**: Custom math library (Vec2, Vec3, Mat4) with Vulkan-specific transformations
- **src/vulkan_utils.h**: Helper functions for buffer creation, shader loading, queue finding

### Rendering Flow

1. **Initialization** (`pbr_init.cpp`): Creates window, Vulkan instance, device, and swapchain
2. **Resource Creation** (`pbr_swapchain.cpp`, `pbr_render.cpp`, etc.): Sets up render passes, pipelines, meshes, descriptors
3. **Main Loop** (`pbr_runtime.cpp`): Polls events, updates UBOs, records command buffers, draws frames
4. **Shadow Pass** (`pbr_shadow.cpp`): Renders depth map from light's perspective before main pass
5. **Cleanup** (`pbr_cleanup.cpp`): Destroys all Vulkan resources in correct order

### UBO Structure

Uniform buffers use `alignas(16)` for std140 compliance. Key structures:
- **UBO_MVP**: Model/view/projection matrices + light space matrix for shadows
- **UBO_Material**: PBR parameters (albedo, metallic, roughness, IOR, opacity) + up to 4 lights
- **UBO_Shadow**: Light space matrix for shadow mapping

## Validation

Validation layers are disabled by default (`ENABLE_VALIDATION = false` in `types.h`). To enable, set it to `true` and rebuild.

## Common Issues

**macOS-specific:**
- Requires `VK_KHR_portability_subset` extension (already included in `types.h`)
- MoltenVK translates Vulkan to Metal; requires linking OpenGL/Metal/QuartzCore frameworks

**Swapchain recreation:**
- Handled via `framebufferResized` flag in Window class
- Triggers `recreateSwapchain()` in main loop
