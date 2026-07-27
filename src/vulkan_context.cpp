#include "vulkan_context.h"
#include "vulkan_utils.h"
#include "types.h"

#include <iostream>
#include <set>
#include <stdexcept>

using namespace config;
using namespace vulkan;

void VulkanContext::initialize(GLFWwindow* window) {
    if (ENABLE_VALIDATION && !checkLayerSupport(validationLayers()))
        throw std::runtime_error("validation layer unavailable");

    createInstance();
    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();
}

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

void VulkanContext::createInstance() {
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
        ici.enabledLayerCount = (uint32_t)validationLayers().size();
        ici.ppEnabledLayerNames = validationLayers().data();
    }
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS)
        throw std::runtime_error("instance creation failed");
}

void VulkanContext::createSurface(GLFWwindow* window) {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
        throw std::runtime_error("surface creation failed");
}

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

        std::set<std::string> req(deviceExtensions().begin(), deviceExtensions().end());
        for (auto& e : eP) req.erase(e.extensionName);
        if (!req.empty()) continue;
        physicalDevice = d;
        break;
    }
    if (!physicalDevice) throw std::runtime_error("no suitable GPU");
}

void VulkanContext::createLogicalDevice() {
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, qfProps.data());

    uint32_t gfxFamily = UINT32_MAX;
    uint32_t presentFamily = UINT32_MAX;
    for (uint32_t i = 0; i < qfCount; i++) {
        if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) gfxFamily = i;
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
        if (presentSupport) presentFamily = i;
    }
    if (gfxFamily == UINT32_MAX) throw std::runtime_error("no graphics queue family");
    if (presentFamily == UINT32_MAX) presentFamily = gfxFamily;

    graphicsFamily = gfxFamily;
    presentFamily = presentFamily;

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
    dci.enabledExtensionCount = (uint32_t)deviceExtensions().size();
    dci.ppEnabledExtensionNames = deviceExtensions().data();

    if (vkCreateDevice(physicalDevice, &dci, nullptr, &device) != VK_SUCCESS)
        throw std::runtime_error("logical device creation failed");

    vkGetDeviceQueue(device, gfxFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
}
