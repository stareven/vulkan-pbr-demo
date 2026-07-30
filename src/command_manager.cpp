#include "command_manager.h"
#include <stdexcept>

// ----------------------------------------------------------------------------
// 析构函数: cleanup 必须显式调用
// ----------------------------------------------------------------------------
CommandManager::~CommandManager() {
    // Cleanup should be called explicitly
}

// ----------------------------------------------------------------------------
// 创建命令池
// ----------------------------------------------------------------------------
// 命令池是命令缓冲的"内存池":
//   - 从池中分配命令缓冲, 避免频繁申请/释放内存
//   - 每个命令池绑定一个队列族 (这里绑定图形队列族)
//   - 设置了 RESET_COMMAND_BUFFER_BIT, 允许单独重置命令缓冲
//     (否则命令缓冲只能随池一起重置, 不够灵活)
//
// 参数:
//   - graphicsFamily: 图形队列族索引
//     (命令池必须绑定到队列族, 从该池分配的命令缓冲只能提交到该族的队列)
// ----------------------------------------------------------------------------
void CommandManager::createPool(uint32_t graphicsFamily) {
    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    // RESET_COMMAND_BUFFER_BIT: 允许单独重置命令缓冲
    //   - 默认情况下, 命令缓冲只能随池一起重置
    //   - 设置这个标志后, 可以单独重置某个命令缓冲 (更灵活)
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    // 绑定到图形队列族
    ci.queueFamilyIndex = graphicsFamily;

    if (vkCreateCommandPool(device, &ci, nullptr, &pool) != VK_SUCCESS)
        throw std::runtime_error("command pool creation failed");
}

// ----------------------------------------------------------------------------
// 分配主渲染命令缓冲
// ----------------------------------------------------------------------------
// 从命令池分配多个命令缓冲:
//   - count: 数量 (通常是 maxFramesInFlight, 如 2)
//   - level: PRIMARY (主命令缓冲, 可直接提交到队列)
//   - 每帧用一个命令缓冲, 录制该帧的渲染命令
//
// 命令缓冲的生命周期:
//   - 分配后持续存在, 每帧重新录制 (不重新分配)
//   - 录制: vkBeginCommandBuffer -> vkCmdXxx -> vkEndCommandBuffer
//   - 提交: vkQueueSubmit
// ----------------------------------------------------------------------------
void CommandManager::allocateBuffers(uint32_t count) {
    buffers.resize(count);

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;  // 主命令缓冲
    ai.commandBufferCount = count;

    if (vkAllocateCommandBuffers(device, &ai, buffers.data()) != VK_SUCCESS)
        throw std::runtime_error("command buffer allocation failed");
}

// ----------------------------------------------------------------------------
// 分配阴影渲染命令缓冲
// ----------------------------------------------------------------------------
// 分配 1 个命令缓冲, 专门用于阴影 pass:
//   - 录制阴影贴图渲染命令
//   - 在主渲染 pass 前执行 (先渲染阴影图, 再渲染主场景)
//   - 与主渲染命令缓冲分开, 便于独立管理
// ----------------------------------------------------------------------------
void CommandManager::allocateShadowCommandBuffer() {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &ai, &shadowCmdBuffer) != VK_SUCCESS)
        throw std::runtime_error("shadow command buffer allocation failed");
}

// ----------------------------------------------------------------------------
// 销毁命令池
// ----------------------------------------------------------------------------
// 销毁命令池时, 池中所有命令缓冲自动释放, 无需单独销毁
// 注意:
//   - 必须在设备销毁前调用
//   - 必须确保所有命令缓冲都已执行完毕 (等待队列空闲)
// ----------------------------------------------------------------------------
void CommandManager::cleanup() {
    if (pool) {
        vkDestroyCommandPool(device, pool, nullptr);
        pool = VK_NULL_HANDLE;
    }
    buffers.clear();
}
