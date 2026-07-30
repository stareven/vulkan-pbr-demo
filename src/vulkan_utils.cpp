#include "vulkan_utils.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace vulkan {

// ============================================================================
// 读取文件内容
// ============================================================================
// 流程:
//   1. 以二进制模式打开文件, 定位到末尾 (std::ios::ate)
//   2. tellg() 获取文件大小
//   3. 分配对应大小的 buffer
//   4. 回到文件开头 (seekg(0))
//   5. 一次性读入 buffer
//
// 用途:
//   - 加载 SPIR-V shader 字节码 (编译后的二进制)
//   - 也可以加载纹理/配置文件
//
// 注意:
//   - 返回 std::vector<char>, 方便直接传给 vkCreateShaderModule
//   - 文件不存在或读取失败会抛异常
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
// 校验层支持检查
// ============================================================================
// 流程:
//   1. 调用 vkEnumerateInstanceLayerProperties 枚举所有可用校验层
//   2. 逐个比对用户请求的校验层名称
//   3. 若所有请求的校验层都能找到, 返回 true
//
// 用途:
//   - 创建 VkInstance 前检查, 避免启用不存在的校验层导致创建失败
//   - 开发期应开启校验层, 捕获 API 误用和内存泄漏
//
// 常见校验层:
//   - VK_LAYER_KHRONOS_validation: 官方校验层, 包含所有标准校验
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
// 创建 Shader 模块
// ============================================================================
// 流程:
//   1. 构造 VkShaderModuleCreateInfo
//      - codeSize: SPIR-V 字节码大小 (字节)
//      - pCode: SPIR-V 字节码指针 (必须是 uint32_t* 对齐)
//   2. 调用 vkCreateShaderModule 创建模块
//
// 用途:
//   - 把 GLSL 编译后的 SPIR-V 二进制加载到 Vulkan
//   - Shader 模块是 Pipeline 创建的一部分, 用于指定顶点/片段着色器
//
// 注意:
//   - SPIR-V 是 Vulkan 的中间表示, 由 glslangValidator 或 DXC 编译
//   - code 必须是 4 字节对齐的 (SPIR-V 指令是 32 位)
//   - 模块创建后不会立即编译, 而是等到 Pipeline 创建时才编译
// ============================================================================
VkShaderModule createShaderModule(VkDevice dev, const std::vector<char>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    // reinterpret_cast: 把 char* 转为 uint32_t* (SPIR-V 是 32 位指令)
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m;
    if (vkCreateShaderModule(dev, &ci, nullptr, &m) != VK_SUCCESS)
        throw std::runtime_error("shader module creation failed");
    return m;
}

// ============================================================================
// 查找队列族
// ============================================================================
// 流程:
//   1. 调用 vkGetPhysicalDeviceQueueFamilyProperties 枚举所有队列族
//   2. 遍历每个队列族, 检查:
//      - queueFlags & VK_QUEUE_GRAPHICS_BIT: 是否支持图形操作
//      - vkGetPhysicalDeviceSurfaceSupportKHR: 是否支持向 Surface 呈现
//   3. 找到第一个满足条件的族, 记录其索引
//
// 返回:
//   - QueueFamilies 结构, 包含 gfx 和 present 的族索引
//   - 若某个族同时支持图形和呈现, 则两个索引相同
//   - 若两个能力在不同的族, 则索引不同 (较少见)
//
// 用途:
//   - 创建逻辑设备时需要指定队列族
//   - 若 gfx != present, 资源创建时必须用 VK_SHARING_MODE_CONCURRENT
// ============================================================================
QueueFamilies findQueues(VkPhysicalDevice pd, VkSurfaceKHR surf) {
    QueueFamilies q;
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, nullptr);
    std::vector<VkQueueFamilyProperties> props(n);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, props.data());
    for (uint32_t i = 0; i < n; ++i) {
        // 检查是否支持图形操作
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) q.gfx = i;
        // 检查是否支持向 Surface 呈现
        VkBool32 sup = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surf, &sup);
        if (sup) q.present = i;
    }
    return q;
}

