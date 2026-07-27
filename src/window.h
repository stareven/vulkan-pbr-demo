#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>

// ============================================================================
// 窗口管理 - 仅负责 GLFW 窗口创建、surface、resize 标志
// (相机/输入由 PBRApp 自己处理)
// ============================================================================
class Window {
private:
    GLFWwindow* handle = nullptr;
    int width;
    int height;
    std::string title;

    bool framebufferResized = false;

public:
    Window(int width, int height, const std::string& title);
    ~Window();

    void create();
    void pollEvents();
    bool shouldClose() const;
    VkSurfaceKHR createSurface(VkInstance instance);

    // 窗口状态
    GLFWwindow* getHandle() const { return handle; }
    bool isFramebufferResized() const { return framebufferResized; }
    void resetFramebufferResized() { framebufferResized = false; }

    void destroy();

private:
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
};
