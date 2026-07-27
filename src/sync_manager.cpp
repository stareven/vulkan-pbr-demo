#include "sync_manager.h"
#include "vulkan_utils.h"
#include <stdexcept>

SyncManager::SyncManager(uint32_t maxFrames)
    : maxFramesInFlight(maxFrames) {}

void SyncManager::create(uint32_t imageCount) {
    this->imageCount = imageCount;
    // 信号量按 maxFramesInFlight 分配（每次 acquire 用的是当前帧的信号量）
    imageAvailable.resize(maxFramesInFlight);
    renderFinished.resize(maxFramesInFlight);
    inFlightFences.resize(maxFramesInFlight);
    imagesInFlight.resize(imageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        if (vkCreateSemaphore(device, &sci, nullptr, &imageAvailable[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &sci, nullptr, &renderFinished[i]) != VK_SUCCESS)
            throw std::runtime_error("sync object creation failed");
    }
    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        if (vkCreateFence(device, &fci, nullptr, &inFlightFences[i]) != VK_SUCCESS)
            throw std::runtime_error("fence creation failed");
    }
}

void SyncManager::cleanup() {
    for (auto s : imageAvailable) {
        if (s) vkDestroySemaphore(device, s, nullptr);
    }
    for (auto s : renderFinished) {
        if (s) vkDestroySemaphore(device, s, nullptr);
    }
    for (auto f : inFlightFences) {
        if (f) vkDestroyFence(device, f, nullptr);
    }
    for (auto f : imagesInFlight) {
        if (f) vkDestroyFence(device, f, nullptr);
    }
    imageAvailable.clear();
    renderFinished.clear();
    inFlightFences.clear();
    imagesInFlight.clear();
}

void SyncManager::waitForFence() {
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
}

void SyncManager::resetFence() {
    vkResetFences(device, 1, &inFlightFences[currentFrame]);
}

void SyncManager::markImageInFlight(uint32_t imageIndex) {
    imagesInFlight[imageIndex] = inFlightFences[currentFrame];
}

VkSemaphore SyncManager::getImageAvailableSemaphore() const {
    return imageAvailable[currentFrame];
}

VkSemaphore SyncManager::getRenderFinishedSemaphore() const {
    return renderFinished[currentFrame];
}

VkFence SyncManager::getInFlightFence() const {
    return inFlightFences[currentFrame];
}

void SyncManager::advanceFrame() {
    currentFrame = (currentFrame + 1) % maxFramesInFlight;
}