// ============================================================================
// 创建 Buffer 并分配内存
// ============================================================================
// 这是 Vulkan 中创建 Buffer 的标准模式:
//   1. 创建 Buffer 对象 (vkCreateBuffer)
//      - 指定大小/用途/共享模式
//      - Buffer 只是"描述", 还没有实际内存
//   2. 查询内存需求 (vkGetBufferMemoryRequirements)
//      - 获取所需大小 (可能比请求的大, 对齐原因)
//      - 获取内存类型位掩码 (不同 GPU 支持不同类型)
//   3. 查找合适的内存类型
//      - 遍历物理设备的内存类型
//      - 找到既满足位掩码要求, 又有指定属性的类型
//      - 常见属性:
//        * DEVICE_LOCAL: 显存, GPU 访问最快, CPU 不可见
//        * HOST_VISIBLE | HOST_COHERENT: CPU 可见, 可直接写入
//   4. 分配内存 (vkAllocateMemory)
//   5. 绑定内存到 Buffer (vkBindBufferMemory)
//
// 用途:
//   - 创建顶点缓冲/索引缓冲/统一缓冲/staging buffer
//   - 几乎所有 GPU 数据都通过 Buffer 管理
// ============================================================================
void createBuffer(VkDevice dev, VkPhysicalDevice pd,
                  VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags memProp,
                  VkBuffer& buf, VkDeviceMemory& mem) {
    // 1. 创建 Buffer 对象
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;  // 用途标志: VERTEX_BUFFER/INDEX_BUFFER/UNIFORM_BUFFER 等
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;  // 单队列族独占
    if (vkCreateBuffer(dev, &bi, nullptr, &buf) != VK_SUCCESS)
        throw std::runtime_error("buffer creation failed");

    // 2. 查询内存需求
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, buf, &mr);

    // 3. 查找合适的内存类型
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);

    uint32_t typeIdx = UINT32_MAX;
    // 遍历所有内存类型, 找第一个满足条件的
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        // 位掩码匹配: 这个类型是否支持这个 Buffer
        // 属性匹配: 是否有指定的属性 (如 DEVICE_LOCAL)
        if ((mr.memoryTypeBits & (1 << i)) &&
            (mp.memoryTypes[i].propertyFlags & memProp) == memProp) {
            typeIdx = i;
            break;
        }
    }
    if (typeIdx == UINT32_MAX) throw std::runtime_error("memory type not found");

    // 4. 分配内存
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;  // 实际需要的内存大小
    ai.memoryTypeIndex = typeIdx;
    if (vkAllocateMemory(dev, &ai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("memory allocation failed");

    // 5. 绑定内存到 Buffer
    vkBindBufferMemory(dev, buf, mem, 0);
}

// ============================================================================
// Buffer 数据拷贝
// ============================================================================
// 用途:
//   - 把数据从 CPU 可见的 staging buffer 拷贝到 GPU 显存的 device local buffer
//   - 这是 Vulkan 的标准模式:
//     1. 创建 staging buffer (HOST_VISIBLE, CPU 可写)
//     2. CPU 写入数据到 staging buffer
//     3. 创建 device local buffer (DEVICE_LOCAL, GPU 访问快)
//     4. 用这个函数把数据从 staging 拷贝到 device local
//     5. GPU 渲染时从 device local buffer 读取
//
// 流程:
//   1. 分配临时命令缓冲 (ONE_TIME_SUBMIT, 只用一次)
//   2. 开始记录命令
//   3. 记录 vkCmdCopyBuffer (拷贝指定大小的数据)
//   4. 结束记录
//   5. 提交命令缓冲到队列
//   6. 等待队列空闲 (确保拷贝完成)
//   7. 释放临时命令缓冲
//
// 注意:
//   - 使用 ONE_TIME_SUBMIT 标志, 告诉驱动这个命令缓冲只会执行一次
//   - vkQueueWaitIdle 确保拷贝完成后才返回, 避免后续操作读到未拷贝的数据
// ============================================================================
void copyBuffer(VkDevice dev, VkQueue queue, VkCommandPool pool,
                VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    // 1. 分配临时命令缓冲
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;  // 主命令缓冲 (可直接提交)
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(dev, &ai, &cmd);

    // 2. 开始记录
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;  // 只用一次
    vkBeginCommandBuffer(cmd, &bi);

    // 3. 记录拷贝命令
    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    vkEndCommandBuffer(cmd);

    // 4. 提交并等待完成
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);  // 等待拷贝完成

    // 5. 释放临时命令缓冲
    vkFreeCommandBuffers(dev, pool, 1, &cmd);
}

} // namespace vulkan
