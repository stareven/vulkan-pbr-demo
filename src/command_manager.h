#pragma once

// ----------------------------------------------------------------------------
// GLFW 与 Vulkan 头文件
// ----------------------------------------------------------------------------
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

// ============================================================================
// 命令管理 - 命令池和命令缓冲
// ============================================================================
// Vulkan 的命令 (绘制/拷贝/屏障等) 不能直接调用, 必须记录到命令缓冲:
//   - 应用录制命令到 CommandBuffer
//   - 提交 CommandBuffer 到 Queue
//   - GPU 异步执行命令
//
// 命令池 (CommandPool):
//   - 命令缓冲的"内存池", 从池中分配命令缓冲
//   - 每个命令池绑定一个队列族
//   - 可以一次性释放池中所有命令缓冲
//
// 命令缓冲 (CommandBuffer):
//   - 主命令缓冲 (PRIMARY): 可直接提交到队列, 可调用 Secondary
//   - 辅助命令缓冲 (SECONDARY): 不能直接提交, 只能被 PRIMARY 调用
//   - 这里只用 PRIMARY, 简化流程
//
// 关键概念:
//   - 命令录制: vkBeginCommandBuffer -> vkCmdXxx -> vkEndCommandBuffer
//   - 命令提交: vkQueueSubmit(queue, commandBuffer)
//   - 命令缓冲每帧重新录制 (因为 Swapchain 图像可能变化)
// ============================================================================
class CommandManager {
private:
    // 逻辑设备, 用于创建/销毁命令池和命令缓冲
    VkDevice device = VK_NULL_HANDLE;

    // 命令池: 命令缓冲的内存池
    //   - 绑定到图形队列族
    //   - 设置了 RESET_COMMAND_BUFFER_BIT, 允许单独重置命令缓冲
    VkCommandPool pool = VK_NULL_HANDLE;

    // 主渲染命令缓冲 (每帧一个, 数量 = maxFramesInFlight)
    //   - 每帧录制渲染命令 (beginRenderPass, draw, endRenderPass)
    //   - 提交到图形队列执行
    std::vector<VkCommandBuffer> buffers;

    // 阴影渲染命令缓冲 (单独的一个, 用于阴影 pass)
    //   - 录制阴影贴图渲染命令
    //   - 在主渲染 pass 前执行
    VkCommandBuffer shadowCmdBuffer = VK_NULL_HANDLE;

public:
    // 默认构造: 所有句柄初始化为 VK_NULL_HANDLE
    CommandManager() = default;

    // 析构: cleanup 必须显式调用
    ~CommandManager();

    // 注入逻辑设备
    void init(VkDevice dev) { device = dev; }

    // 创建命令池:
    //   - graphicsFamily: 图形队列族索引
    //   - 设置 RESET_COMMAND_BUFFER_BIT, 允许单独重置命令缓冲
    void createPool(uint32_t graphicsFamily);

    // 分配主渲染命令缓冲:
    //   - count: 数量 (通常是 maxFramesInFlight)
    //   - 从命令池分配, 级别为 PRIMARY
    void allocateBuffers(uint32_t count);

    // 分配阴影渲染命令缓冲:
    //   - 分配 1 个 PRIMARY 级别的命令缓冲
    void allocateShadowCommandBuffer();

    // 销毁命令池和所有命令缓冲
    void cleanup();

    // ---------- Getters ----------

    // 获取命令池 (用于临时命令缓冲分配, 如 copyBuffer)
    VkCommandPool getPool() const { return pool; }

    // 获取第 index 个主渲染命令缓冲
    VkCommandBuffer getBuffer(uint32_t index) const { return buffers[index]; }

    // 获取阴影渲染命令缓冲
    VkCommandBuffer getShadowCommandBuffer() const { return shadowCmdBuffer; }
};
