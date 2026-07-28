#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

// ============================================================================
// 描述符管理 - 描述符布局、池、集合
// ============================================================================
class DescriptorManager {
private:
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout dslMVP = VK_NULL_HANDLE;
    VkDescriptorSetLayout dslMat = VK_NULL_HANDLE;       // 球体材质布局
    VkDescriptorSetLayout dslMatGround = VK_NULL_HANDLE; // 地面材质布局
    VkDescriptorPool pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> setsMVP;
    std::vector<VkDescriptorSet> setsMat;
    std::vector<VkDescriptorSet> setsMatGround;

public:
    DescriptorManager() = default;
    ~DescriptorManager();

    void init(VkDevice dev) { device = dev; }
    void createLayouts();
    void createPool(uint32_t imageCount);
    void allocateSets(uint32_t imageCount);
    void updateSets(uint32_t imageIndex, VkBuffer mvpBuffer, VkBuffer matBuffer);
    void updateGroundSets(uint32_t imageIndex, VkBuffer matGroundBuffer);
    void cleanup();

    VkDescriptorSetLayout getMVPLayout() const { return dslMVP; }
    VkDescriptorSetLayout getMaterialLayout() const { return dslMat; }
    VkDescriptorSetLayout getMaterialGroundLayout() const { return dslMatGround; }
    VkDescriptorSet getMVPSet(uint32_t index) const { return setsMVP[index]; }
    VkDescriptorSet getMaterialSet(uint32_t index) const { return setsMat[index]; }
    VkDescriptorSet getMaterialGroundSet(uint32_t index) const { return setsMatGround[index]; }
};