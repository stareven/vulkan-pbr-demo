#include "pbr_app.h"
#include "vulkan_utils.h"
#include "mesh.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

PBRApp::PBRApp(int argc, char* argv[]) {
    try {
        exeDir = std::filesystem::canonical(argv[0]).parent_path();
    } catch (...) {
        exeDir = std::filesystem::current_path();
    }
}

// ============================================================================
// Window
// ============================================================================
void PBRApp::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window = glfwCreateWindow(WIDTH, HEIGHT, TITLE, nullptr, nullptr);

    if (!window) {
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwShowWindow(window);
    glfwFocusWindow(window);
    glfwPollEvents();

#ifdef __APPLE__
    // macOS: 确保窗口在前台显示
    glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_FALSE);
#endif

    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int) {
        reinterpret_cast<PBRApp*>(glfwGetWindowUserPointer(w))->fbResized = true;
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        auto s = reinterpret_cast<PBRApp*>(glfwGetWindowUserPointer(w));
        if (s->leftDown) {
            double dx = x - s->lastMX, dy = y - s->lastMY;
            s->camYaw += dx * 0.005f;
            s->camPitch -= dy * 0.005f;
            s->camPitch = std::clamp(s->camPitch, -1.5f, 1.5f);
        }
        s->lastMX = x;
        s->lastMY = y;
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int btn, int act, int) {
        auto s = reinterpret_cast<PBRApp*>(glfwGetWindowUserPointer(w));
        if (btn == GLFW_MOUSE_BUTTON_LEFT) {
            s->leftDown = (act == GLFW_PRESS);
            glfwGetCursorPos(w, &s->lastMX, &s->lastMY);
        }
    });
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

// ============================================================================
// Vulkan Init
// ============================================================================
void PBRApp::initVulkan() {
    if (ENABLE_VALIDATION && !checkLayerSupport(VALIDATION_LAYERS))
        throw std::runtime_error("validation layer unavailable");

    // Instance
    VkApplicationInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "PBR Demo";
    ai.apiVersion = VK_API_VERSION_1_1;

    uint32_t extCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
    std::vector<const char*> exts(glfwExts, glfwExts + extCount);
    exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = (uint32_t)exts.size();
    ici.ppEnabledExtensionNames = exts.data();
    ici.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    if (ENABLE_VALIDATION) {
        ici.enabledLayerCount = (uint32_t)VALIDATION_LAYERS.size();
        ici.ppEnabledLayerNames = VALIDATION_LAYERS.data();
    }
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS)
        throw std::runtime_error("instance creation failed");

    // Physical device
    {
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

            std::set<std::string> req(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());
            for (auto& e : eP) req.erase(e.extensionName);
            if (!req.empty()) continue;
            pd = d;
            break;
        }
        if (!pd) throw std::runtime_error("no suitable GPU");
    }

    // Surface
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
        throw std::runtime_error("surface creation failed");

    // Logical device
    {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfProps(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, qfProps.data());

        uint32_t gfxFamily = UINT32_MAX;
        uint32_t presentFamily = UINT32_MAX;
        for (uint32_t i = 0; i < qfCount; i++) {
            if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) gfxFamily = i;
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &presentSupport);
            if (presentSupport) presentFamily = i;
        }
        if (gfxFamily == UINT32_MAX) throw std::runtime_error("no graphics queue family");
        if (presentFamily == UINT32_MAX) presentFamily = gfxFamily;

        std::set<uint32_t> uq{gfxFamily, presentFamily};
        std::vector<VkDeviceQueueCreateInfo> qci;
        float pri = 1.0f;
        for (uint32_t i : uq) {
            VkDeviceQueueCreateInfo c{};
            c.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            c.queueFamilyIndex = i;
            c.queueCount = 1;
            c.pQueuePriorities = &pri;
            qci.push_back(c);
        }
        VkPhysicalDeviceFeatures feat{};
        VkDeviceCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = (uint32_t)qci.size();
        dci.pQueueCreateInfos = qci.data();
        dci.pEnabledFeatures = &feat;
        dci.enabledExtensionCount = (uint32_t)DEVICE_EXTENSIONS.size();
        dci.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();

        if (vkCreateDevice(pd, &dci, nullptr, &device) != VK_SUCCESS)
            throw std::runtime_error("logical device creation failed");

        vkGetDeviceQueue(device, gfxFamily, 0, &gfxQueue);
        vkGetDeviceQueue(device, presentFamily, 0, &presQueue);
    }

    createSwapchain();
    createImageViews();
    createDepthBuffer();
    createRenderPass();
    createDescriptorLayouts();

    // Shadow map resources (需要 descriptor layout 在 graphics pipeline 之前)
    createShadowMap();
    createShadowRenderPass();
    createShadowDescriptorLayout();
    createShadowSampler();
    createShadowSamplerDescriptorLayout();

    createGraphicsPipeline();
    createShadowPipeline();
    createShadowFramebuffer();
    createFramebuffers();
    createCommandPool();
    createMesh();

    createUniformBuffers();

    // 需要先创建 sync objects 来获取 imageCount
    createSyncObjects();

    createDescriptorPool();
    createDescriptorSets();
    createShadowDescriptorSets();
    createShadowSamplerDescriptorSets();
    createCommandBuffers();
}
