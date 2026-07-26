#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

// ============================================================================
// 描述符管理 - 描述符布局、池、集合
// ============================================================================
class DescriptorManager {
private:
    VkDescriptorSetLayout dslMVP = VK_NULL_HANDLE;
    VkDescriptorSetLayout dslMat = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> setsMVP;
    std::vector<VkDescriptorSet> setsMat;

public:
    DescriptorManager() = default;
    ~DescriptorManager();

    void createLayouts(VkDevice device);
    void createPool(VkDevice device, uint32_t imageCount);
    void allocateSets(VkDevice device, uint32_t imageCount);
    void updateSets(VkDevice device, uint32_t imageIndex, VkBuffer mvpBuffer, VkBuffer matBuffer);
    void cleanup(VkDevice device);

    VkDescriptorSetLayout getMVPLayout() const { return dslMVP; }
    VkDescriptorSetLayout getMaterialLayout() const { return dslMat; }
    VkDescriptorSet getMVPSet(uint32_t index) const { return setsMVP[index]; }
    VkDescriptorSet getMaterialSet(uint32_t index) const { return setsMat[index]; }

private:
    void createMVPLayout(VkDevice device);
    void createMaterialLayout(VkDevice device);
};
