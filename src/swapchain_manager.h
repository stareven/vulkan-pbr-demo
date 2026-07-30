#pragma once

// ----------------------------------------------------------------------------
// GLFW 与 Vulkan 的集成
// ----------------------------------------------------------------------------
// GLFW_INCLUDE_VULKAN: 告诉 GLFW 头文件不要包含 OpenGL 的函数指针,
//   而是包含 Vulkan 的类型定义 (VkImage, VkFormat 等)
// GLFW/glfw3.h: 窗口库头, 提供窗口创建/事件处理以及 Vulkan 集成函数
// ----------------------------------------------------------------------------
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

// ============================================================================
// Swapchain 管理 - 呈现管线的核心
// ============================================================================
// Swapchain 是 Vulkan 的"双/三缓冲"机制, 作用是:
//   - 让应用渲染到一张图像上, 同时显示系统在显示另一张图像
//   - 通过 vkQueuePresentKHR 把渲染好的图像"交换"给显示系统
//   - 从而消除画面撕裂 (tearing) 并提升帧率
//
// SwapchainManager 负责:
//   1. Swapchain 本身 (创建/重建/销毁)
//   2. Swapchain 中每张图像的 VkImageView (视图, 让 shader 能采样/写入)
//   3. 深度缓冲 (depth buffer, 做深度测试用, 必须与 swapchain 图像尺寸一致)
//
// 生命周期:
//   create()  - 窗口首次创建时调用
//   recreate()- 窗口尺寸变化时调用 (resize / 全屏切换 / DPI 改变),
//               内部会先 cleanup 再 create
//   cleanup() - 应用退出或重建前调用, 按创建的反序释放
//
// 重建触发时机:
//   - 用户拖拽改变窗口大小 -> framebufferResized 标志 -> 主循环调 recreate
//   - 窗口从最小化恢复 -> 尺寸变 0 -> 重建 (需要等待有效尺寸)
//
// 关键 Vulkan 类型说明:
//   GLFWwindow*: GLFW 窗口句柄, 封装了操作系统窗口, createSurface 时需要它
//   VkSwapchainKHR: Swapchain 对象, 管理多张图像的双/三缓冲
//   VkImage: 图像对象, 存储像素数据 (可以是颜色图/深度图/纹理)
//   VkImageView: 图像视图, 让 Vulkan 管线以某种方式访问 Image (2D 纹理/某一层/某格式)
//   VkFormat: 像素格式枚举, 如 VK_FORMAT_B8G8R8A8_SRGB (BGRA 8888 + sRGB)
//   VkExtent2D: 二维尺寸 (宽 x 高), 单位是像素
//   VkDeviceMemory: GPU 内存块, 必须显式分配并绑定到 Buffer/Image
//   VK_NULL_HANDLE: Vulkan 的"空指针", 表示句柄未初始化或已销毁
// ============================================================================
class SwapchainManager {
private:
    // ---- 从外部传入, 创建 Vulkan 对象时需要 ----
    // 这些句柄来自 VulkanContext, 通过 init() 注入, 本类不自己创建它们

    // 逻辑设备, 创建 Swapchain/Image/ImageView/Memory 等 GPU 资源时用它
    VkDevice device = VK_NULL_HANDLE;

    // 物理设备 (GPU), 查询内存类型时需要它 (vkGetPhysicalDeviceMemoryProperties)
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

    // ---- Swapchain 核心 ----

    // Swapchain 对象: 管理多张图像的双/三缓冲
    //   - 创建时指定表面/格式/尺寸/图像数量/呈现模式
    //   - 通过 vkAcquireNextImageKHR 获取下一张要渲染的图像
    //   - 通过 vkQueuePresentKHR 把渲染完的图像提交给显示系统
    //   - 窗口尺寸变化时必须重建 (先销毁旧的, 再创建新的)
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;

    // 选定的像素格式 (通常是 VK_FORMAT_B8G8R8A8_SRGB):
    //   - 决定每个像素的通道顺序/位深/色彩空间
    //   - 后续创建 render pass 的 color attachment 时必须匹配这个格式
    //   - 也用于创建 ImageView 时指定格式
    VkFormat format;

    // Swapchain 图像的像素尺寸 (宽 x 高), 必须和窗口帧缓冲一致:
    //   - 深度缓冲的尺寸必须与它匹配
    //   - framebuffer 的尺寸由它决定
    //   - viewport/scissor 的范围基于它设置
    //   - 窗口 resize 时会变化, 触发 swapchain 重建
    VkExtent2D extent;

    // Swapchain 内的所有图像句柄 (数量由 minImageCount 决定, 通常是 2 或 3):
    //   - 每帧渲染时通过 vkAcquireNextImageKHR 从中"取"一张来绘制
    //   - 这些图像由 Swapchain 管理生命周期, 不要自己销毁
    //   - 必须为每张图像创建对应的 ImageView 才能在 render pass 中使用
    std::vector<VkImage> images;

    // 每张 image 对应的视图 (View):
    //   - Vulkan 里 Image 只是原始像素数据容器, 必须通过 ImageView 才能被管线访问
    //   - ImageView 指定: 维度 (1D/2D/3D)、格式、mipmap 层级、数组层
    //   - 作为 framebuffer 的 attachment 时必须提供 ImageView
    //   - 数量与 images 相同, 一一对应
    std::vector<VkImageView> imageViews;

    // ---- 深度缓冲 ----

