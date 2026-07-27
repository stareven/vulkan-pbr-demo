#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

// ============================================================================
// 同步管理 - 信号量和栅栏
// ============================================================================
class SyncManager {
private:
    // 按 maxFramesInFlight 分配：每帧在飞有自己的信号量 + fence
    std::vector<VkSemaphore> imageAvailable;
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkFence> inFlightFences;
    // 按 imageCount 分配：追踪每个 swapchain image 当前正在被哪个 fence 占用
    std::vector<VkFence> imagesInFlight;
    uint32_t currentFrame = 0;
    uint32_t maxFramesInFlight;
    uint32_t imageCount = 0;

public:
    SyncManager(uint32_t maxFrames = 2);

    void create(VkDevice device, uint32_t imageCount);
    void cleanup(VkDevice device);

    void waitForFence(VkDevice device);
    void resetFence(VkDevice device);
    void markImageInFlight(uint32_t imageIndex);

    VkSemaphore getImageAvailableSemaphore() const;
    VkSemaphore getRenderFinishedSemaphore() const;
    VkFence getInFlightFence() const;

    uint32_t getCurrentFrame() const { return currentFrame; }
    uint32_t getImageCount() const { return imageCount; }
    void advanceFrame();

private:
    void createSyncObjects(VkDevice device);
};
