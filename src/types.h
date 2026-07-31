#pragma once

// ----------------------------------------------------------------------------
// Vulkan 头文件: 提供所有 Vk* 类型和 VK_* 常量
// ----------------------------------------------------------------------------
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include "math_utils.h"

// ============================================================================
// 全局配置常量
// ============================================================================
// 所有常量放入 config 命名空间, 避免全局污染
namespace config {

// 窗口默认尺寸
constexpr uint32_t WIDTH  = 1280;
constexpr uint32_t HEIGHT = 720;
constexpr const char* TITLE = "Vulkan PBR Demo";

// 是否启用 Vulkan 校验层 (Validation Layers):
//   - true:  开启 VK_LAYER_KHRONOS_validation, 会捕获 API 误用/内存泄漏, 性能下降
//   - false: 关闭校验层, Release 构建用, 性能最佳
// 校验层是 Vulkan 最重要的调试工具, 开发期务必开启
constexpr bool ENABLE_VALIDATION = false;

// 最大同时渲染帧数 (Frames in Flight):
//   - 控制 CPU 和 GPU 的流水线重叠程度
//   - 设为 2: 典型的"双缓冲"流水线, CPU 渲染第 N+1 帧时 GPU 在处理第 N 帧
//   - 设得太大: 增加输入延迟 (用户操作到画面显示的延迟)
//   - 设得太小: GPU 可能空闲等待, 性能下降
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

// 校验层列表:
//   - VK_LAYER_KHRONOS_validation: Khronos 官方校验层, 包含所有标准校验
//   用 inline 函数返回静态 vector, 保证只初始化一次且线程安全 (C++11 起)
inline const std::vector<const char*>& validationLayers() {
    static const auto v = std::vector<const char*>{"VK_LAYER_KHRONOS_validation"};
    return v;
}

// 设备级扩展列表:
//   - VK_KHR_swapchain: 必需, 让设备能创建 Swapchain 来呈现图像
//   - VK_KHR_portability_subset: macOS/iOS 必需, MoltenVK 是不完全兼容实现,
//     必须声明这个扩展才能使用 MoltenVK 设备
inline const std::vector<const char*>& deviceExtensions() {
    static const auto v = std::vector<const char*>{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        "VK_KHR_portability_subset"
    };
    return v;
}

} // namespace config

// ============================================================================
// 顶点结构 - GPU 渲染时使用的顶点数据格式
// ============================================================================
// 顶点是 GPU 渲染的基本输入, 每个顶点包含:
//   - pos: 3D 位置 (模型空间坐标)
//   - normal: 法向量 (用于光照计算, 必须是单位向量)
//   - uv: 纹理坐标 (用于贴图采样, 范围通常是 [0,1])
// 注意: 这个结构没有用 alignas, 因为 Vulkan 会按紧凑布局绑定,
//       只要 shader 里用 std430 或匹配 layout 即可
struct Vertex {
    glm::vec3 pos, normal;  // 位置 + 法向量, 各 12 字节
    glm::vec2 uv;           // 纹理坐标, 8 字节
    // 总计: 32 字节/顶点
};

// ============================================================================
// MVP UBO (Uniform Buffer Object) - 主渲染 pass 的变换矩阵
// ============================================================================
// UBO 是 GPU 的"只读常量缓冲区", 用于从 CPU 向 shader 传递变换矩阵等参数
// alignas(16): Vulkan 的 std140 布局要求 UBO 成员必须 16 字节对齐
//   - std140 是 OpenGL/Vulkan 的统一缓冲标准, 规定了成员在内存中的布局规则
//   - vec3 实际占用 16 字节 (12 字节数据 + 4 字节填充)
//   - mat4 占用 64 字节 (4 个 vec4, 每个 16 字节)
//   - 结构体总大小必须是 16 的倍数
// 这个 UBO 包含:
//   - model/view/proj: 模型/视图/投影矩阵, 用于 MVP 变换
//   - lightSpaceMatrix: 光源空间的 VP 矩阵, 用于阴影贴图采样
//   - cameraPos: 相机位置, 用于 PBR 光照计算 (视角方向)
struct alignas(16) UBO_MVP {
    glm::mat4 model, view, proj;        // 3 个矩阵, 各 64 字节, 共 192 字节
    glm::mat4 lightSpaceMatrix;         // 光源 VP 矩阵, 64 字节, 偏移 192
    glm::vec3 cameraPos;                // 相机位置, 12 字节, 偏移 256
    float _pad;                    // 填充 4 字节, 凑齐 16 字节对齐, 偏移 268
    // 总计: 272 字节 (必须是 16 的倍数)
};

