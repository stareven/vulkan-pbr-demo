#pragma once

// ----------------------------------------------------------------------------
// GLFW 与 Vulkan 的集成
// ----------------------------------------------------------------------------
// GLFW_INCLUDE_VULKAN: 告诉 GLFW 头文件不要包含 OpenGL 的函数指针,
//   而是包含 Vulkan 的类型定义 (VkInstance, VkDevice 等)
// GLFW/glfw3.h: 窗口库头, 提供:
//   - 窗口创建/事件处理 (glfwCreateWindow, glfwPollEvents 等)
//   - Vulkan 集成函数 (glfwGetRequiredInstanceExtensions, glfwCreateWindowSurface)
//   - 通过定义 GLFW_INCLUDE_VULKAN, 还能直接使用 Vulkan 的类型和函数
// ----------------------------------------------------------------------------
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include <string>
#include <set>

// ============================================================================
// Vulkan 上下文 - 管理核心 Vulkan 资源
// ============================================================================
// Vulkan 的初始化遵循严格的层级关系:
//   VkInstance ( Vulkan 实例, 应用与 Vulkan 驱动之间的连接)
//     └─ VkSurfaceKHR (窗口表面, Vulkan 与窗口系统的桥梁, 由 GLFW 创建)
//         └─ VkPhysicalDevice (物理设备, 即 GPU)
//             └─ VkDevice (逻辑设备, 应用实际使用的 GPU 抽象)
//                 └─ VkQueue (命令队列, 用于向 GPU 提交工作)
//                      ├─ Graphics Queue (图形队列, 支持绘制/光栅化)
//                      └─ Present Queue  (呈现队列, 支持向 Surface 提交图像)
//
// 初始化顺序:
//   1. createInstance     - 建立 Vulkan 运行时, 声明要用的 API 版本/扩展/校验层
//   2. createSurface      - 通过 GLFW 把操作系统窗口包装成 Vulkan 能理解的"表面"
//   3. pickPhysicalDevice - 枚举所有 GPU, 挑选一个支持所需扩展的 (通常是独显)
//   4. createLogicalDevice- 从物理设备"开出"一个逻辑设备, 并获取图形/呈现队列句柄
//
// 析构顺序必须与创建顺序相反: device -> surface -> instance
//
// 关键 Vulkan 类型说明:
//   GLFWwindow*: GLFW 窗口句柄, 封装了操作系统窗口 (HWND/CAMetalLayer/X11 Window)
//   VkInstance: Vulkan 运行时实例, 是应用与 Vulkan 驱动的"会话", 所有 Vulkan 操作的起点
//   VkSurfaceKHR: 窗口表面, 由 GLFW 创建, 是 Vulkan 与窗口系统的桥梁, Swapchain 的目标
//   VkPhysicalDevice: 物理设备 (GPU), 用于查询能力 (支持的扩展/内存类型/队列族)
//   VkDevice: 逻辑设备, 应用实际"操作" GPU 的句柄, 创建 Buffer/Image/Pipeline 等都用它
//   VkQueue: 命令队列, 用于向 GPU 提交命令缓冲 (CommandBuffer)
//   VkQueueFamily: 队列族索引, 标识队列支持的操作类型 (图形/计算/传输/呈现)
//   VK_NULL_HANDLE: Vulkan 的"空指针", 表示句柄未初始化或已销毁
//   UINT32_MAX: 32 位无符号整数最大值, 这里用作 queueFamily 的"未设置"标记
// ============================================================================
class VulkanContext {
public:
    // 初始化入口: 按顺序创建 Instance/Surface/PhysicalDevice/LogicalDevice
    // window: GLFW 窗口句柄, createSurface 时需要用它创建 VkSurfaceKHR
    void initialize(GLFWwindow* window);

    // 清理入口: 按相反顺序销毁 device/surface/instance
    // 必须在应用退出前调用, 否则会导致资源泄漏或驱动崩溃
    void cleanup();

    // ---------- Getters: 把内部句柄暴露给其他 Manager 使用 ----------
    // 其他 Manager (SwapchainManager/RenderPipeline 等) 创建 Vulkan 对象时
    // 需要这些句柄作为参数, 通过 getter 获取而不是直接访问私有成员

    // VkInstance: Vulkan 运行时实例, 创建 Surface/枚举物理设备等操作都依赖它
    VkInstance getInstance() const { return instance; }

