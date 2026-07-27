#include "window.h"
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
