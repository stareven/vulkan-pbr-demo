#pragma once

// GLFW 必须在使用 Vulkan 头文件之前包含，GLFW_INCLUDE_VULKAN 宏使 GLFW 自动包含 vulkan.h
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>
#include <vector>
#include "types.h"
#include "math_utils.h"

// ============================================================================
// 阴影系统 - 基于阴影贴图（Shadow Map）的实时阴影渲染
// ============================================================================
//
// 核心概念：
//   1. 阴影贴图（Shadow Map）: 从光源视角渲染场景，记录每个像素的深度值到一张深度纹理中。
//      在主渲染 pass 时，将片段位置变换到光源空间，比较其深度与阴影贴图中的深度，
//      如果当前片段深度大于阴影贴图中的深度，说明该片段被遮挡，处于阴影中。
//
//   2. 深度优先渲染（Depth-only Rendering）: 阴影 pass 只写入深度缓冲，不输出颜色。
//      这样可以节省带宽，只需要一个深度附件（depth attachment）和一个帧缓冲。
//
//   3. 深度偏移（Depth Bias）: 由于浮点数精度限制和自遮挡问题，阴影贴图中的深度值
//      可能与主 pass 中的深度值产生微小差异，导致"阴影痤疮"（shadow acne）——表面上出现
//      不该有的条纹状阴影。通过在阴影 pass 中给深度值添加一个小的偏移量（bias）来缓解。
//
//   4. PCF 采样（Percentage-Closer Filtering）: 对阴影贴图进行多次采样并取平均值，
//      可以产生柔和的阴影边缘。本实现使用简单的单次采样，可通过扩展为 PCF。
//
// 阴影管线流程：
//   Step 1: 从光源位置渲染场景 → 生成深度图（shadow map）
//           - 光源位置作为相机，构建 view matrix 和 projection matrix
//           - 只渲染需要投射阴影的物体（本例中是球体）
//           - 深度值存储到 2048x2048 的 D32_SFLOAT 格式纹理中
//
//   Step 2: 主渲染 pass 中采样阴影贴图
//           - 在片段着色器中，将顶点位置变换到光源的裁剪空间（light space）
//           - 使用比较采样器（comparison sampler）读取 shadow map
//           - 如果 fragment 深度 > shadow map 深度 → 处于阴影中，减弱光照
//           - 否则 → 处于光照中，正常计算 PBR 光照
//
// Vulkan 关键设置：
//   - Depth Bias: rs.depthBiasEnable = VK_TRUE, depthBiasConstantFactor, depthBiasSlopeFactor
//     控制深度偏移量，用于消除阴影痤疮
//   - Comparison Sampler: si.compareEnable = VK_TRUE, compareOp = VK_COMPARE_OP_LESS_OR_EQUAL
//     硬件自动比较深度值，返回 0 或 1，可用于 PCF 滤波
//   - Render Pass Dependencies: subpass dependency 确保 shadow map 写入完成后才能被主 pass 读取
//     srcStageMask = EARLY/LATE_FRAGMENT_TESTS, dstStageMask = FRAGMENT_SHADER
//     srcAccessMask = DEPTH_STENCIL_ATTACHMENT_WRITE, dstAccessMask = SHADER_READ
class ShadowSystem {
private:
    // 阴影贴图分辨率（2048x2048），越高分辨率阴影越清晰但内存占用越大
    static constexpr uint32_t MAP_SIZE = 2048;

    VkDevice device = VK_NULL_HANDLE;        // Vulkan 逻辑设备句柄
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;  // Vulkan 物理设备句柄

    // --- 阴影贴图资源（深度纹理）---
    VkImage shadowMapImage = VK_NULL_HANDLE;         // 2D 深度图像，格式 D32_SFLOAT
    VkDeviceMemory shadowMapMemory = VK_NULL_HANDLE; // 设备本地内存，绑定到 shadowMapImage
    VkImageView shadowMapImageView = VK_NULL_HANDLE; // 图像视图，用于在着色器中采样
    VkSampler shadowSampler = VK_NULL_HANDLE;        // 采样器，用于主 pass 采样阴影贴图

