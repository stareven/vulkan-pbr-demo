#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include <string>
#include "types.h"

// ============================================================================
// 渲染管线 - 渲染通道、管线布局、图形管线、帧缓冲
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

    VkRenderPass getRenderPass() const { return renderPass; }
    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    const std::vector<VkFramebuffer>& getFramebuffers() const { return framebuffers; }
};