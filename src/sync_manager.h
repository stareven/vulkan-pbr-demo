#pragma once

// ----------------------------------------------------------------------------
// GLFW 与 Vulkan 头文件
// ----------------------------------------------------------------------------
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

// ============================================================================
// 同步管理 - GPU/CPU 同步机制
// ============================================================================
// Vulkan 是异步 API: CPU 提交命令后 GPU 异步执行, 两者独立运行
// 必须用同步原语协调, 避免:
//   - CPU 修改数据时 GPU 还在读 (数据竞争)
//   - CPU 提交第 N+1 帧时 GPU 还在渲染第 N 帧 (流水线冲突)
//   - 呈现图像时 GPU 还没渲染完 (画面撕裂)
//
// 同步原语:
//   - VkSemaphore: GPU 内部同步 (队列间/命令间), CPU 不可等待
//     * imageAvailable: Swapchain 图像获取完成, 可以开始渲染
//     * renderFinished: 渲染完成, 可以呈现
//   - VkFence: GPU->CPU 同步, CPU 可以等待
//     * inFlightFences: 追踪每帧的渲染状态, CPU 等待完成后再复用资源
//     * imagesInFlight: 追踪每个 Swapchain 图像正在被哪帧占用
//
// 流水线模型 (maxFramesInFlight = 2):
//   帧 0: CPU 记录命令 -> GPU 渲染 -> 呈现
//   帧 1: CPU 记录命令 -> GPU 渲染 -> 呈现 (与帧 0 重叠)
//   帧 0: CPU 等待 fence -> 复用资源 -> 记录新命令
//
// 关键概念:
//   - MAX_FRAMES_IN_FLIGHT: 同时渲染的帧数 (2 = 双缓冲流水线)
//   - Fence: 栅障, CPU 等待 GPU 完成某帧后重置
//   - Semaphore: 信号量, GPU 内部同步, 无需 CPU 干预
// ============================================================================
class SyncManager {
private:
    // 逻辑设备, 用于创建/销毁同步对象
    VkDevice device = VK_NULL_HANDLE;

    // 按 maxFramesInFlight 分配的同步对象:
    //   - imageAvailable: 每帧一个信号量, 表示 Swapchain 图像已获取, 可以开始渲染
    //   - renderFinished: 每帧一个信号量, 表示渲染完成, 可以呈现
    //   - inFlightFences: 每帧一个栅障, CPU 等待该帧 GPU 完成后重置
    // 为什么按帧数分配 (而不是图像数)?
    //   - 每帧用固定的信号量/fence, 避免每次重新创建
    //   - 帧循环使用, 第 0 帧用 imageAvailable[0], 第 1 帧用 [1], 第 2 帧又用 [0]
    std::vector<VkSemaphore> imageAvailable;
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkFence> inFlightFences;

    // 按 Swapchain 图像数分配的追踪数组:
    //   - imagesInFlight[i]: 记录 Swapchain 图像 i 当前正在被哪个 fence 占用
    //   - 用途: 当 CPU 要获取图像 i 时, 检查 imagesInFlight[i] 是否已完成
    //   - 若未完成, 等待该 fence; 若已完成, 直接复用
    //   - 这是弱引用 (别名), 不拥有所有权, cleanup 时不销毁
    std::vector<VkFence> imagesInFlight;

    // 当前帧索引 (0 或 1, 循环使用)
    uint32_t currentFrame = 0;

    // 最大同时渲染帧数 (通常是 2)
    uint32_t maxFramesInFlight;

    // Swapchain 图像数量
    uint32_t imageCount = 0;

public:
    // 构造函数: 保存 maxFrames 参数
    SyncManager(uint32_t maxFrames = 2);

    // 注入逻辑设备
    void init(VkDevice dev) { device = dev; }

    // 创建同步对象:
    //   - imageCount: Swapchain 图像数量, 用于分配 imagesInFlight
    //   - 创建信号量和 fence (按 maxFramesInFlight 数量)
    void create(uint32_t imageCount);

    // 销毁所有同步对象
    void cleanup();

    // 等待当前帧的 fence (CPU 阻塞直到 GPU 完成)
    void waitForFence();

    // 重置当前帧的 fence (准备下一帧使用)
    void resetFence();

    // 标记某个 Swapchain 图像正在被当前帧占用:
    //   - imageIndex: Swapchain 图像索引
    //   - 记录到 imagesInFlight[imageIndex] = inFlightFences[currentFrame]
    void markImageInFlight(uint32_t imageIndex);

    // 获取当前帧的信号量/fence:
    VkSemaphore getImageAvailableSemaphore() const;
    VkSemaphore getRenderFinishedSemaphore() const;
    VkFence getInFlightFence() const;

    // 获取当前帧索引
    uint32_t getCurrentFrame() const { return currentFrame; }

    // 获取 Swapchain 图像数量
    uint32_t getImageCount() const { return imageCount; }

    // 推进到下一帧 (currentFrame = (currentFrame + 1) % maxFramesInFlight)
    void advanceFrame();
};
