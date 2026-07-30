#include "window.h"
#include <stdexcept>

// ----------------------------------------------------------------------------
// 构造函数: 只保存参数, 不创建窗口
// ----------------------------------------------------------------------------
// 分离构造和创建的好处:
//   - 可以在构造后检查参数有效性
//   - 创建失败时可以抛异常, 而构造函数抛异常可能导致资源泄漏
//   - 便于延迟初始化 (比如先检查 Vulkan 是否可用)
Window::Window(int width, int height, const std::string& title)
    : width(width), height(height), title(title) {}

// ----------------------------------------------------------------------------
// 析构函数: 确保窗口资源被释放
// ----------------------------------------------------------------------------
Window::~Window() {
    destroy();
}

// ----------------------------------------------------------------------------
// 创建 GLFW 窗口
// ----------------------------------------------------------------------------
// 流程:
//   1. glfwInit: 初始化 GLFW 库 (必须在创建窗口前调用)
//   2. glfwWindowHint: 设置窗口创建参数
//      - GLFW_CLIENT_API = GLFW_NO_API: 不使用 OpenGL (我们用 Vulkan)
//      - GLFW_RESIZABLE = GLFW_TRUE: 允许用户拖拽改变窗口大小
//   3. glfwCreateWindow: 创建操作系统窗口
//      - 参数: 宽/高/标题/全屏监视器( nullptr=窗口模式)/共享上下文(nullptr=不共享)
//      - 返回: GLFWwindow* 句柄, 封装了操作系统窗口
//   4. glfwShowWindow + glfwFocusWindow: 显示并聚焦窗口
//   5. glfwPollEvents: 立即处理一次事件 (确保窗口正确显示)
//   6. macOS 特殊处理: 取消浮动属性, 确保窗口在前台
//   7. glfwSetInputMode: 设置光标模式 (NORMAL=显示光标, DISABLED=隐藏光标)
//
// 注意: 键盘/鼠标回调由 PBRApp::initWindow() 在调用 create() 后统一注册
// ----------------------------------------------------------------------------
void Window::create() {
    glfwInit();  // 初始化 GLFW

    // 窗口提示 (hints):
    //   - GLFW_CLIENT_API = GLFW_NO_API: 告诉 GLFW 不要创建 OpenGL 上下文
    //     (默认会创建, 但我们用 Vulkan, 不需要 OpenGL)
    //   - GLFW_RESIZABLE = GLFW_TRUE: 允许用户拖拽改变窗口大小
    //     (resize 时会触发 framebufferResized 标志, 触发 Swapchain 重建)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // 创建窗口:
    //   - 宽/高: 像素单位
    //   - 标题: 显示在窗口标题栏
    //   - 监视器: nullptr 表示窗口模式 (非全屏)
    //   - 共享: nullptr 表示不与其他窗口共享 OpenGL 上下文 (Vulkan 不需要)
    handle = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);

    if (!handle) {
        throw std::runtime_error("Failed to create GLFW window");
    }

    // 显示并聚焦窗口, 然后立即处理一次事件
    glfwShowWindow(handle);
    glfwFocusWindow(handle);
    glfwPollEvents();

#ifdef __APPLE__
    // macOS 特殊处理: 取消浮动属性
    //   - MoltenVK 创建的窗口默认可能是浮动的, 这里强制取消
    //   - 确保窗口在正常层级显示, 不被其他窗口遮挡
    glfwSetWindowAttrib(handle, GLFW_FLOATING, GLFW_FALSE);
#endif

    // 设置光标模式: GLFW_CURSOR_NORMAL 表示显示并允许光标自由移动
    //   - GLFW_CURSOR_DISABLED: 隐藏光标并锁定 (FPS 游戏常用)
    //   - GLFW_CURSOR_HIDDEN: 隐藏光标但不锁定
    // 注意: 键盘/鼠标回调由 PBRApp::initWindow() 注册, 不在这里
    glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

// ----------------------------------------------------------------------------
// 轮询窗口事件
// ----------------------------------------------------------------------------
// 必须每帧调用, 作用:
//   - 检查键盘/鼠标/手柄输入
//   - 处理窗口 resize/移动/最小化等事件
//   - 触发已注册的回调函数 (如 framebuffer resize callback)
// 如果不调用, 窗口会"卡死" (操作系统认为程序无响应)
// ----------------------------------------------------------------------------
void Window::pollEvents() {
    glfwPollEvents();
}

// ----------------------------------------------------------------------------
// 判断窗口是否应该关闭
// ----------------------------------------------------------------------------
// 返回 true 的情况:
//   - 用户点击窗口关闭按钮 (X)
//   - 用户按 Esc 键 (默认行为, 可通过回调禁用)
//   - 代码调用 glfwSetWindowShouldClose(handle, GLFW_TRUE)
// 主循环据此判断是否退出
// ----------------------------------------------------------------------------
bool Window::shouldClose() const {
    return glfwWindowShouldClose(handle);
}

// ----------------------------------------------------------------------------
// 创建 Vulkan Surface
// ----------------------------------------------------------------------------
// Surface 是 Vulkan 与窗口系统的桥梁:
//   - Vulkan 渲染的图像最终要"呈现"到 Surface 上
//   - Swapchain 创建时必须指定 Surface
//   - Surface 由 GLFW 创建, 内部调用平台特定 API:
//     * Windows: VK_KHR_win32_surface + HWND
//     * macOS: VK_EXT_metal_surface + CAMetalLayer
//     * Linux/X11: VK_KHR_xcb_surface + X11 Window
//
// 参数:
//   - instance: Vulkan 实例, Surface 是实例级对象 (属于 Instance, 不属于 Device)
// 返回:
//   - VkSurfaceKHR: 创建好的 Surface 句柄
// ----------------------------------------------------------------------------
VkSurfaceKHR Window::createSurface(VkInstance instance) {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, handle, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Surface creation failed");
    }
    return surface;
}

// ----------------------------------------------------------------------------
// 销毁窗口
// ----------------------------------------------------------------------------
// 释放 GLFW 窗口资源:
//   - 调用 glfwDestroyWindow 销毁操作系统窗口
//   - 置空句柄, 避免重复销毁
// 注意: 必须在 Vulkan Instance 销毁前调用
//   (因为 Surface 依赖窗口, Instance 销毁后 Surface 也失效)
// ----------------------------------------------------------------------------
void Window::destroy() {
    if (handle) {
        glfwDestroyWindow(handle);
        handle = nullptr;
    }
}
