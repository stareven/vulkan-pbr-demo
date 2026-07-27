#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

// ============================================================================
// 命令管理 - 命令池和命令缓冲
// ============================================================================
class CommandManager {
private:
    VkDevice device = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> buffers;
    VkCommandBuffer shadowCmdBuffer = VK_NULL_HANDLE;

public:
    CommandManager() = default;
    ~CommandManager();

    void init(VkDevice dev) { device = dev; }
    void createPool(uint32_t graphicsFamily);
    void allocateBuffers(uint32_t count);
    void allocateShadowCommandBuffer();
    void cleanup();

    VkCommandPool getPool() const { return pool; }
    VkCommandBuffer getBuffer(uint32_t index) const { return buffers[index]; }
    VkCommandBuffer getShadowCommandBuffer() const { return shadowCmdBuffer; }
};