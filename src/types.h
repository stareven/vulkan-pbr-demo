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

struct UBO_MVP {
    Mat4 model, view, proj;
    Vec3 cameraPos;
};

struct UBOLight {
    Vec3 position;
    Vec3 color;
    float intensity;
};

struct UBO_Material {
    Vec3 albedo;
    float metallic;
    float roughness;
    float ao;
    float ior;        // 折射率 (玻璃 ~1.5)
    float opacity;    // 透明度 [0,1] (玻璃 < 1.0)
    int   _pad0;
    UBOLight lights[4];
    Vec3 ambientLight;
    Vec3 cameraPos;
    Vec3 emissive;        // 自发光颜色
    float emissiveStrength; // 自发光强度
};
