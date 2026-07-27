#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

// ============================================================================
// Swapchain 管理 - Swapchain、图像视图、深度缓冲
// ============================================================================
class SwapchainManager {
private:
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format;
    VkExtent2D extent;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;

    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

public:
    SwapchainManager() = default;
    ~SwapchainManager();

    void init(VkDevice dev, VkPhysicalDevice pd) { device = dev; physicalDevice = pd; }
    void create(VkSurfaceKHR surface, GLFWwindow* window);
    void recreate(VkSurfaceKHR surface, GLFWwindow* window);
    void cleanup();

    VkSwapchainKHR getSwapchain() const { return swapchain; }
    VkFormat getFormat() const { return format; }
    VkExtent2D getExtent() const { return extent; }
    const std::vector<VkImageView>& getImageViews() const { return imageViews; }
    VkImageView getDepthImageView() const { return depthImageView; }
    uint32_t getImageCount() const { return (uint32_t)images.size(); }

private:
    void createSwapchainInternal(VkSurfaceKHR surface, GLFWwindow* window);
    void createImageViews();
    void createDepthBuffer();
};