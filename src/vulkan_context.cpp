#include "vulkan_context.h"
#include "vulkan_utils.h"
#include "types.h"

#include <iostream>
#include <set>
#include <stdexcept>

using namespace config;
using namespace vulkan;

// ----------------------------------------------------------------------------
// 总入口: 按顺序初始化 Vulkan 核心栈, 任一步失败则抛出异常中断启动
// ----------------------------------------------------------------------------
void VulkanContext::initialize(GLFWwindow* window) {
    // 若开启了校验层但系统不支持, 直接拒绝启动——校验层是调试必备, 缺失时问题难定位
    if (ENABLE_VALIDATION && !checkLayerSupport(validationLayers()))
        throw std::runtime_error("validation layer unavailable");

    createInstance();       // 1. Vulkan 实例
    createSurface(window);  // 2. 窗口表面 (依赖实例)
    pickPhysicalDevice();   // 3. 挑选 GPU (依赖实例 + 表面)
    createLogicalDevice();  // 4. 逻辑设备 + 队列 (依赖物理设备 + 表面)
}

// ----------------------------------------------------------------------------
// 析构: 顺序必须严格为 device -> surface -> instance
// - device 依赖 instance (销毁 device 前 instance 必须还活着)
// - surface 依赖 instance (surface 是 instance 级别的 dispatchable 对象)
// - instance 是最后销毁的根对象
// ----------------------------------------------------------------------------
void VulkanContext::cleanup() {
    if (device) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
    if (surface) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
    if (instance) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

// ----------------------------------------------------------------------------
// 创建 VkInstance: Vulkan 运行时入口
// ----------------------------------------------------------------------------
// VkInstance 是应用与 Vulkan 驱动之间的"会话"。创建时需要声明:
//   - API 版本 (这里选 1.1, macOS MoltenVK 支持的上限附近)
//   - 实例级扩展 (instance extensions): GLFW 提供的表面创建扩展 +
//     VK_KHR_PORTABILITY_ENUMERATION (macOS 的 MoltenVK 是不完全兼容实现,
//     必须显式声明这个扩展才能枚举到它, 否则会看不到任何 GPU)
//   - 校验层 (validation layers): 开发期使用, 会捕获 API 误用、内存泄漏等,
//     Release 构建通过 ENABLE_VALIDATION=false 关闭以提升性能
// ----------------------------------------------------------------------------
void VulkanContext::createInstance() {
    VkApplicationInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "PBR Demo";
    ai.apiVersion = VK_API_VERSION_1_1;

    // 从 GLFW 拿窗口系统所需的实例扩展:
    // - VK_KHR_surface (表面抽象)
    // - 平台相关扩展: macOS 上是 VK_EXT_metal_surface, Windows 上是 VK_KHR_win32_surface 等
    uint32_t extCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
    std::vector<const char*> exts(glfwExts, glfwExts + extCount);
    // macOS 专用: MoltenVK 实现的是 Vulkan Portability 子集,
    // 必须加这个扩展 + 下面的 VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
    // 否则 vkEnumeratePhysicalDevices 会看不到 MoltenVK 设备
    exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = (uint32_t)exts.size();
    ici.ppEnabledExtensionNames = exts.data();
    ici.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;  // 启用 portability 枚举
    if (ENABLE_VALIDATION) {
        ici.enabledLayerCount = (uint32_t)validationLayers().size();
        ici.ppEnabledLayerNames = validationLayers().data();
    }
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS)
        throw std::runtime_error("instance creation failed");
}

// ----------------------------------------------------------------------------
// 创建 VkSurfaceKHR: 把操作系统窗口"包"成 Vulkan 能呈现的抽象表面
// ----------------------------------------------------------------------------
// Surface 是 Vulkan 与窗口系统的桥梁。glfwCreateWindowSurface 内部会:
//   - Windows: 调用 VK_KHR_win32_surface, 绑定 HWND
//   - macOS:   调用 VK_EXT_metal_surface, 绑定 CAMetalLayer
//   - Linux/X11: 调用 VK_KHR_xcb_surface
// 创建出的 surface 是后续 Swapchain 的目标——渲染好的图像最终呈现到这块表面上
// ----------------------------------------------------------------------------
void VulkanContext::createSurface(GLFWwindow* window) {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
        throw std::runtime_error("surface creation failed");
}

