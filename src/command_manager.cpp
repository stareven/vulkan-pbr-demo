#include "command_manager.h"
#include <stdexcept>

CommandManager::~CommandManager() {
    // Cleanup should be called explicitly
}

void CommandManager::createPool(VkDevice device, uint32_t graphicsFamily) {
    createCommandPoolInternal(device, graphicsFamily);
}

void CommandManager::allocateBuffers(VkDevice device, uint32_t count) {
    buffers.resize(count);

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = count;

    if (vkAllocateCommandBuffers(device, &ai, buffers.data()) != VK_SUCCESS)
        throw std::runtime_error("command buffer allocation failed");
}

void CommandManager::allocateShadowCommandBuffer(VkDevice device) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &ai, &shadowCmdBuffer) != VK_SUCCESS)
        throw std::runtime_error("shadow command buffer allocation failed");
}

void CommandManager::cleanup(VkDevice device) {
    if (pool) {
        vkDestroyCommandPool(device, pool, nullptr);
        pool = VK_NULL_HANDLE;
    }
    buffers.clear();
}

void CommandManager::createCommandPoolInternal(VkDevice device, uint32_t graphicsFamily) {
    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = graphicsFamily;

    if (vkCreateCommandPool(device, &ci, nullptr, &pool) != VK_SUCCESS)
        throw std::runtime_error("command pool creation failed");
}
