#include "vulkan_utils.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

// ============================================================================
// 文件读取
// ============================================================================
std::vector<char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::ate | std::ios::binary);
    if (!f) throw std::runtime_error("open failed: " + path);
    std::vector<char> buf(f.tellg());
    f.seekg(0);
    f.read(buf.data(), buf.size());
    return buf;
}

// ============================================================================
// Validation Layer
// ============================================================================
bool checkLayerSupport(const std::vector<const char*>& validationLayers) {
    uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> layers(n);
    vkEnumerateInstanceLayerProperties(&n, layers.data());
    for (const char* l : validationLayers) {
        bool found = false;
        for (auto& p : layers) {
            if (std::strcmp(l, p.layerName) == 0) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

// ============================================================================
// Shader Module
// ============================================================================
VkShaderModule createShaderModule(VkDevice dev, const std::vector<char>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m;
    if (vkCreateShaderModule(dev, &ci, nullptr, &m) != VK_SUCCESS)
        throw std::runtime_error("shader module creation failed");
    return m;
}

// ============================================================================
// Queue Families
// ============================================================================
QueueFamilies findQueues(VkPhysicalDevice pd, VkSurfaceKHR surf) {
    QueueFamilies q;
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, nullptr);
    std::vector<VkQueueFamilyProperties> props(n);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, props.data());
    for (uint32_t i = 0; i < n; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) q.gfx = i;
        VkBool32 sup = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surf, &sup);
        if (sup) q.present = i;
    }
    return q;
}

// ============================================================================
// Buffer 辅助
// ============================================================================
void createBuffer(VkDevice dev, VkPhysicalDevice pd,
                  VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags memProp,
                  VkBuffer& buf, VkDeviceMemory& mem) {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &bi, nullptr, &buf) != VK_SUCCESS)
        throw std::runtime_error("buffer creation failed");

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, buf, &mr);

    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);

    uint32_t typeIdx = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((mr.memoryTypeBits & (1 << i)) &&
            (mp.memoryTypes[i].propertyFlags & memProp) == memProp) {
            typeIdx = i;
            break;
        }
    }
    if (typeIdx == UINT32_MAX) throw std::runtime_error("memory type not found");

    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = typeIdx;
    if (vkAllocateMemory(dev, &ai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("memory allocation failed");
    vkBindBufferMemory(dev, buf, mem, 0);
}

void copyBuffer(VkDevice dev, VkQueue queue, VkCommandPool pool,
                VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(dev, &ai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(dev, pool, 1, &cmd);
}
