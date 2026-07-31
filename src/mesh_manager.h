#pragma once

// ----------------------------------------------------------------------------
// GLFW 与 Vulkan 头文件
// ----------------------------------------------------------------------------
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include "types.h"

// ============================================================================
// 网格管理 - 顶点/索引/Uniform 缓冲
// ============================================================================
// MeshManager 管理所有几何体的 GPU 缓冲:
//   1. 顶点缓冲 (VBO): 存储顶点数据 (位置/法向量/UV)
//   2. 索引缓冲 (IBO): 存储三角形索引
//   3. Uniform 缓冲 (UBO): 存储 Shader 常量 (MVP 矩阵/材质参数)
//
// 管理的网格:
//   - 球体 (Sphere): 主网格, 32x64 细分, 用于展示 PBR 材质
//   - 地面 (Plane): 10x10 平面, 接收阴影, 固定材质
//
// Uniform 缓冲 (每帧独立, 数量 = imageCount):
//   - UBO_MVP: MVP 矩阵 + 光源空间矩阵 + 相机位置
//   - UBO_Material (球体): 材质参数 (albedo/metallic/roughness 等)
//   - UBO_Material (地面): 固定材质 (混凝土), 不受 M/G/F 按键影响
//
// 关键概念:
//   - Staging Buffer: CPU 可见内存, 用于上传数据到 GPU
//   - Device Local Buffer: GPU 显存, 渲染时快速访问
//   - 流程: CPU -> Staging -> (GPU copy) -> Device Local
// ============================================================================
class MeshManager {
private:
    // 逻辑设备和物理设备 (从 VulkanContext 注入)
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

    // ---- 球体网格 (主网格) ----

    // 顶点缓冲 (VBO): 存储球体的顶点数据 (位置/法向量/UV)
    // 放在 GPU 显存 (DEVICE_LOCAL), 渲染时快速访问
    VkBuffer vbo = VK_NULL_HANDLE;

    // 索引缓冲 (IBO): 存储球体的三角形索引
    VkBuffer ibo = VK_NULL_HANDLE;

    // VBO 和 IBO 的 GPU 内存
    VkDeviceMemory vboMem = VK_NULL_HANDLE;
    VkDeviceMemory iboMem = VK_NULL_HANDLE;

    // 球体索引数量 (用于 vkCmdDrawIndexed)
    uint32_t indexCount = 0;

    // ---- 地面网格 ----

    // 地面的 VBO/IBO 和内存
    VkBuffer planeVbo = VK_NULL_HANDLE;
    VkBuffer planeIbo = VK_NULL_HANDLE;
    VkDeviceMemory planeVboMem = VK_NULL_HANDLE;
    VkDeviceMemory planeIboMem = VK_NULL_HANDLE;

    // 地面索引数量
    uint32_t planeIndexCount = 0;

    // ---- Uniform 缓冲 (每帧独立) ----

    // MVP UBO: 每帧一个, 存储变换矩阵
    std::vector<VkBuffer> uboMVPBuf;
    std::vector<VkDeviceMemory> uboMVPMem;

    // 球体材质 UBO: 每帧一个, 存储 PBR 材质参数
    std::vector<VkBuffer> uboMatBuf;       // 球体材质
    std::vector<VkDeviceMemory> uboMatMem;

    // 地面材质 UBO: 每帧一个, 固定材质 (不受 M/G/F 影响)
    std::vector<VkBuffer> uboMatGroundBuf; // 地面材质（固定，不受 M/G/F 影响）
    std::vector<VkDeviceMemory> uboMatGroundMem;

public:
    // 默认构造: 所有句柄初始化为 VK_NULL_HANDLE
    MeshManager() = default;

    // 析构: cleanup 必须显式调用
    ~MeshManager();

    // 注入逻辑设备和物理设备
    void init(VkDevice dev, VkPhysicalDevice pd) { device = dev; physicalDevice = pd; }

    // 创建网格 (球体 + 地面):
    //   - graphicsQueue: 图形队列 (用于拷贝命令)
    //   - cmdPool: 命令池 (用于分配临时命令缓冲)
    //   - 流程: 生成顶点/索引 -> 上传到 staging buffer -> 拷贝到 device local
    void createMeshes(VkQueue graphicsQueue, VkCommandPool cmdPool);

    // 创建 Uniform 缓冲:
    //   - imageCount: Swapchain 图像数量 (每帧独立一套)
    //   - 创建 MVP UBO + 球体材质 UBO + 地面材质 UBO
    //   - 地面材质立即初始化 (固定混凝土), 球体材质每帧更新
    void createUniformBuffers(size_t imageCount);

    // 更新 Uniform 缓冲 (每帧调用):
    //   - imageIndex: 当前帧索引
    //   - model/view/proj: MVP 矩阵
    //   - lightSpaceMatrix: 光源空间矩阵 (用于阴影)
    //   - cameraPos: 相机位置
    //   - materialPreset: 材质预设索引
    //   - glassEnabled: 玻璃模式开关
    //   - emissiveEnabled: 自发光开关
    void updateUniformBuffers(uint32_t imageIndex, const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj,
                             const glm::mat4& lightSpaceMatrix, const glm::vec3& cameraPos,
                             int materialPreset, bool glassEnabled, bool emissiveEnabled);

    // 销毁所有缓冲
    void cleanup();

    // ---------- Getters ----------

    // 球体网格
    VkBuffer getSphereVBO() const { return vbo; }
    VkBuffer getSphereIBO() const { return ibo; }
    uint32_t getSphereIndexCount() const { return indexCount; }

    // 地面网格
    VkBuffer getPlaneVBO() const { return planeVbo; }
    VkBuffer getPlaneIBO() const { return planeIbo; }
    uint32_t getPlaneIndexCount() const { return planeIndexCount; }

    // Uniform 缓冲 (按帧索引获取)
    VkBuffer getMVPBuffer(uint32_t index) const { return uboMVPBuf[index]; }
    VkBuffer getMaterialBuffer(uint32_t index) const { return uboMatBuf[index]; }
    VkBuffer getMaterialGroundBuffer(uint32_t index) const { return uboMatGroundBuf[index]; }
};
