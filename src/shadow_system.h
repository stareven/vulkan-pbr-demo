#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include "types.h"
#include "math_utils.h"

// ============================================================================
// 阴影系统 - 阴影贴图、阴影渲染通道、阴影管线
// ============================================================================
class ShadowSystem {
private:
    static constexpr uint32_t MAP_SIZE = 2048;

    VkImage shadowMapImage = VK_NULL_HANDLE;
    VkDeviceMemory shadowMapMemory = VK_NULL_HANDLE;
    VkImageView shadowMapImageView = VK_NULL_HANDLE;
    VkSampler shadowSampler = VK_NULL_HANDLE;

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    VkDescriptorSetLayout dslShadow = VK_NULL_HANDLE;
    VkDescriptorSetLayout dslShadowSampler = VK_NULL_HANDLE;
    std::vector<VkBuffer> uboShadowBuf;
    std::vector<VkDeviceMemory> uboShadowMem;
    std::vector<VkDescriptorSet> descSetsShadow;
    std::vector<VkDescriptorSet> descSetsShadowSampler;

    Mat4 lightView;
    Mat4 lightProj;

public:
    ShadowSystem() = default;
    ~ShadowSystem();

    void initialize(VkDevice device, VkPhysicalDevice physicalDevice);
    void createShadowMap(VkDevice device, VkPhysicalDevice physicalDevice);
    void createShadowRenderPass(VkDevice device);
    void createShadowPipeline(VkDevice device, const std::string& shaderDir);
    void createShadowFramebuffer(VkDevice device);
    void createShadowDescriptorLayout(VkDevice device);
    void createShadowDescriptorSets(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t imageCount);
    void createShadowSampler(VkDevice device);
    void createShadowSamplerDescriptorLayout(VkDevice device);
    void createShadowSamplerDescriptorSets(VkDevice device, uint32_t imageCount, VkImageView shadowMapView);
    void updateShadowUBO(VkDevice device, uint32_t imageIndex, const Mat4& model);
    void cleanup(VkDevice device);

    // Getters
    VkRenderPass getRenderPass() const { return renderPass; }
    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkFramebuffer getFramebuffer() const { return framebuffer; }
    VkImageView getShadowMapView() const { return shadowMapImageView; }
    VkSampler getSampler() const { return shadowSampler; }
    VkDescriptorSetLayout getShadowLayout() const { return dslShadow; }
    VkDescriptorSetLayout getSamplerLayout() const { return dslShadowSampler; }
    VkDescriptorSet getShadowSet(uint32_t index) const { return descSetsShadow[index]; }
    VkDescriptorSet getSamplerSet(uint32_t index) const { return descSetsShadowSampler[index]; }
    const Mat4& getLightView() const { return lightView; }
    const Mat4& getLightProj() const { return lightProj; }

private:
    void createShadowMapInternal(VkDevice device, VkPhysicalDevice physicalDevice);
    void createShadowRenderPassInternal(VkDevice device);
    void createShadowPipelineInternal(VkDevice device, const std::string& shaderDir);
    void createShadowFramebufferInternal(VkDevice device);
    void createShadowDescriptorLayoutInternal(VkDevice device);
    void createShadowSamplerInternal(VkDevice device);
    void createShadowSamplerDescriptorLayoutInternal(VkDevice device);
};
