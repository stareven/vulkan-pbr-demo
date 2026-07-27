#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <optional>
#include <string>
#include <vector>

// ============================================================================
// Vulkan 工具函数
// ============================================================================
namespace vulkan {

std::vector<char> readFile(const std::string& path);
bool checkLayerSupport(const std::vector<const char*>& validationLayers);
VkShaderModule createShaderModule(VkDevice dev, const std::vector<char>& code);

struct QueueFamilies {
    std::optional<uint32_t> gfx, present;
    bool complete() const { return gfx && present; }
};

QueueFamilies findQueues(VkPhysicalDevice pd, VkSurfaceKHR surf);

void createBuffer(VkDevice dev, VkPhysicalDevice pd,
                  VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags memProp,
                  VkBuffer& buf, VkDeviceMemory& mem);

void copyBuffer(VkDevice dev, VkQueue queue, VkCommandPool pool,
                VkBuffer src, VkBuffer dst, VkDeviceSize size);

} // namespace vulkan
