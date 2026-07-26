#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

// ============================================================================
// 命令管理 - 命令池和命令缓冲
// ============================================================================
class CommandManager {
private:
    VkCommandPool pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> buffers;
    VkCommandBuffer shadowCmdBuffer = VK_NULL_HANDLE;

public:
    CommandManager() = default;
    ~CommandManager();

    void createPool(VkDevice device, uint32_t graphicsFamily);
    void allocateBuffers(VkDevice device, uint32_t count);
    void allocateShadowCommandBuffer(VkDevice device);
    void cleanup(VkDevice device);

    VkCommandPool getPool() const { return pool; }
    VkCommandBuffer getBuffer(uint32_t index) const { return buffers[index]; }
    VkCommandBuffer getShadowCommandBuffer() const { return shadowCmdBuffer; }

private:
    void createCommandPoolInternal(VkDevice device, uint32_t graphicsFamily);
};
