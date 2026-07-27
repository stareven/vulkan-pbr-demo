#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

// ============================================================================
// 渲染管线 - 渲染通道、管线布局、图形管线、帧缓冲
// ============================================================================
class RenderPipeline {
private:
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;

public:
    RenderPipeline() = default;
    ~RenderPipeline();

    void createRenderPass(VkDevice device, VkFormat swapchainFormat);
    void createPipelineLayout(VkDevice device, VkDescriptorSetLayout mvpLayout,
                             VkDescriptorSetLayout materialLayout,
                             VkDescriptorSetLayout shadowSamplerLayout);
    void createGraphicsPipeline(VkDevice device, VkExtent2D extent, const std::string& shaderDir);
    void createFramebuffers(VkDevice device, VkExtent2D extent,
                           const std::vector<VkImageView>& swapchainViews,
                           VkImageView depthView);
    void cleanup(VkDevice device);

    VkRenderPass getRenderPass() const { return renderPass; }
    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    const std::vector<VkFramebuffer>& getFramebuffers() const { return framebuffers; }

private:
    void createRenderPassInternal(VkDevice device, VkFormat swapchainFormat);
    void createGraphicsPipelineInternal(VkDevice device, VkExtent2D extent,
                                       const std::string& vertexShaderPath,
                                       const std::string& fragmentShaderPath);
};
