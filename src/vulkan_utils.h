#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <optional>
#include <set>
#include <vector>

// ============================================================================
// 文件读取
// ============================================================================
std::vector<char> readFile(const std::string& path);

// ============================================================================
// Validation Layer
// ============================================================================
bool checkLayerSupport(const std::vector<const char*>& validationLayers);

// ============================================================================
// Shader Module
// ============================================================================
VkShaderModule makeShaderModule(VkDevice dev, const std::vector<char>& code);

// ============================================================================
// Queue Families
// ============================================================================
struct QueueFamilies {
    std::optional<uint32_t> gfx, present;
    bool complete() const { return gfx && present; }
};

QueueFamilies findQueues(VkPhysicalDevice pd, VkSurfaceKHR surf);

// ============================================================================
// Buffer 辅助
// ============================================================================
VkDeviceSize alignUp(VkDeviceSize sz, VkDeviceSize align);

void createBuffer(VkDevice dev, VkPhysicalDevice pd,
                  VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags memProp,
                  VkBuffer& buf, VkDeviceMemory& mem);

void copyBuffer(VkDevice dev, VkQueue queue, VkCommandPool pool,
                VkBuffer src, VkBuffer dst, VkDeviceSize size);

// ============================================================================
// 内存类型查找
// ============================================================================
uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t typeFilter, VkMemoryPropertyFlags properties);

// ============================================================================
// Shader Module 创建（简化版）
// ============================================================================
VkShaderModule createShaderModule(VkDevice dev, const std::vector<char>& code);