    // --- 阴影渲染 pass 资源 ---
    VkFramebuffer framebuffer = VK_NULL_HANDLE;  // 帧缓冲，只包含阴影贴图作为深度附件
    VkRenderPass renderPass = VK_NULL_HANDLE;    // 渲染 pass，配置为深度-only 渲染
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;  // 管线布局，描述 descriptor set 布局
    VkPipeline pipeline = VK_NULL_HANDLE;              // 图形管线，包含顶点着色器和光栅化状态

    // --- Descriptor Sets ---
    VkDescriptorSetLayout dslShadow = VK_NULL_HANDLE;          // UBO descriptor 布局（顶点着色器用）
    VkDescriptorSetLayout dslShadowSampler = VK_NULL_HANDLE;   // 采样器 descriptor 布局（片段着色器用）
    std::vector<VkBuffer> uboShadowBuf;                        // Uniform 缓冲区数组（每帧一个）
    std::vector<VkDeviceMemory> uboShadowMem;                  // UBO 对应的设备内存
    std::vector<VkDescriptorSet> descSetsShadow;               // UBO descriptor sets（每帧一个）
    std::vector<VkDescriptorSet> descSetsShadowSampler;        // 采样器 descriptor sets（每帧一个）

    // --- 光源视角矩阵 ---
    glm::mat4 lightView;   // 从光源位置看向场景原点的 view matrix
    glm::mat4 lightProj;   // 正交投影矩阵（方向光），近裁剪面 0.1，远裁剪面 50

public:
    ShadowSystem() = default;
    ~ShadowSystem();

    // 完整初始化：按正确顺序创建所有阴影系统资源
    // imageCount = swapchain 图像数量，用于分配对应数量的 shadow sampler descriptor sets
    void initialize(VkDevice dev, VkPhysicalDevice pd,
                    const std::string& shaderDir, uint32_t imageCount);

    // 更新阴影 UBO：计算光源的 lightSpaceMatrix（proj * view）和光源位置
    // model 参数当前未使用，保留用于未来扩展
    void updateShadowUBO(uint32_t imageIndex, const glm::mat4& model);

    // 清理所有 Vulkan 资源（按依赖逆序销毁）
    void cleanup();

    // 录制阴影 pass 命令：将阴影 pass 的绘制命令编码到 command buffer
    // cmd: 命令缓冲
    // frameIdx: 当前帧索引，用于选择对应的 descriptor set
    // sphereVbo/sphereIbo: 球体的顶点/索引缓冲
    // sphereIndexCount: 球体的索引数量
    void recordShadowPass(VkCommandBuffer cmd, uint32_t frameIdx,
                          VkBuffer sphereVbo, VkBuffer sphereIbo, uint32_t sphereIndexCount) const;

    // Getters - 获取内部 Vulkan 资源句柄，供其他系统使用
    VkRenderPass getRenderPass() const { return renderPass; }
    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkFramebuffer getFramebuffer() const { return framebuffer; }
    VkImageView getShadowMapView() const { return shadowMapImageView; }
    VkDescriptorSetLayout getSamplerLayout() const { return dslShadowSampler; }
    VkDescriptorSet getShadowSet(uint32_t index) const { return descSetsShadow[index]; }
    VkDescriptorSet getSamplerSet(uint32_t index) const { return descSetsShadowSampler[index]; }
    const glm::mat4& getLightView() const { return lightView; }
    const glm::mat4& getLightProj() const { return lightProj; }

private:
    // 以下是内部资源创建方法，由 initialize() 按顺序调用
    void createShadowMap(VkPhysicalDevice physicalDevice);        // 创建深度纹理图像和视图
    void createShadowRenderPass();                                // 创建深度-only 渲染 pass
    void createShadowPipeline(const std::string& shaderDir);      // 创建阴影管线（含 depth bias）
    void createShadowFramebuffer();                               // 创建阴影帧缓冲
    void createShadowDescriptorLayout();                          // 创建 UBO descriptor 布局
    void createShadowDescriptorSets(VkPhysicalDevice physicalDevice, uint32_t imageCount);  // 分配 UBO descriptors
    void createShadowSampler();                                   // 创建阴影采样器
    void createShadowSamplerDescriptorLayout();                   // 创建采样器 descriptor 布局
    void createShadowSamplerDescriptorSets(uint32_t imageCount, VkImageView shadowMapView); // 分配采样器 descriptors
};
