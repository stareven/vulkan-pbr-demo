#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include <string>
#include "types.h"

// ============================================================================
// 渲染管线 - 渲染通道、管线布局、图形管线、帧缓冲、主 pass 录制
// ============================================================================
class RenderPipeline {
private:
    VkDevice device = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;

public:
    RenderPipeline() = default;
    ~RenderPipeline();

    void init(VkDevice dev) { device = dev; }

    void createRenderPass(VkFormat swapchainFormat);
    void createPipelineLayout(VkDescriptorSetLayout mvpLayout,
                             VkDescriptorSetLayout materialLayout,
                             VkDescriptorSetLayout shadowSamplerLayout);
    void createGraphicsPipeline(VkExtent2D extent, const std::string& shaderDir);
    void createFramebuffers(VkExtent2D extent,
                           const std::vector<VkImageView>& swapchainViews,
                           VkImageView depthView);
    void cleanup();

    // 录制主 pass（PBRApp 从各 Manager 取得句柄传进来，避免 RenderPipeline 依赖其他 Manager）
    void recordMainPass(VkCommandBuffer cmd, uint32_t imgIdx, VkExtent2D extent,
                        const std::vector<VkDescriptorSet>& descSets,
                        VkBuffer sphereVbo, VkBuffer sphereIbo, uint32_t sphereIndexCount,
                        VkBuffer planeVbo, VkBuffer planeIbo, uint32_t planeIndexCount) const;

    VkRenderPass getRenderPass() const { return renderPass; }
    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    const std::vector<VkFramebuffer>& getFramebuffers() const { return framebuffers; }
};