    // 深度图: 存储每个像素的深度值, 用于深度测试 (近处遮挡远处):
    //   - 格式通常是 VK_FORMAT_D32_SFLOAT (32 位浮点) 或 D24_UNORM_S8_UINT
    //   - 尺寸必须与 swapchain extent 一致, 否则 render pass 会失败
    //   - 作为 render pass 的 depth/stencil attachment 使用
    //   - 必须分配显存并创建 ImageView 才能使用
    VkImage depthImage = VK_NULL_HANDLE;

    // 深度图背后的 GPU 内存:
    //   - 必须从 DEVICE_LOCAL (显存) 类型分配, 因为深度测试每帧高频读写
    //   - 若放在 HOST_VISIBLE (CPU 可见内存) 会显著拖慢 GPU 性能
    //   - 通过 vkAllocateMemory 分配, 通过 vkBindImageMemory 绑定到 depthImage
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;

    // 深度图的视图:
    //   - aspect 必须设为 VK_IMAGE_ASPECT_DEPTH_BIT (不是 COLOR_BIT!)
    //   - 作为 render pass 的 depth/stencil attachment 使用
    //   - 格式必须与 depthImage 匹配 (通常是 VK_FORMAT_D32_SFLOAT)
    VkImageView depthImageView = VK_NULL_HANDLE;

public:
    // 默认构造: 所有句柄初始化为 VK_NULL_HANDLE
    SwapchainManager() = default;

    // 析构: 注意 cleanup() 必须显式调用, 析构函数本身不做清理
    // (因为析构时 device 可能已经被 VulkanContext 销毁了)
    ~SwapchainManager();

    // 注入依赖: device/pd 来自 VulkanContext, 本类不自己创建它们
    // dev: 逻辑设备, 用于创建 Vulkan 对象
    // pd: 物理设备, 用于查询内存类型
    void init(VkDevice dev, VkPhysicalDevice pd) { device = dev; physicalDevice = pd; }

    // 首次创建: swapchain + image views + depth buffer
    // surface: 窗口表面, Swapchain 创建时必须指定 (图像呈现的目标)
    // window: GLFW 窗口句柄, 用于获取帧缓冲尺寸 (glfwGetFramebufferSize)
    void create(VkSurfaceKHR surface, GLFWwindow* window);

    // 窗口变化时重建: 先 cleanup 旧资源, 再 create 新的
    // 触发时机: 窗口 resize / 全屏切换 / DPI 改变 / 从最小化恢复
    // 注意: 调用前必须确保所有依赖旧 swapchain 的对象 (framebuffer 等) 已被销毁
    void recreate(VkSurfaceKHR surface, GLFWwindow* window);

    // 释放所有本类持有的 Vulkan 对象, 顺序与 create 相反:
    //   depthImageView -> depthMemory -> depthImage -> imageViews -> swapchain
    // 必须在析构前或重建前调用
    void cleanup();

    // ---------- Getters ----------

    // VkSwapchainKHR: Swapchain 对象, 用于:
    //   - vkAcquireNextImageKHR (获取下一张要渲染的图像)
    //   - vkQueuePresentKHR (提交渲染完的图像)
    //   - 创建 framebuffer 时作为依赖
    VkSwapchainKHR getSwapchain() const { return swapchain; }

    // VkFormat: 选定的像素格式, 用于:
    //   - 创建 render pass 时指定 color attachment 格式
    //   - 创建 ImageView 时指定格式
    //   - 创建 framebuffer 时必须与 render pass 匹配
    VkFormat getFormat() const { return format; }

    // VkExtent2D: Swapchain 图像尺寸, 用于:
    //   - 创建 framebuffer 时指定尺寸
    //   - 设置 viewport/scissor 的范围
    //   - 创建深度缓冲时指定尺寸
    VkExtent2D getExtent() const { return extent; }

    // std::vector<VkImageView>: Swapchain 图像的视图列表, 用于:
    //   - 创建 framebuffer 时作为 color attachment
    //   - 每帧渲染时根据当前帧索引选择对应的 view
    //   返回 const 引用避免拷贝
    const std::vector<VkImageView>& getImageViews() const { return imageViews; }

    // VkImageView: 深度图视图, 用于:
    //   - 创建 framebuffer 时作为 depth/stencil attachment
    //   - render pass 执行时自动进行深度测试
    VkImageView getDepthImageView() const { return depthImageView; }

    // uint32_t: Swapchain 图像数量 (通常是 2 或 3):
    //   - 用于循环遍历 imageViews
    //   - 创建同步对象 (semaphore/fence) 时通常需要与帧数匹配
    uint32_t getImageCount() const { return (uint32_t)images.size(); }

private:
    // 查询表面能力 -> 选定格式/呈现模式/尺寸 -> 创建 swapchain
    // surface: 窗口表面, 查询能力和创建时都需要
    // window: GLFW 窗口句柄, 用于获取帧缓冲尺寸 (当 caps.currentExtent 无效时)
    void createSwapchainInternal(VkSurfaceKHR surface, GLFWwindow* window);

    // 为 swapchain 中的每张 image 创建一个 2D color view:
    //   - viewType: VK_IMAGE_VIEW_TYPE_2D
    //   - format: 与 swapchain format 匹配
    //   - aspect: VK_IMAGE_ASPECT_COLOR_BIT
    void createImageViews();

    // 创建深度缓冲三步曲:
    //   1. 创建 VkImage (格式 D32_SFLOAT, 尺寸与 extent 一致)
    //   2. 分配 DEVICE_LOCAL 显存并绑定到 image
    //   3. 创建 VkImageView (aspect = DEPTH_BIT)
    void createDepthBuffer();
};
