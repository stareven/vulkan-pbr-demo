#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>
#include "math_utils.h"

// ============================================================================
// 窗口管理 - 窗口创建、事件处理、相机控制
// ============================================================================
class Window {
private:
    GLFWwindow* handle = nullptr;
    int width;
    int height;
    std::string title;

    // 相机状态
    Vec3 cameraPos{0, 2, 5};
    float cameraYaw = M_PI;
    float cameraPitch = -0.5f;

    // 输入状态
    bool mouseDown = false;
    double lastMouseX = 0;
    double lastMouseY = 0;

    bool framebufferResized = false;

public:
    Window(int width, int height, const std::string& title);
    ~Window();

    void create();
    void pollEvents();
    bool shouldClose() const;
    VkSurfaceKHR createSurface(VkInstance instance);

    // 相机控制
    void updateCamera();
    const Vec3& getCameraPosition() const { return cameraPos; }
    float getCameraYaw() const { return cameraYaw; }
    float getCameraPitch() const { return cameraPitch; }

    // 窗口状态
    GLFWwindow* getHandle() const { return handle; }
    bool isFramebufferResized() const { return framebufferResized; }
    void resetFramebufferResized() { framebufferResized = false; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }

    void destroy();

private:
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
};
