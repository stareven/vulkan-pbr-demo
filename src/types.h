#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include "math_utils.h"

// ============================================================================
// 常量
// ============================================================================
constexpr uint32_t WIDTH  = 1280;
constexpr uint32_t HEIGHT = 720;
constexpr const char* TITLE = "Vulkan PBR Demo";

constexpr bool ENABLE_VALIDATION = false;
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
const std::vector<const char*> VALIDATION_LAYERS = {"VK_LAYER_KHRONOS_validation"};
const std::vector<const char*> DEVICE_EXTENSIONS = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    "VK_KHR_portability_subset"
};

// ============================================================================
// Vertex / UBO 结构
// ============================================================================
struct Vertex {
    Vec3 pos, normal;
    Vec2 uv;
};

struct alignas(16) UBO_MVP {
    Mat4 model, view, proj;   // 3 * 64 = 192 bytes
    Mat4 lightSpaceMatrix;    // offset 192, 64 bytes (光源 VP 矩阵，用于阴影)
    Vec3 cameraPos;           // offset 256, 12 bytes
    float _pad;               // offset 268, pad to 272
};

struct UBOLight {
    Vec3 position;    // offset 0, 12 bytes + 4 pad = 16 (std140 vec3 stride)
    float _pad0;
    Vec3 color;       // offset 16, 12 bytes
    float intensity;  // offset 28, 4 bytes
    // struct total: 32 bytes (std140 rounds to 16-byte alignment)
};

struct alignas(16) UBO_Material {
    Vec3 albedo;          // offset 0
    float metallic;       // offset 12
    float roughness;      // offset 16
    float ao;             // offset 20
    float ior;            // offset 24
    float opacity;        // offset 28
    UBOLight lights[4];   // offset 32, each 32 bytes -> ends at 160
    Vec3 ambientLight;    // offset 160
    float _pad1;          // offset 172 (pad to align cameraPos to 16)
    Vec3 cameraPos;       // offset 176
    float _pad2;          // offset 188 (pad to align emissive to 16)
    Vec3 emissive;        // offset 192
    float emissiveStrength; // offset 204
    // Total: 208 bytes (matches GLSL std140)
};

// ============================================================================
// Shadow UBO - 光源空间矩阵
// ============================================================================
struct alignas(16) UBO_Shadow {
    Mat4 lightSpaceMatrix;  // offset 0, 64 bytes (lightProj * lightView)
    Vec3 lightPos;          // offset 64, 12 bytes
    float _pad;             // offset 76, pad to 80
};
