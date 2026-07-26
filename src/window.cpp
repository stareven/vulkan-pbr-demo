#include "window.h"
#include <iostream>
#include <algorithm>
#include <stdexcept>

Window::Window(int width, int height, const std::string& title)
    : width(width), height(height), title(title) {}

Window::~Window() {
    destroy();
}

void Window::create() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    handle = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);

    if (!handle) {
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwShowWindow(handle);
    glfwFocusWindow(handle);
    glfwPollEvents();

#ifdef __APPLE__
    // macOS: 确保窗口在前台显示
    glfwSetWindowAttrib(handle, GLFW_FLOATING, GLFW_FALSE);
#endif

    glfwSetWindowUserPointer(handle, this);
    glfwSetFramebufferSizeCallback(handle, framebufferSizeCallback);
    glfwSetCursorPosCallback(handle, cursorPosCallback);
    glfwSetMouseButtonCallback(handle, mouseButtonCallback);
    glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void Window::pollEvents() {
    glfwPollEvents();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(handle);
}

VkSurfaceKHR Window::createSurface(VkInstance instance) {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, handle, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Surface creation failed");
    }
    return surface;
}

void Window::updateCamera() {
    // 相机更新在回调中处理
}

void Window::destroy() {
    if (handle) {
        glfwDestroyWindow(handle);
        handle = nullptr;
    }
}

void Window::framebufferSizeCallback(GLFWwindow* window, int, int) {
    auto self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    self->framebufferResized = true;
}

void Window::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self->mouseDown) {
        double dx = xpos - self->lastMouseX;
        double dy = ypos - self->lastMouseY;
        self->cameraYaw += dx * 0.005f;
        self->cameraPitch -= dy * 0.005f;
        self->cameraPitch = std::clamp(self->cameraPitch, -1.5f, 1.5f);
    }
    self->lastMouseX = xpos;
    self->lastMouseY = ypos;
}

void Window::mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    auto self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        self->mouseDown = (action == GLFW_PRESS);
        glfwGetCursorPos(window, &self->lastMouseX, &self->lastMouseY);
    }
}
