#pragma once

// ----------------------------------------------------------------------------
// GLFW 与 Vulkan 头文件: 提供窗口管理和 Vulkan 类型定义
// ----------------------------------------------------------------------------
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <optional>
#include <string>
#include <vector>

// ============================================================================
// Vulkan 工具函数库
// ============================================================================
// 这个命名空间提供 Vulkan 开发中常用的辅助函数:
//   - readFile: 读取文件内容 (shader/纹理)
//   - checkLayerSupport: 检查校验层是否可用
//   - createShaderModule: 从 SPIR-V 字节码创建 shader 模块
//   - findQueues: 查找图形/呈现队列族
//   - createBuffer: 创建 Vulkan Buffer 并分配内存
//   - copyBuffer: 在两个 Buffer 之间拷贝数据
//
// 这些函数封装了 Vulkan 的常见操作模式, 减少重复代码
// ============================================================================
namespace vulkan {

// 读取文件内容到内存:
//   - path: 文件路径
//   - 返回: 文件内容的字节数组
//   - 用途: 加载 SPIR-V shader 字节码, 也可以加载纹理/配置
//   - 实现: 用 std::ios::ate 定位到文件末尾, 获取文件大小, 然后一次性读入
std::vector<char> readFile(const std::string& path);

// 检查校验层支持:
//   - validationLayers: 要检查的校验层名称列表
//   - 返回: true 表示所有校验层都可用
//   - 用途: 创建 VkInstance 前检查, 避免创建失败
//   - 实现: 调用 vkEnumerateInstanceLayerProperties 枚举所有可用校验层, 逐个比对
bool checkLayerSupport(const std::vector<const char*>& validationLayers);

// 创建 Shader 模块:
//   - dev: 逻辑设备
//   - code: SPIR-V 字节码 (从文件读取的二进制数据)
//   - 返回: VkShaderModule 句柄
//   - 用途: 把 GLSL 编译后的 SPIR-V 加载到 Vulkan
//   - 注意: code 必须是 4 字节对齐的 (SPIR-V 是 32 位指令)
VkShaderModule createShaderModule(VkDevice dev, const std::vector<char>& code);

// ----------------------------------------------------------------------------
// 队列族查找结果
// ----------------------------------------------------------------------------
// 封装图形队列和呈现队列的族索引:
//   - gfx: 支持图形操作的队列族 (绘制/光栅化)
//   - present: 支持向 Surface 呈现图像的队列族
//   - complete(): 两个队列都找到了
// 注意: gfx 和 present 可能是同一个族 (大多数情况), 也可能是不同的族
struct QueueFamilies {
    std::optional<uint32_t> gfx, present;
    bool complete() const { return gfx && present; }
};

// 查找队列族:
//   - pd: 物理设备 (GPU)
//   - surf: 窗口表面
//   - 返回: QueueFamilies 结构, 包含 gfx 和 present 的族索引
//   - 用途: 创建逻辑设备时需要指定队列族
//   - 实现: 遍历所有队列族, 检查是否支持图形操作和 Surface 呈现
QueueFamilies findQueues(VkPhysicalDevice pd, VkSurfaceKHR surf);

// ----------------------------------------------------------------------------
// Buffer 创建辅助
// ----------------------------------------------------------------------------
// 创建 Buffer 并分配内存:
//   - dev: 逻辑设备
//   - pd: 物理设备 (用于查询内存类型)
//   - size: Buffer 大小 (字节)
//   - usage: 用途标志 (如 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
//   - memProp: 内存属性 (如 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
//   - buf: 输出参数, 创建的 Buffer 句柄
//   - mem: 输出参数, 分配的内存句柄
//
// 流程:
//   1. 调用 vkCreateBuffer 创建 Buffer 对象
//   2. 调用 vkGetBufferMemoryRequirements 查询所需内存大小和类型
//   3. 遍历物理设备的内存类型, 找到满足要求的一个
//   4. 调用 vkAllocateMemory 分配内存
//   5. 调用 vkBindBufferMemory 把内存绑定到 Buffer
//
// 这是 Vulkan 中创建 Buffer 的标准模式, 几乎所有 Buffer 都走这个流程
void createBuffer(VkDevice dev, VkPhysicalDevice pd,
                  VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags memProp,
                  VkBuffer& buf, VkDeviceMemory& mem);

// 在两个 Buffer 之间拷贝数据:
//   - dev: 逻辑设备
//   - queue: 图形队列 (用于提交拷贝命令)
//   - pool: 命令池 (用于分配临时命令缓冲)
//   - src: 源 Buffer
//   - dst: 目标 Buffer
//   - size: 拷贝大小 (字节)
//
// 用途:
//   - 把数据从 CPU 可见的 staging buffer 拷贝到 GPU 显存的 device local buffer
//   - 这是 Vulkan 的标准模式: CPU 写入 staging buffer, GPU 从 device local buffer 读取
//
// 流程:
//   1. 分配一个临时命令缓冲
//   2. 记录 vkCmdCopyBuffer 命令
//   3. 提交命令缓冲并等待完成
//   4. 释放临时命令缓冲
void copyBuffer(VkDevice dev, VkQueue queue, VkCommandPool pool,
                VkBuffer src, VkBuffer dst, VkDeviceSize size);

} // namespace vulkan
