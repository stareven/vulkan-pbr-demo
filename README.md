# Vulkan PBR Demo

A real-time **Physically Based Rendering (PBR)** demo built with the Vulkan API. Showcases metallic/roughness shading, shadow mapping, and multiple material presets, all organized around a clean composition-based C++17 architecture.

![Screenshot](screenshot_test.png)

## Features

- **PBR shading** — Cook-Torrance BRDF with GGX microfacet distribution, Smith-Schlick geometry term, and Fresnel-Schlick approximation
- **Shadow mapping** — Two-pass rendering (light-space depth pass + main color pass) with PCF-softened shadows
- **Material presets** — Cycle through several canonical PBR materials (metals, dielectrics, translucent) at runtime
- **Multiple point lights** — Up to 4 animated lights rendered in a single forward pass
- **Swapchain recreation** — Robust window resize handling
- **Modular architecture** — Core logic split into focused managers (`VulkanContext`, `SwapchainManager`, `RenderPipeline`, `MeshManager`, `DescriptorManager`, `CommandManager`, `SyncManager`, `ShadowSystem`, `MaterialSystem`)

## Requirements

| Dependency | Notes |
| ---------- | ----- |
| C++17 compiler | GCC/Clang/MSVC |
| CMake ≥ 3.16 | |
| Vulkan SDK | Set `VULKAN_SDK` or install via the LunarG installer |
| GLFW3 | `brew install glfw` on macOS |
| `glslangValidator` | Ships with the Vulkan SDK; used to compile shaders to SPIR-V |

### macOS
The app runs through **MoltenVK** (Vulkan → Metal). The CMake build automatically links the `OpenGL`, `Metal`, and `QuartzCore` frameworks required by MoltenVK. `VK_KHR_portability_subset` is enabled in `src/types.h`.

## Build

```bash
# First-time setup
mkdir -p build && cd build
cmake ..

# Build
make

# Clean rebuild
rm -rf * && cmake .. && make
```

## Compile shaders

Compiled `.spv` files are intentionally not checked in (see `.gitignore`). Regenerate them after editing any shader:

```bash
glslangValidator -V shaders/shader.vert    -o shaders/shader.vert.spv
glslangValidator -V shaders/shader.frag    -o shaders/shader.frag.spv
glslangValidator -V shaders/shadow.vert    -o shaders/shadow.vert.spv
```

## Run

```bash
./build/vulkan_pbr
```

## Controls

| Key | Action |
| --- | ------ |
| `W` / `A` / `S` / `D` | Move camera |
| `Q` / `E` | Move camera down / up |
| Mouse drag | Orbit camera |
| `M` | Cycle material preset |
| `G` | Toggle glass / transparency mode (applies when preset 6 is selected) |
| `F` | Toggle emissive lighting |
| `Esc` | Quit |

## Project layout

```
vulkan_pbr_demo/
├── CMakeLists.txt
├── shaders/
│   ├── shader.vert       # Main pass vertex shader
│   ├── shader.frag       # PBR fragment shader
│   └── shadow.vert       # Shadow pass vertex shader
└── src/
    ├── main.cpp
    ├── pbr_app.h         # PBRApp — thin orchestrator over the managers
    ├── pbr_init.cpp      # Window creation + Vulkan init (calls each manager)
    ├── pbr_runtime.cpp   # Main loop, input, per-frame draw orchestration
    ├── pbr_cleanup.cpp   # Reverse-order manager cleanup + run()
    ├── window.*          # GLFW window creation, surface, resize flag
    ├── vulkan_context.*  # Instance / physical device / logical device / queues
    ├── sync_manager.*    # Semaphores + fences
    ├── swapchain_manager.*  # Swapchain, image views, depth buffer
    ├── render_pipeline.*    # Main render pass, pipeline layout, graphics pipeline, framebuffers
    ├── mesh_manager.*       # Vertex/index buffers + MVP/Material UBOs
    ├── descriptor_manager.* # Descriptor layouts / pool / sets (MVP + Material)
    ├── command_manager.*    # Command pool + per-frame and shadow command buffers
    ├── shadow_system.*      # Shadow map, shadow render pass, shadow pipeline, shadow UBO
    ├── material_system.*    # Material preset state (no Vulkan resources)
    ├── mesh.*               # Sphere/plane vertex generation utilities
    ├── types.h              # Vertex, UBOs, Vulkan constants
    ├── math_utils.h         # Vec2/Vec3/Mat4 helpers
    └── vulkan_utils.h       # Buffer creation, shader loading, queue finding
```

### Architecture

The codebase follows a **composition-based** design:

- **`PBRApp`** is a thin orchestrator. It owns instances of every manager and is responsible only for initialization order, input, per-frame sequencing, and cleanup order.
- **Each manager** owns a distinct slice of Vulkan state:
  - `VulkanContext` — instance, device, surface, queues
  - `SwapchainManager` — swapchain + image views + depth buffer
  - `RenderPipeline` — main render pass, pipeline layout, graphics pipeline, framebuffers
  - `MeshManager` — vertex/index buffers and per-frame MVP/Material UBOs
  - `DescriptorManager` — main descriptor layouts / pool / sets
  - `CommandManager` — command pool + per-frame command buffers + shadow command buffer
  - `SyncManager` — semaphores + fences (per-image + per-frame)
  - `ShadowSystem` — shadow map + shadow render pass + shadow pipeline + shadow UBOs + shadow sampler
  - `MaterialSystem` — material preset state (pure logic, no Vulkan resources)
  - `Window` — GLFW window + surface + resize flag

This split keeps each manager's lifetime, cleanup order, and public API easy to reason about.

## Rendering pipeline

1. **Shadow pass** — scene rendered from the light's point of view into a depth-only framebuffer (`ShadowSystem`)
2. **Main pass** — PBR fragment shader samples the shadow map and evaluates the BRDF for each light
3. Per-frame UBOs (`UBO_MVP`, `UBO_Material`, `UBO_Shadow`) are `alignas(16)` for `std140` compliance

## Debugging

Validation layers are disabled by default (`ENABLE_VALIDATION = false` in `src/types.h`). Flip the flag and rebuild to get full Vulkan validation output — invaluable when tracking down sync or layout issues.

## License

MIT
