#include "swapchain_manager.h"
#include "vulkan_utils.h"
#include <algorithm>
#include <stdexcept>
using namespace vulkan;

SwapchainManager::~SwapchainManager() {
    // Cleanup should be called explicitly
}

// ----------------------------------------------------------------------------
// 首次创建: 顺序为 swapchain -> image views -> depth buffer
// 重建/销毁都走 create()/cleanup() 这对, 保证资源生命周期清晰
// ----------------------------------------------------------------------------
void SwapchainManager::create(VkSurfaceKHR surface, GLFWwindow* window) {
    createSwapchainInternal(surface, window);
    createImageViews();
    createDepthBuffer();
}

// 窗口尺寸变化时的重建入口: 先释放旧资源, 再按相同流程创建新的
// 注意: 重建期间所有依赖旧 swapchain 的对象 (framebuffer, render pass 等)
// 必须在调用本函数前已经被销毁, 否则会因引用已释放对象而崩溃
void SwapchainManager::recreate(VkSurfaceKHR surface, GLFWwindow* window) {
    cleanup();
    create(surface, window);
}

// ----------------------------------------------------------------------------
// 释放本类持有的所有 Vulkan 对象, 顺序与创建相反:
//   depth view -> depth memory -> depth image -> image views -> swapchain
// ----------------------------------------------------------------------------
void SwapchainManager::cleanup() {
    if (depthImageView) {
        vkDestroyImageView(device, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthMemory) {
        vkFreeMemory(device, depthMemory, nullptr);
        depthMemory = VK_NULL_HANDLE;
    }
    if (depthImage) {
        vkDestroyImage(device, depthImage, nullptr);
        depthImage = VK_NULL_HANDLE;
    }
    for (auto view : imageViews) {
        if (view) vkDestroyImageView(device, view, nullptr);
    }
    imageViews.clear();
    if (swapchain) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    images.clear();
}

// ----------------------------------------------------------------------------
// 创建 Swapchain 主流程
// ----------------------------------------------------------------------------
// Swapchain 是 Vulkan 中唯一"由窗口系统参与管理"的资源, 创建前必须查询:
//   1. 表面能力 (capabilities): 图像数量范围、尺寸范围、支持的变换等
//   2. 支持的表面格式 (formats): 像素格式 + 颜色空间的组合
//   3. 支持的呈现模式 (present modes): 决定画面如何呈现 (vsync/低延迟/…)
// 三项都从物理设备 + 表面查询得到, 不同 GPU/平台可能差异很大
// ----------------------------------------------------------------------------
void SwapchainManager::createSwapchainInternal(VkSurfaceKHR surface, GLFWwindow* window) {
    // (1) 查询表面能力: 包含图像数量、尺寸、支持的变换、用法标志等
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);

    // (2) 查询支持的表面格式 (像素格式 + 颜色空间的组合)
    uint32_t fmtN = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &fmtN, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts;
    if (fmtN == 0) {
        // 默认回退: BGRA 8888 + sRGB
        // - VK_FORMAT_B8G8R8A8_SRGB: 每像素 32 位, 按 B/G/R/A 顺序存储 (8 位/通道, unorm)
        //   后缀 _SRGB 表示该格式关联 sRGB 色彩空间, GPU 写入时自动做 sRGB 编码 (线性->非线性),
        //   采样时自动解码, 从而让着色器在线性空间计算, 显示端得到正确的 gamma 校正结果
        // - VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: 表面期望接收 sRGB 非线性 (经过 OETF/gamma 压缩) 的数据,
        //   这是绝大多数显示器的原生色彩空间, 与上面的 _SRGB 格式配对语义一致
        // 选 B8G8R8A8 而非 R8G8B8A8, 是因为 Windows/macOS/iOS 的窗口系统原生缓冲都是 BGRA 顺序
        // (macOS/iOS 继承自 CoreGraphics/Metal 的 BGRA 传统), 这样可避免呈现时驱动做额外的通道 swizzle
        // 注意: Android 平台原生缓冲通常是 RGBA8888 (OpenGL ES 传统), 如果移植到 Android,
        // 应优先选择 VK_FORMAT_R8G8B8A8_SRGB 以避免 swizzle 开销
        fmts.push_back({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
        fmtN = 1;
    } else {
        fmts.resize(fmtN);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &fmtN, fmts.data());
    }

    // (3) 查询支持的呈现模式
    uint32_t pmN = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &pmN, nullptr);
    std::vector<VkPresentModeKHR> pms;
    if (pmN == 0) {
        // FIFO 是唯一保证所有 Vulkan 实现都支持的模式 (等同垂直同步 vsync)
        pms.push_back(VK_PRESENT_MODE_FIFO_KHR);
        pmN = 1;
    } else {
        pms.resize(pmN);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &pmN, pms.data());
    }

    // 从支持列表中挑选"最优"格式: BGRA8 + sRGB 配对
    // - BGR 通道顺序与窗口系统原生缓冲一致, 避免呈现时额外 swizzle
    // - sRGB 格式 + sRGB 颜色空间的组合让驱动自动完成 gamma 校正,
    //   PBR 着色器只需在线性空间计算即可得到正确的显示效果
    VkSurfaceFormatKHR sf = fmts[0];
    for (auto& f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            sf = f;
            break;
        }

    // 挑选呈现模式: 优先 MAILBOX (三重缓冲, 低延迟无撕裂), 否则回退 FIFO
    // - FIFO: 严格队列, 按 vblank 节拍呈现, 永不撕裂, 但输入延迟大
    // - MAILBOX: 每 vblank 只取最新一帧, 旧帧被丢弃; 延迟低, 也不会撕裂
    // - IMMEDIATE: 立即呈现, 可能撕裂 (这里不用)
    VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : pms)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) { pm = m; break; }

    // 确定 swapchain 图像尺寸:
    // - 多数窗口系统下 caps.currentExtent 直接给出正确值
    // - 某些平台 (老式窗口管理器) 返回 UINT32_MAX 表示"由应用决定"
    // - 尺寸过小 (<16) 通常发生在窗口被最小化, 退到窗口实际帧缓冲大小
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == UINT32_MAX || ext.width < 16 || ext.height < 16) {
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        if (w > 0 && h > 0) {
            ext.width = (uint32_t)w;
            ext.height = (uint32_t)h;
        } else {
            // 兜底: 窗口尺寸拿不到 (比如最小化时), 给个合理默认值避免除零
            ext.width = 1280;
            ext.height = 720;
        }
    }

    // 图像数量: 最小数量 + 1 构成典型的"三缓冲" (minImageCount 通常是 2)
    // 多一张缓冲可以让 GPU/CPU 流水线重叠, 减少等待; 但不能超过设备允许的最大值
    uint32_t imgN = caps.minImageCount + 1;
    if (caps.maxImageCount && imgN > caps.maxImageCount) imgN = caps.maxImageCount;

    // 找出图形/呈现队列族: 如果两者不同, swapchain image 需要跨族共享,
    // 必须用 CONCURRENT sharing mode 并显式列出族索引, 否则用 EXCLUSIVE (更高效)
    auto q = findQueues(physicalDevice, surface);
    if (!q.present) q.present = q.gfx;
    uint32_t families[] = {q.gfx.value(), q.present.value()};

    // 组装 swapchain 创建信息
    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface;
    sci.minImageCount = imgN;
    sci.imageFormat = sf.format;
    sci.imageColorSpace = sf.colorSpace;
    sci.imageExtent = ext;
    sci.imageArrayLayers = 1;
    // 单图层: VR/立体渲染时会设成 2 (左右眼), 普通 2D 渲染固定 1
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    // 用法: 作为颜色附件 (渲染目标); 若要做后处理还可加 SAMPLED_BIT / TRANSFER_SRC_BIT 等
    sci.imageSharingMode =
        (q.gfx != q.present) ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
    // CONCURRENT 模式: 多个队列族可直接读写同一张图, 无需显式 ownership transfer,
    //                  但会有轻微性能损失; 仅在 gfxFamily != presentFamily 时使用
    // EXCLUSIVE 模式: 一次只有一个队列族拥有该图像, 更高效
    if (sci.imageSharingMode == VK_SHARING_MODE_CONCURRENT) {
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = families;
    }
    sci.preTransform = caps.currentTransform;
    // 预变换: 保留当前变换 (通常是 IDENTITY); 移动端可能旋转 90° (竖屏),
    // 直接交给 swapchain 做比在 shader 里旋转更高效
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // 合成 alpha: 不透明模式 (忽略 alpha 通道);
    // 若要实现半透明窗口, 需改为 VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
    sci.presentMode = pm;
    sci.clipped = VK_TRUE;
    // clipped=TRUE: 允许 Vulkan 裁剪掉被其他窗口遮挡的区域, 性能更好
    // clipped=FALSE: 保证整张图都被渲染 (通常没必要, 除非要做屏幕截图)
    sci.oldSwapchain = VK_NULL_HANDLE;
    // 重建 swapchain 时把旧的 swapchain 填到这里, Vulkan 会做无缝过渡;
    // 这里首次创建所以为空

    if (vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain) != VK_SUCCESS)
        throw std::runtime_error("swapchain creation failed");

    // 取出 swapchain 内部的图像句柄 (数量由实际分配决定, 可能多于 minImageCount)
    // 这些 image 由 swapchain 管理生命周期, 不要自己销毁
    vkGetSwapchainImagesKHR(device, swapchain, &imgN, nullptr);
    images.resize(imgN);
    vkGetSwapchainImagesKHR(device, swapchain, &imgN, images.data());
    format = sf.format;
    extent = ext;
}

