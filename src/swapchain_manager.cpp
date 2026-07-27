#include "swapchain_manager.h"
#include "vulkan_utils.h"
#include <algorithm>
#include <stdexcept>
using namespace vulkan;

SwapchainManager::~SwapchainManager() {
    // Cleanup should be called explicitly
}

void SwapchainManager::create(VkSurfaceKHR surface, GLFWwindow* window) {
    createSwapchainInternal(surface, window);
    createImageViews();
    createDepthBuffer();
}

void SwapchainManager::recreate(VkSurfaceKHR surface, GLFWwindow* window) {
    cleanup();
    create(surface, window);
}

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

void SwapchainManager::createSwapchainInternal(VkSurfaceKHR surface, GLFWwindow* window) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);

    uint32_t fmtN = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &fmtN, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts;
    if (fmtN == 0) {
        fmts.push_back({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
        fmtN = 1;
    } else {
        fmts.resize(fmtN);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &fmtN, fmts.data());
    }

    uint32_t pmN = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &pmN, nullptr);
    std::vector<VkPresentModeKHR> pms;
    if (pmN == 0) {
        pms.push_back(VK_PRESENT_MODE_FIFO_KHR);
        pmN = 1;
    } else {
        pms.resize(pmN);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &pmN, pms.data());
    }

    VkSurfaceFormatKHR sf = fmts[0];
    for (auto& f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            sf = f;
            break;
        }

    VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : pms)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) { pm = m; break; }

    VkExtent2D ext = caps.currentExtent;
    if (ext.width == UINT32_MAX || ext.width < 16 || ext.height < 16) {
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        if (w > 0 && h > 0) {
            ext.width = (uint32_t)w;
            ext.height = (uint32_t)h;
        } else {
            ext.width = 1280;
            ext.height = 720;
        }
    }

    uint32_t imgN = caps.minImageCount + 1;
    if (caps.maxImageCount && imgN > caps.maxImageCount) imgN = caps.maxImageCount;

    auto q = findQueues(physicalDevice, surface);
    if (!q.present) q.present = q.gfx;
    uint32_t families[] = {q.gfx.value(), q.present.value()};

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface;
    sci.minImageCount = imgN;
    sci.imageFormat = sf.format;
    sci.imageColorSpace = sf.colorSpace;
    sci.imageExtent = ext;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode =
        (q.gfx != q.present) ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
    if (sci.imageSharingMode == VK_SHARING_MODE_CONCURRENT) {
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = families;
    }
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = pm;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain) != VK_SUCCESS)
        throw std::runtime_error("swapchain creation failed");

    vkGetSwapchainImagesKHR(device, swapchain, &imgN, nullptr);
    images.resize(imgN);
    vkGetSwapchainImagesKHR(device, swapchain, &imgN, images.data());
    format = sf.format;
    extent = ext;
}

void SwapchainManager::createImageViews() {
    imageViews.resize(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        VkImageViewCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image = images[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = format;
        ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device, &ci, nullptr, &imageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("image view creation failed");
    }
}

void SwapchainManager::createDepthBuffer() {
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_D32_SFLOAT;
    ii.extent = {extent.width, extent.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &ii, nullptr, &depthImage) != VK_SUCCESS)
        throw std::runtime_error("depth image creation failed");

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device, depthImage, &mr);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &mp);
    uint32_t ti = UINT32_MAX;
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

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = depthImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_D32_SFLOAT;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &vi, nullptr, &depthImageView) != VK_SUCCESS)
        throw std::runtime_error("depth image view failed");
}
