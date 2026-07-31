#pragma once

// ----------------------------------------------------------------------------
// GLFW 与 Vulkan 的集成: 让 GLFW 头文件包含 Vulkan 类型定义
// ----------------------------------------------------------------------------
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>

// ============================================================================
// 窗口管理 - GLFW 窗口的封装
// ============================================================================
// Window 类职责单一, 只负责:
//   1. 创建/销毁 GLFW 窗口 (操作系统窗口)
//   2. 处理窗口 resize 事件 (设置标志位)
//   3. 创建 VkSurfaceKHR (Vulkan 与窗口的桥梁)
//
// 注意: 相机控制和键盘/鼠标输入由 PBRApp 自己处理, 不在这里
//
// GLFW 关键概念:
//   GLFWwindow*: GLFW 窗口句柄, 封装了操作系统窗口 (HWND/CAMetalLayer/X11 Window)
//   glfwWindowHint: 设置窗口创建参数 (如是否 resizable, 使用什么图形 API)
//   glfwCreateWindowSurface: 把 GLFW 窗口包装成 Vulkan 的 VkSurfaceKHR
//   framebufferResized: 标志位, 窗口 resize 时设为 true, 触发 Swapchain 重建
// ============================================================================
class Window {
private:
    // GLFW 窗口句柄: 封装操作系统窗口
    //   - Windows: HWND
    //   - macOS: CAMetalLayer + NSWindow
    //   - Linux/X11: Window
    GLFWwindow* handle = nullptr;

    // 窗口尺寸 (像素), 用于创建时指定大小
    int width;
    int height;

    // 窗口标题 (显示在标题栏)
    std::string title;

    // 帧缓冲是否被 resize 的标志:
    //   - 用户拖拽改变窗口大小 / 全屏切换 / DPI 改变 时, GLFW 回调会设为 true
    //   - 主循环检测到这个标志后, 会触发 Swapchain 重建
    //   - 重建完成后重置为 false
    bool framebufferResized = false;

    // ImGui 窗口是否可见 (F1 键控制)
    bool showImGui = true;

public:
    // 构造函数: 只保存参数, 不创建窗口
    // 实际创建在 create() 中, 便于分离构造和初始化
    Window(int width, int height, const std::string& title);

    // 析构函数: 调用 destroy() 释放窗口资源
    ~Window();

    // 创建 GLFW 窗口:
    //   - 调用 glfwInit 初始化 GLFW
    //   - 设置窗口提示 (hint): 不使用 OpenGL, 允许 resize
    //   - 调用 glfwCreateWindow 创建窗口
    void create();

    // 轮询窗口事件:
    //   - 处理键盘/鼠标/窗口 resize 等事件
    //   - 必须每帧调用, 否则窗口会"卡死"
    void pollEvents();

    // 窗口是否应该关闭:
    //   - 用户点击关闭按钮 / 按 Esc / 调用 glfwSetWindowShouldClose
    //   - 主循环据此判断是否退出
    bool shouldClose() const;

    // 创建 Vulkan Surface:
    //   - 调用 glfwCreateWindowSurface 把 GLFW 窗口包装成 VkSurfaceKHR
    //   - instance: Vulkan 实例, Surface 是实例级对象
    //   - 返回: 创建好的 VkSurfaceKHR, 用于 Swapchain 创建
    VkSurfaceKHR createSurface(VkInstance instance);

    // ---------- Getters ----------

    // 获取 GLFW 窗口句柄, 用于注册回调/获取输入状态
    GLFWwindow* getHandle() const { return handle; }

    // 帧缓冲是否被 resize (用于触发 Swapchain 重建)
    bool isFramebufferResized() const { return framebufferResized; }

    // 重置 resize 标志 (Swapchain 重建完成后调用)
    void resetFramebufferResized() { framebufferResized = false; }

    // 设置 resize 标志 (GLFW 回调中调用)
    void notifyFramebufferResized() { framebufferResized = true; }

    // ImGui 窗口是否应该显示
    bool shouldShowImGui() const { return showImGui; }

    // 切换 ImGui 窗口显示状态 (F1 键触发)
    void toggleImGui() { showImGui = !showImGui; }

    // 销毁窗口: 释放 GLFW 窗口资源
    // 必须在 Vulkan 实例销毁前调用 (Surface 依赖窗口)
    void destroy();
};