    // VkPhysicalDevice: 代表一块真实 GPU, 用于:
    //   - 查询设备能力 (vkGetPhysicalDeviceProperties)
    //   - 查询支持的扩展 (vkEnumerateDeviceExtensionProperties)
    //   - 查询内存类型 (vkGetPhysicalDeviceMemoryProperties)
    //   - 创建逻辑设备 (vkCreateDevice)
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }

    // VkDevice: 逻辑设备, 创建以下 GPU 资源时都要用它:
    //   - Buffer (顶点缓冲/索引缓冲/统一缓冲)
    //   - Image (纹理/深度图/Swapchain 图像)
    //   - Pipeline (图形管线/计算管线)
    //   - Descriptor Set/Pool/Layout
    //   - Command Pool/Buffer
    //   - Semaphore/Fence (同步对象)
    VkDevice getDevice() const { return device; }

    // VkSurfaceKHR: 窗口表面, Swapchain 创建时必须指定:
    //   - Swapchain 的图像最终要"呈现"到这个表面上
    //   - 查询表面能力/格式/呈现模式时都需要它
    VkSurfaceKHR getSurface() const { return surface; }

    // Graphics Queue: 图形队列, 用于提交以下命令:
    //   - vkCmdDraw/vkCmdDrawIndexed (绘制)
    //   - vkCmdCopyBuffer/vkCmdCopyImage (数据拷贝)
    //   - vkCmdPipelineBarrier (资源布局转换/内存屏障)
    //   - vkCmdBeginRenderPass/vkCmdEndRenderPass (渲染过程)
    VkQueue getGraphicsQueue() const { return graphicsQueue; }

    // Present Queue: 呈现队列, 用于调用:
    //   - vkQueuePresentKHR (把渲染完的帧提交到 Swapchain, 显示到屏幕)
    // 注意: graphicsQueue 和 presentQueue 可能是同一个队列对象
    //       (当 graphicsFamily == presentFamily 时)
    VkQueue getPresentQueue() const { return presentQueue; }

    // Queue Family Index: 队列族索引, 标识队列支持的操作类型
    //   - graphicsFamily: 支持图形操作的队列族 (绘制/光栅化)
    //   - presentFamily: 支持向 Surface 呈现图像的队列族
    //   - 若两者相同: 资源可以用 VK_SHARING_MODE_EXCLUSIVE (更高效)
    //   - 若两者不同: 资源必须用 VK_SHARING_MODE_CONCURRENT, 并显式列出两个族索引
    uint32_t getGraphicsFamily() const { return graphicsFamily; }
    uint32_t getPresentFamily() const { return presentFamily; }

private:
    // ---- Vulkan 核心句柄 ----

    // Vulkan 运行时实例, 是整个 Vulkan 状态的根对象
    VkInstance instance = VK_NULL_HANDLE;

    // 物理设备 (GPU), 用于查询能力和创建逻辑设备
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

    // 逻辑设备, 应用实际操作的 GPU 抽象
    VkDevice device = VK_NULL_HANDLE;

    // 窗口表面, 由 GLFW 创建, 是 Swapchain 的目标
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    // 图形队列句柄, 用于提交绘制命令
    VkQueue graphicsQueue = VK_NULL_HANDLE;

    // 呈现队列句柄, 用于提交呈现命令
    VkQueue presentQueue = VK_NULL_HANDLE;

    // ---- 队列族索引 ----

    // 支持图形操作的队列族索引, UINT32_MAX 表示未设置
    uint32_t graphicsFamily = UINT32_MAX;

    // 支持呈现操作的队列族索引, UINT32_MAX 表示未设置
    uint32_t presentFamily = UINT32_MAX;

    // ---- 内部初始化步骤 (按顺序调用) ----

    // 创建 VkInstance: 声明 API 版本/扩展/校验层
    void createInstance();

    // 创建 VkSurfaceKHR: 通过 GLFW 把操作系统窗口包装成 Vulkan 表面
    // window: GLFW 窗口句柄, 内部会调用 glfwCreateWindowSurface
    void createSurface(GLFWwindow* window);

    // 挑选物理设备: 枚举所有 GPU, 选择第一个支持所需扩展的
    void pickPhysicalDevice();

    // 创建逻辑设备: 查找队列族, 创建 VkDevice, 获取队列句柄
    void createLogicalDevice();
};
