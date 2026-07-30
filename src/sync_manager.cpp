#include "sync_manager.h"
#include "vulkan_utils.h"
#include <stdexcept>

// ----------------------------------------------------------------------------
// 构造函数: 保存 maxFramesInFlight 参数
// ----------------------------------------------------------------------------
SyncManager::SyncManager(uint32_t maxFrames)
    : maxFramesInFlight(maxFrames) {}

// ----------------------------------------------------------------------------
// 创建同步对象
// ----------------------------------------------------------------------------
// 流程:
//   1. 按 maxFramesInFlight 分配信号量和 fence
//   2. 按 imageCount 分配 imagesInFlight (初始化为 VK_NULL_HANDLE)
//   3. 创建信号量 (imageAvailable + renderFinished, 每帧各一个)
//   4. 创建 fence (每帧一个, 初始状态为 SIGNALED, 避免首次等待时死锁)
//
// 为什么 fence 初始为 SIGNALED?
//   - 第一帧开始前, CPU 要等待 fence
//   - 若 fence 初始为 UNSIGNALED, CPU 会永远等待 (因为还没有 GPU 工作完成)
//   - 设为 SIGNALED, 第一次等待立即通过, 之后由 GPU 正常 signal/reset
// ----------------------------------------------------------------------------
void SyncManager::create(uint32_t imageCount) {
    this->imageCount = imageCount;

    // 分配同步对象数组
    imageAvailable.resize(maxFramesInFlight);
    renderFinished.resize(maxFramesInFlight);
    inFlightFences.resize(maxFramesInFlight);
    imagesInFlight.resize(imageCount, VK_NULL_HANDLE);

    // 信号量创建信息 (无特殊参数)
    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    // Fence 创建信息: 初始状态为 SIGNALED
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // 关键: 避免首次等待死锁

    // 创建信号量 (每帧一对: imageAvailable + renderFinished)
    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        if (vkCreateSemaphore(device, &sci, nullptr, &imageAvailable[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &sci, nullptr, &renderFinished[i]) != VK_SUCCESS)
            throw std::runtime_error("sync object creation failed");
    }

    // 创建 fence (每帧一个)
    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        if (vkCreateFence(device, &fci, nullptr, &inFlightFences[i]) != VK_SUCCESS)
            throw std::runtime_error("fence creation failed");
    }
}

// ----------------------------------------------------------------------------
// 销毁同步对象
// ----------------------------------------------------------------------------
// 注意:
//   - imagesInFlight 只是弱引用 (别名), 不拥有所有权
//   - 上面已经销毁了 inFlightFences, 这里不能再销毁 imagesInFlight
//   - 否则会 double-free (同一个 fence 被销毁两次)
// ----------------------------------------------------------------------------
void SyncManager::cleanup() {
    // 销毁所有信号量
    for (auto s : imageAvailable) {
        if (s) vkDestroySemaphore(device, s, nullptr);
    }
    for (auto s : renderFinished) {
        if (s) vkDestroySemaphore(device, s, nullptr);
    }
    // 销毁所有 fence
    for (auto f : inFlightFences) {
        if (f) vkDestroyFence(device, f, nullptr);
    }
    // 清空数组 (imagesInFlight 只是指针, 不销毁)
    imageAvailable.clear();
    renderFinished.clear();
    inFlightFences.clear();
    imagesInFlight.clear();
}

// ----------------------------------------------------------------------------
// 等待当前帧的 fence
// ----------------------------------------------------------------------------
// 参数:
//   - VK_TRUE: 等待所有 fence (这里只有 1 个, 所以无所谓)
//   - UINT64_MAX: 无限等待 (直到 GPU 完成)
// 用途:
//   - CPU 提交第 N 帧后, 在提交第 N+maxFramesInFlight 帧前等待
//   - 避免 CPU 太快, GPU 跟不上, 导致资源被同时修改
// ----------------------------------------------------------------------------
void SyncManager::waitForFence() {
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
}

// ----------------------------------------------------------------------------
// 重置当前帧的 fence
// ----------------------------------------------------------------------------
// 等待 fence 通过后, 必须重置为 UNSIGNALED, 下次 GPU 完成时才能再次 signal
// 流程:
//   1. waitForFence: CPU 等待 GPU 完成
//   2. resetFence: 重置 fence 为 UNSIGNALED
//   3. 提交新命令: GPU 完成后 signal fence
//   4. 下次循环: 再次 waitForFence
// ----------------------------------------------------------------------------
void SyncManager::resetFence() {
    vkResetFences(device, 1, &inFlightFences[currentFrame]);
}

// ----------------------------------------------------------------------------
// 标记 Swapchain 图像正在被当前帧占用
// ----------------------------------------------------------------------------
// 参数:
//   - imageIndex: Swapchain 图像索引 (从 vkAcquireNextImageKHR 获取)
// 用途:
//   - 记录 imagesInFlight[imageIndex] = inFlightFences[currentFrame]
//   - 下次 CPU 要获取同一张图像时, 检查这个 fence 是否已完成
//   - 若未完成, 等待; 若已完成, 直接复用
// ----------------------------------------------------------------------------
void SyncManager::markImageInFlight(uint32_t imageIndex) {
    imagesInFlight[imageIndex] = inFlightFences[currentFrame];
}

// ----------------------------------------------------------------------------
// 获取当前帧的信号量/fence
// ----------------------------------------------------------------------------
// 这些函数返回当前帧 (currentFrame) 对应的同步对象:
//   - getImageAvailableSemaphore: 图像获取完成的信号量
//   - getRenderFinishedSemaphore: 渲染完成的信号量
//   - getInFlightFence: 当前帧的 fence
// ----------------------------------------------------------------------------
VkSemaphore SyncManager::getImageAvailableSemaphore() const {
    return imageAvailable[currentFrame];
}

VkSemaphore SyncManager::getRenderFinishedSemaphore() const {
    return renderFinished[currentFrame];
}

VkFence SyncManager::getInFlightFence() const {
    return inFlightFences[currentFrame];
}

// ----------------------------------------------------------------------------
// 推进到下一帧
// ----------------------------------------------------------------------------
// currentFrame = (currentFrame + 1) % maxFramesInFlight
// 循环使用帧索引: 0 -> 1 -> 0 -> 1 -> ...
// ----------------------------------------------------------------------------
void SyncManager::advanceFrame() {
    currentFrame = (currentFrame + 1) % maxFramesInFlight;
}
