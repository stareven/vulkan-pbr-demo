#pragma once

// ----------------------------------------------------------------------------
// GLFW 与 Vulkan 头文件
// ----------------------------------------------------------------------------
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

// ============================================================================
// 描述符管理 - Shader 资源绑定
// ============================================================================
// Vulkan 不允许 Shader 直接访问 CPU 内存, 必须通过"描述符"绑定 GPU 资源:
//   - 统一缓冲 (UBO): Shader 的只读常量 (变换矩阵/材质参数等)
//   - 纹理采样器: Shader 读取的纹理图像
//   - 存储缓冲 (SSBO): Shader 可读写的大 buffer (本 demo 未用)
//
// 描述符系统的三层结构:
//   1. 描述符集布局 (DescriptorSetLayout):
//      - 定义 Shader 需要哪些资源 (binding 0/1/2...)
//      - 类似函数签名, 声明参数类型但不绑定具体值
//   2. 描述符池 (DescriptorPool):
//      - 描述符的"内存池", 从中分配描述符集
//      - 预先分配一定数量的各类型描述符
//   3. 描述符集 (DescriptorSet):
//      - 实际的资源绑定, 把 Buffer/Image 绑到 Shader 的 binding
//      - 类似函数调用的参数, 绑定具体值
//
// 本 Demo 的描述符布局:
//   - Set 0 (MVP): vertex shader 的变换矩阵 (model/view/proj)
//   - Set 1 (Material): fragment shader 的材质参数 (albedo/metallic/roughness)
//   - Set 1 (MaterialGround): 地面的材质参数 (独立 set, 避免切换布局)
//
// 关键概念:
//   - Binding: Shader 中的资源槽位 (layout(binding=0) uniform UBO ubo;)
//   - Descriptor Type: 资源类型 (UNIFORM_BUFFER/COMBINED_IMAGE_SAMPLER 等)
//   - Stage Flags: Shader 阶段 (VERTEX/FRAGMENT)
// ============================================================================
class DescriptorManager {
private:
    // 逻辑设备, 用于创建/销毁描述符对象
    VkDevice device = VK_NULL_HANDLE;

    // 描述符集布局:
    //   - dslMVP: MVP 矩阵的布局 (vertex shader, binding 0)
    //   - dslMat: 球体材质的布局 (fragment shader, binding 0)
    //   - dslMatGround: 地面材质的布局 (fragment shader, binding 0)
    // 布局是 Pipeline 创建的一部分, 定义 Shader 需要什么资源
    VkDescriptorSetLayout dslMVP = VK_NULL_HANDLE;
    VkDescriptorSetLayout dslMat = VK_NULL_HANDLE;       // 球体材质布局
    VkDescriptorSetLayout dslMatGround = VK_NULL_HANDLE; // 地面材质布局

    // 描述符池: 描述符集的"内存池"
    //   - 预先分配一定数量的 UNIFORM_BUFFER 和 COMBINED_IMAGE_SAMPLER
    //   - 从中分配描述符集, 避免频繁申请/释放
    VkDescriptorPool pool = VK_NULL_HANDLE;

    // 描述符集 (每帧一个, 数量 = imageCount):
    //   - setsMVP: 每帧的 MVP 矩阵绑定
    //   - setsMat: 每帧的球体材质绑定
    //   - setsMatGround: 每帧的地面材质绑定
    // 每帧独立一套, 避免 CPU 更新时 GPU 还在读 (数据竞争)
    std::vector<VkDescriptorSet> setsMVP;
    std::vector<VkDescriptorSet> setsMat;
    std::vector<VkDescriptorSet> setsMatGround;

public:
    // 默认构造: 所有句柄初始化为 VK_NULL_HANDLE
    DescriptorManager() = default;

    // 析构: cleanup 必须显式调用
    ~DescriptorManager();

    // 注入逻辑设备
    void init(VkDevice dev) { device = dev; }

    // 创建描述符集布局:
    //   - dslMVP: UNIFORM_BUFFER, vertex shader, binding 0
    //   - dslMat: UNIFORM_BUFFER, fragment shader, binding 0
    //   - dslMatGround: UNIFORM_BUFFER, fragment shader, binding 0
    void createLayouts();

    // 创建描述符池:
    //   - imageCount: Swapchain 图像数量
    //   - 预分配 imageCount * 3 个 UNIFORM_BUFFER (MVP + Mat + MatGround)
    //   - 预分配 imageCount 个 COMBINED_IMAGE_SAMPLER (预留, 当前未用)
    void createPool(uint32_t imageCount);

    // 分配描述符集:
    //   - imageCount: Swapchain 图像数量
    //   - 从池中分配 setsMVP/setsMat/setsMatGround, 每帧各一个
    void allocateSets(uint32_t imageCount);

    // 更新球体的描述符集:
    //   - imageIndex: 当前帧索引
    //   - mvpBuffer: MVP 矩阵的 uniform buffer
    //   - matBuffer: 球体材质的 uniform buffer
    //   - 把 buffer 绑到 setsMVP[imageIndex] 和 setsMat[imageIndex]
    void updateSets(uint32_t imageIndex, VkBuffer mvpBuffer, VkBuffer matBuffer);

    // 更新地面的描述符集:
    //   - imageIndex: 当前帧索引
    //   - matGroundBuffer: 地面材质的 uniform buffer
    //   - 把 buffer 绑到 setsMatGround[imageIndex]
    void updateGroundSets(uint32_t imageIndex, VkBuffer matGroundBuffer);

    // 销毁所有描述符对象
    void cleanup();

    // ---------- Getters ----------

    // 获取描述符集布局 (Pipeline 创建时需要)
    VkDescriptorSetLayout getMVPLayout() const { return dslMVP; }
    VkDescriptorSetLayout getMaterialLayout() const { return dslMat; }
    VkDescriptorSetLayout getMaterialGroundLayout() const { return dslMatGround; }

    // 获取描述符集 (渲染时绑定)
    VkDescriptorSet getMVPSet(uint32_t index) const { return setsMVP[index]; }
    VkDescriptorSet getMaterialSet(uint32_t index) const { return setsMat[index]; }
    VkDescriptorSet getMaterialGroundSet(uint32_t index) const { return setsMatGround[index]; }
};