// ----------------------------------------------------------------------------
// 为 swapchain 中的每张 image 创建 ImageView
// ----------------------------------------------------------------------------
// Image 只是原始像素数据容器, 必须通过 View 才能被 Vulkan 管线以某种方式解读:
//   - viewType: 1D/2D/3D/Cube 等 (这里是 2D)
//   - format:   与 image 匹配, 也可以重解释 (如 UNORM <-> SRGB)
//   - subresourceRange: 指定要访问的 mipmap 层级/数组层/aspect
// 这里每张 swapchain image 都创建一个覆盖全部层的 2D color view,
// 作为 framebuffer 的 color attachment 使用
// ----------------------------------------------------------------------------
void SwapchainManager::createImageViews() {
    imageViews.resize(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        VkImageViewCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image = images[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = format;
        // subresourceRange: aspect=color, 从第 0 层开始, 1 级 mipmap, 1 层数组
        ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device, &ci, nullptr, &imageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("image view creation failed");
    }
}

// ----------------------------------------------------------------------------
// 创建深度缓冲: 深度图 + 显存 + 视图
// ----------------------------------------------------------------------------
// PBR 渲染必须做深度测试 (近处物体遮挡远处), 因此需要一张与 swapchain 同尺寸的
// 深度图 (depth image)。流程分三步:
//   1. 创建 VkImage: 格式选 D32_SFLOAT (32 位浮点深度),
//      比 D16_UNORM 精度高但更耗显存, PBR 场景深度精度重要所以选 32 位
//   2. 分配显存: 必须从 DEVICE_LOCAL (显存) 类型分配, 因为深度测试每帧高频读写,
//      放 CPU 可见内存会拖慢 GPU; 通过 vkGetImageMemoryRequirements 查所需大小,
//      再遍历内存类型找第一个既满足位掩码又是 DEVICE_LOCAL 的类型
//   3. 绑定内存并创建视图: 视图的 aspect 设为 DEPTH (不是 COLOR!),
//      这样它才能作为 render pass 的 depth/stencil attachment
// ----------------------------------------------------------------------------
void SwapchainManager::createDepthBuffer() {
    // (1) 创建深度图
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_D32_SFLOAT;
    // 32 位浮点深度: PBR 需要精确的深度比较, 避免 z-fighting
    // 若显存吃紧可降为 D24_UNORM_S8_UINT (24 位深度 + 8 位模板)
    ii.extent = {extent.width, extent.height, 1};
    // 深度图尺寸必须与 swapchain 一致, 否则 render pass 会因 attachment 尺寸不匹配而失败
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    // 多重采样抗锯齿 (MSAA): 这里用 1 表示不开; 若要开 MSAA,
    // 深度图也要相应提升采样数 (如 VK_SAMPLE_COUNT_4_BIT)
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    // OPTIMAL: 驱动按硬件最优方式排布像素 (通常是 swizzled/tiled), 不能 CPU 直接读;
    // LINEAR: CPU 可逐像素读, 但 GPU 渲染性能差——仅调试用
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    // 仅作为深度/模板附件; 若要做 shadow map 采样, 还需加 SAMPLED_BIT
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // 初始布局未知: 因为我们马上要在 render pass 中 transition 到 DEPTH_ATTACHMENT,
    // 所以无需保留原有内容, 设 UNDEFINED 让驱动跳过清零, 性能更好
    if (vkCreateImage(device, &ii, nullptr, &depthImage) != VK_SUCCESS)
        throw std::runtime_error("depth image creation failed");

    // (2) 分配显存
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device, depthImage, &mr);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &mp);
    uint32_t ti = UINT32_MAX;
    // 在物理设备的内存类型中找一个:
    //   - 位掩码满足 image 的要求 (mr.memoryTypeBits, 不同 GPU 支持的类型不同)
    //   - 同时是 DEVICE_LOCAL (显存, GPU 访问最快)
    // 深度缓冲是每帧高频读写的资源, 必须放显存; 若用 HOST_VISIBLE 会显著拖慢性能
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((mr.memoryTypeBits & (1 << i)) &&
            (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            ti = i;
            break;
        }
    }
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = ti;
    if (vkAllocateMemory(device, &ai, nullptr, &depthMemory) != VK_SUCCESS ||
        vkBindImageMemory(device, depthImage, depthMemory, 0) != VK_SUCCESS)
        throw std::runtime_error("depth memory setup failed");

    // (3) 创建深度图视图
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = depthImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_D32_SFLOAT;
    // 关键: aspect 必须是 DEPTH_BIT (不是 COLOR_BIT!), 这样它才能作为 depth attachment
    // 若格式是 D24_UNORM_S8_UINT 这种复合深度+模板格式, 可同时包含 DEPTH | STENCIL
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &vi, nullptr, &depthImageView) != VK_SUCCESS)
        throw std::runtime_error("depth image view failed");
}