// ============================================================================
// 光源结构 - PBR 光照计算用的单个光源参数
// ============================================================================
// std140 布局下 vec3 实际占 16 字节 (12 字节数据 + 4 字节填充),
// 所以每个光源结构是 32 字节:
//   - position: 光源世界坐标 (vec3, 实际占 16 字节)
//   - color: 光源颜色 (vec3, 实际占 16 字节, 但这里紧跟 intensity)
//   - intensity: 光源强度 (float, 4 字节)
//   - 填充: 凑齐 32 字节 (16 字节对齐)
struct UBOLight {
    glm::vec3 position;    // 光源位置, 12 字节 + 4 字节填充 = 16 字节, 偏移 0
    float _pad0;      // 填充, 凑齐 vec3 的 16 字节对齐
    glm::vec3 color;       // 光源颜色 (RGB), 12 字节, 偏移 16
    float intensity;  // 光源强度, 4 字节, 偏移 28
    // 总计: 32 字节 (std140 下结构体大小是 16 的倍数)
};

// ============================================================================
// 材质 UBO - PBR 渲染的材质参数
// ============================================================================
// PBR (Physically Based Rendering) 需要以下材质参数:
//   - albedo: 基础颜色 (漫反射颜色), RGB
//   - metallic: 金属度 [0,1], 0=非金属, 1=金属
//   - roughness: 粗糙度 [0,1], 0=光滑如镜, 1=完全粗糙
//   - ao: 环境光遮蔽 [0,1], 模拟缝隙/褶皱处的阴影
//   - ior: 折射率 (Index of Refraction), 玻璃=1.5, 水=1.33
//   - opacity: 不透明度 [0,1], 0=完全透明, 1=完全不透明
//   - lights[4]: 最多 4 个点光源
//   - ambientLight: 环境光颜色
//   - cameraPos: 相机位置 (用于计算视角方向)
//   - emissive: 自发光颜色
//   - emissiveStrength: 自发光强度
// 所有成员都用 padding 凑齐 16 字节对齐, 符合 std140 规范
struct alignas(16) UBO_Material {
    glm::vec3 albedo;          // 基础颜色, 偏移 0
    float metallic;       // 金属度, 偏移 12
    float roughness;      // 粗糙度, 偏移 16
    float ao;             // 环境光遮蔽, 偏移 20
    float ior;            // 折射率, 偏移 24
    float opacity;        // 不透明度, 偏移 28
    UBOLight lights[4];   // 4 个光源, 各 32 字节, 共 128 字节, 偏移 32 -> 160
    glm::vec3 ambientLight;    // 环境光颜色, 偏移 160
    float _pad1;          // 填充, 凑齐 16 字节, 偏移 172
    glm::vec3 cameraPos;       // 相机位置, 偏移 176
    float _pad2;          // 填充, 凑齐 16 字节, 偏移 188
    glm::vec3 emissive;        // 自发光颜色, 偏移 192
    float emissiveStrength; // 自发光强度, 偏移 204
    // 总计: 208 字节 (matches GLSL std140 布局)
};

// ============================================================================
// 阴影 UBO - 阴影 pass 用的光源空间矩阵
// ============================================================================
// 阴影贴图渲染时, 需要从光源的视角渲染场景, 这个 UBO 包含:
//   - lightSpaceMatrix: lightProj * lightView, 把世界坐标变换到光源空间
//   - lightPos: 光源位置, 用于阴影计算时的光照方向
// 主渲染 pass 会采样阴影贴图, 用 lightSpaceMatrix 把像素坐标变换到光源空间,
// 比较深度值判断是否在阴影中
struct alignas(16) UBO_Shadow {
    glm::mat4 lightSpaceMatrix;  // 光源 VP 矩阵, 64 字节, 偏移 0
    glm::vec3 lightPos;          // 光源位置, 12 字节, 偏移 64
    float _pad;             // 填充, 凑齐 16 字节, 偏移 76
    // 总计: 80 字节 (必须是 16 的倍数)
};

// ============================================================================
// 渲染统计信息 - 用于 ImGui 显示
// ============================================================================
// 每帧更新的渲染统计信息,包括:
//   - 性能指标: FPS, 帧时间
//   - 渲染统计: Draw Calls, 三角形数量
//   - 资源统计: Descriptor Sets, Uniform Buffers, Textures 数量
struct RenderStats {
    // 性能指标
    float fps = 0.0f;           // 帧率 (每秒帧数)
    float frameTime = 0.0f;     // 帧时间 (毫秒)

    // 渲染统计
    uint32_t drawCalls = 0;     // 本帧 Draw Call 数量
    uint32_t triangles = 0;     // 本帧绘制的三角形数量

    // 资源统计
    uint32_t descriptorSets = 0;    // Descriptor Sets 数量
    uint32_t uniformBuffers = 0;    // Uniform Buffers 数量
    uint32_t textures = 0;          // Textures 数量

    // 重置每帧统计 (Draw Calls 和 Triangles)
    void resetPerFrame() {
        drawCalls = 0;
        triangles = 0;
    }
};