// ----------------------------------------------------------------------------
// 挑选物理设备 (GPU)
// ----------------------------------------------------------------------------
// 枚举系统中所有 Vulkan 物理设备, 依次检查每个设备:
//   - 打印设备名便于调试 (笔记本常有多块 GPU, 比如集显+独显)
//   - 查询设备支持的扩展, 对比我们需要的 deviceExtensions() (见 types.h,
//     通常包含 VK_KHR_SWAPCHAIN_EXTENSION_NAME, 以及 macOS 上必需的
//     VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)
//   - 若所有所需扩展都能被满足, 就选中该设备
// 注意: 这里取的是第一个满足条件的设备, 严格实现应给独显打分优先选择,
//       但 MoltenVK 下通常只有一块金属兼容 GPU, 所以取首个即可
// ----------------------------------------------------------------------------
void VulkanContext::pickPhysicalDevice() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance, &n, nullptr);
    if (n == 0) throw std::runtime_error("no suitable GPU");
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance, &n, devs.data());
    for (auto d : devs) {
        VkPhysicalDeviceProperties dp;
        vkGetPhysicalDeviceProperties(d, &dp);
        std::cout << "Found device: " << dp.deviceName << "\n" << std::flush;

        uint32_t eN = 0;
        vkEnumerateDeviceExtensionProperties(d, nullptr, &eN, nullptr);
        std::vector<VkExtensionProperties> eP(eN);
        vkEnumerateDeviceExtensionProperties(d, nullptr, &eN, eP.data());

        // 把所需扩展名丢进 set, 每匹配到一个就删一个, 全删完说明该设备支持所需全部扩展
        std::set<std::string> req(deviceExtensions().begin(), deviceExtensions().end());
        for (auto& e : eP) req.erase(e.extensionName);
        if (!req.empty()) continue;
        physicalDevice = d;
        break;
    }
    if (!physicalDevice) throw std::runtime_error("no suitable GPU");
}

// ----------------------------------------------------------------------------
// 创建逻辑设备 + 获取队列
// ----------------------------------------------------------------------------
// 逻辑设备是应用实际"操作" GPU 的句柄 (物理设备只是查询能力用的)。
// 创建过程分两步:
//   A. 查询队列族 (Queue Family):
//      GPU 上的队列按"族"分组, 每族支持不同操作:
//        - VK_QUEUE_GRAPHICS_BIT: 图形 (绘制/光栅化/清屏等)
//        - VK_QUEUE_COMPUTE_BIT:  计算着色
//        - VK_QUEUE_TRANSFER_BIT: 数据搬运
//        - 表面呈现支持: 由 vkGetPhysicalDeviceSurfaceSupportKHR 单独判断
//      同一族的队列共享命令缓冲池, 族间需要同步
//
//   B. 创建逻辑设备并索取队列:
//      用 set 去重 graphicsFamily/presentFamily, 避免重复创建相同族
//      (如果它们相同, 只提交一个 VkDeviceQueueCreateInfo 即可)
//      最后通过 vkGetDeviceQueue 拿到队列句柄, 用于后续命令提交
// ----------------------------------------------------------------------------
void VulkanContext::createLogicalDevice() {
    // A. 查询所有队列族
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, qfProps.data());

    // 找出支持图形操作的族 + 支持向当前 surface 呈现图像的族
    // 注意: 很多 GPU (尤其独显) 图形族和呈现族是同一个; 但某些实现会分开
    uint32_t gfxFamily = UINT32_MAX;
    uint32_t presentFamily = UINT32_MAX;
    for (uint32_t i = 0; i < qfCount; i++) {
        if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) gfxFamily = i;
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
        if (presentSupport) presentFamily = i;
    }
    if (gfxFamily == UINT32_MAX) throw std::runtime_error("no graphics queue family");
    // 找不到独立的呈现族时, 退而求其次复用图形族 (绝大多数情况都会落到这里)
    if (presentFamily == UINT32_MAX) presentFamily = gfxFamily;

    graphicsFamily = gfxFamily;
    presentFamily = presentFamily;

    // B. 用 set 去重, 避免 gfxFamily == presentFamily 时重复创建同一个队列
    std::set<uint32_t> uq{gfxFamily, presentFamily};
    std::vector<VkDeviceQueueCreateInfo> qci;
    float pri = 1.0f;  // 队列优先级, 这里只有一条队列所以固定 1.0
    for (uint32_t i : uq) {
        VkDeviceQueueCreateInfo c{};
        c.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        c.queueFamilyIndex = i;
        c.queueCount = 1;
        c.pQueuePriorities = &pri;
        qci.push_back(c);
    }

    VkPhysicalDeviceFeatures feat{};  // 设备特性 (各向异性/几何着色器等), 这里用默认 (全关)
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = (uint32_t)qci.size();
    dci.pQueueCreateInfos = qci.data();
    dci.pEnabledFeatures = &feat;
    // 设备级扩展: 目前主要是 VK_KHR_SWAPCHAIN_EXTENSION_NAME
    // (让设备能够创建 swapchain 来呈现图像), macOS 上还需 portability_subset
    dci.enabledExtensionCount = (uint32_t)deviceExtensions().size();
    dci.ppEnabledExtensionNames = deviceExtensions().data();

    if (vkCreateDevice(physicalDevice, &dci, nullptr, &device) != VK_SUCCESS)
        throw std::runtime_error("logical device creation failed");

    // 从刚创建的逻辑设备里取出队列句柄, 后续命令提交都用它们
    vkGetDeviceQueue(device, gfxFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
}
