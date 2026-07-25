#include "pbr_app.h"
#include "vulkan_utils.h"
#include "mesh.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

// ============================================================================
// Window
// ============================================================================
void PBRApp::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window = glfwCreateWindow(WIDTH, HEIGHT, TITLE, nullptr, nullptr);

    glfwShowWindow(window);
    glfwPollEvents();

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
        VkResult enumResult = vkEnumeratePhysicalDevices(instance, &n, nullptr);
        std::cout << "vkEnumeratePhysicalDevices result: " << enumResult
                  << ", count: " << n << "\n";
        if (n == 0) {
            std::cerr << "ERROR: no Vulkan physical devices found!\n";
            throw std::runtime_error("no suitable GPU");
        }
        std::vector<VkPhysicalDevice> devs(n);
        vkEnumeratePhysicalDevices(instance, &n, devs.data());
        for (auto d : devs) {
            VkPhysicalDeviceProperties dp;
            vkGetPhysicalDeviceProperties(d, &dp);
            std::cout << "Found device: " << dp.deviceName << "\n";

            uint32_t eN = 0;
            vkEnumerateDeviceExtensionProperties(d, nullptr, &eN, nullptr);
            std::vector<VkExtensionProperties> eP(eN);
            vkEnumerateDeviceExtensionProperties(d, nullptr, &eN, eP.data());
            std::cout << "  Extensions: " << eN << "\n";

            std::set<std::string> req(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());
            for (auto& e : eP) req.erase(e.extensionName);
            if (!req.empty()) {
                std::cout << "  Missing extensions:\n";
                for (auto& r : req) std::cout << "    - " << r << "\n";
                continue;
            }
            pd = d;
            std::cout << "  Selected!\n";
            break;
        }
        if (!pd) throw std::runtime_error("no suitable GPU");
    }
    std::cerr << "[DEBUG] Physical device selected\n";
    std::cerr.flush();

    // Surface
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
        throw std::runtime_error("surface creation failed");
    std::cerr << "[DEBUG] Surface created\n";
    std::cerr.flush();

    // Logical device
    {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfProps(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, qfProps.data());

        uint32_t gfxFamily = UINT32_MAX;
        uint32_t presentFamily = UINT32_MAX;
        std::cerr << "[DEBUG] Checking " << qfCount << " queue families...\n";
        for (uint32_t i = 0; i < qfCount; i++) {
            std::cerr << "[DEBUG] Queue family " << i << ": flags=" << std::hex
                      << qfProps[i].queueFlags << std::dec << "\n";
            if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                gfxFamily = i;
                std::cerr << "[DEBUG]   -> Graphics queue\n";
            }
            VkBool32 presentSupport = VK_FALSE;
            VkResult surfResult =
                vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &presentSupport);
            std::cerr << "[DEBUG]   -> Surface support query result: " << surfResult
                      << ", support: " << presentSupport << "\n";
            if (presentSupport) presentFamily = i;
        }
        if (gfxFamily == UINT32_MAX)
            throw std::runtime_error("no graphics queue family");

        if (presentFamily == UINT32_MAX) {
            std::cerr << "[DEBUG] No present queue found, using graphics queue ("
                      << gfxFamily << ")\n";
            presentFamily = gfxFamily;
        }

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

        std::cerr << "[DEBUG] About to create logical device...\n";
        std::cerr.flush();
        VkResult devResult = vkCreateDevice(pd, &dci, nullptr, &device);
        std::cerr << "[DEBUG] vkCreateDevice result: " << devResult << "\n";
        std::cerr.flush();
        if (devResult != VK_SUCCESS)
            throw std::runtime_error("logical device creation failed");

        vkGetDeviceQueue(device, gfxFamily, 0, &gfxQueue);
        vkGetDeviceQueue(device, presentFamily, 0, &presQueue);
    }
    std::cerr << "[DEBUG] Logical device created\n" << std::flush;

    std::cerr << "[DEBUG] creating swapchain...\n" << std::flush;
    createSwapchain();
    std::cerr << "[DEBUG] swapchain done\n" << std::flush;
    createImageViews();
    std::cerr << "[DEBUG] image views done\n" << std::flush;
    createDepthBuffer();
    std::cerr << "[DEBUG] depth buffer done\n" << std::flush;
    createRenderPass();
    std::cerr << "[DEBUG] render pass done\n" << std::flush;
    createDescriptorLayouts();
    std::cerr << "[DEBUG] descriptor layouts done\n" << std::flush;
    createGraphicsPipeline();
    std::cerr << "[DEBUG] graphics pipeline done\n" << std::flush;
    createFramebuffers();
    std::cerr << "[DEBUG] framebuffers done\n" << std::flush;
    createCommandPool();
    std::cerr << "[DEBUG] command pool done\n" << std::flush;
    createMesh();
    std::cerr << "[DEBUG] mesh done\n" << std::flush;
    createUniformBuffers();
    std::cerr << "[DEBUG] uniform buffers done\n" << std::flush;
    createDescriptorPool();
    std::cerr << "[DEBUG] descriptor pool done\n" << std::flush;
    createDescriptorSets();
    std::cerr << "[DEBUG] descriptor sets done\n" << std::flush;
    createCommandBuffers();
    std::cerr << "[DEBUG] command buffers done\n" << std::flush;
    createSyncObjects();
    std::cerr << "[DEBUG] sync objects done\n" << std::flush;
    std::cerr << "[DEBUG] About to enter mainLoop...\n" << std::flush;
    std::cerr << "[DEBUG] Init complete!\n" << std::flush;
}

// ============================================================================
// Swapchain
// ============================================================================
void PBRApp::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surface, &caps);

    uint32_t fmtN = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &fmtN, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts;
    if (fmtN == 0) {
        fmts.push_back({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
        fmtN = 1;
    } else {
        fmts.resize(fmtN);
        vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &fmtN, fmts.data());
    }

    uint32_t pmN = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &pmN, nullptr);
    std::vector<VkPresentModeKHR> pms;
    if (pmN == 0) {
        pms.push_back(VK_PRESENT_MODE_FIFO_KHR);
        pmN = 1;
    } else {
        pms.resize(pmN);
        vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &pmN, pms.data());
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

    auto q = findQueues(pd, surface);
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
    scImages.resize(imgN);
    vkGetSwapchainImagesKHR(device, swapchain, &imgN, scImages.data());
    scFormat = sf.format;
    scExtent = ext;
}

void PBRApp::createImageViews() {
    scImageViews.resize(scImages.size());
    for (size_t i = 0; i < scImages.size(); ++i) {
        VkImageViewCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image = scImages[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = scFormat;
        ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device, &ci, nullptr, &scImageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("image view creation failed");
    }
}

// ============================================================================
// Depth buffer
// ============================================================================
void PBRApp::createDepthBuffer() {
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_D32_SFLOAT;
    ii.extent = {scExtent.width, scExtent.height, 1};
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
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
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

// ============================================================================
// Render pass
// ============================================================================
void PBRApp::createRenderPass() {
    std::array<VkAttachmentDescription, 2> att{};
    // Color
    att[0].format = scFormat;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    // Depth
    att[1].format = VK_FORMAT_D32_SFLOAT;
    att[1].samples = VK_SAMPLE_COUNT_1_BIT;
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = (uint32_t)att.size();
    rpi.pAttachments = att.data();
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    rpi.dependencyCount = 1;
    rpi.pDependencies = &dep;

    if (vkCreateRenderPass(device, &rpi, nullptr, &renderPass) != VK_SUCCESS)
        throw std::runtime_error("render pass creation failed");
}

// ============================================================================
// Descriptor layouts
// ============================================================================
void PBRApp::createDescriptorLayouts() {
    // 空layout，不绑定任何UBO
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 0;
    li.pBindings = nullptr;
    if (vkCreateDescriptorSetLayout(device, &li, nullptr, &dslMVP) != VK_SUCCESS)
        throw std::runtime_error("dsl mvp failed");
    if (vkCreateDescriptorSetLayout(device, &li, nullptr, &dslMat) != VK_SUCCESS)
        throw std::runtime_error("dsl mat failed");
}

// ============================================================================
// Graphics pipeline
// ============================================================================
void PBRApp::createGraphicsPipeline() {
    auto vs = readFile("shaders/shader.vert.spv");
    auto fs = readFile("shaders/shader.frag.spv");
    auto vsm = makeShaderModule(device, vs);
    auto fsm = makeShaderModule(device, fs);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsm;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fsm;
    stages[1].pName = "main";

    // Vertex input - 不使用，完全依赖 gl_VertexIndex
    VkPipelineVertexInputStateCreateInfo vici{};
    vici.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vici.vertexBindingDescriptionCount = 0;
    vici.pVertexBindingDescriptions = nullptr;
    vici.vertexAttributeDescriptionCount = 0;
    vici.pVertexAttributeDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo iaci{};
    iaci.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    iaci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vsci{};
    vsci.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vsci.viewportCount = vsci.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rsci{};
    rsci.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rsci.polygonMode = VK_POLYGON_MODE_FILL;
    rsci.lineWidth = 1.0f;
    rsci.cullMode = VK_CULL_MODE_NONE;
    rsci.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo msci{};
    msci.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo dsci{};
    dsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsci.depthTestEnable = dsci.depthWriteEnable = VK_TRUE;
    dsci.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cbci{};
    cbci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbci.attachmentCount = 1;
    cbci.pAttachments = &cba;

    std::vector<VkDynamicState> ds = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dsci2{};
    dsci2.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dsci2.dynamicStateCount = (uint32_t)ds.size();
    dsci2.pDynamicStates = ds.data();

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 0;
    pli.pSetLayouts = nullptr;
    if (vkCreatePipelineLayout(device, &pli, nullptr, &pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("pipeline layout failed");

    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount = 2;
    gpi.pStages = stages;
    gpi.pVertexInputState = &vici;
    gpi.pInputAssemblyState = &iaci;
    gpi.pViewportState = &vsci;
    gpi.pRasterizationState = &rsci;
    gpi.pMultisampleState = &msci;
    gpi.pDepthStencilState = &dsci;
    gpi.pColorBlendState = &cbci;
    gpi.pDynamicState = &dsci2;
    gpi.layout = pipelineLayout;
    gpi.renderPass = renderPass;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpi, nullptr, &pipeline) !=
        VK_SUCCESS)
        throw std::runtime_error("graphics pipeline failed");

    vkDestroyShaderModule(device, vsm, nullptr);
    vkDestroyShaderModule(device, fsm, nullptr);
}

void PBRApp::createFramebuffers() {
    framebuffers.resize(scImageViews.size());
    for (size_t i = 0; i < scImageViews.size(); ++i) {
        VkImageView att[] = {scImageViews[i], depthImageView};
        VkFramebufferCreateInfo fbi{};
        fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbi.renderPass = renderPass;
        fbi.attachmentCount = 2;
        fbi.pAttachments = att;
        fbi.width = scExtent.width;
        fbi.height = scExtent.height;
        fbi.layers = 1;
        if (vkCreateFramebuffer(device, &fbi, nullptr, &framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("framebuffer failed");
    }
}

void PBRApp::createCommandPool() {
    auto q = findQueues(pd, surface);
    VkCommandPoolCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = q.gfx.value();
    if (vkCreateCommandPool(device, &cpi, nullptr, &cmdPool) != VK_SUCCESS)
        throw std::runtime_error("command pool failed");
}

// ============================================================================
// Mesh
// ============================================================================
void PBRApp::createMesh() {
    auto verts = generateTriangle();
    auto idxs = generateTriangleIndices();
    indexCount = (uint32_t)idxs.size();

    VkDeviceSize vboSz = verts.size() * sizeof(Vertex);
    VkDeviceSize iboSz = idxs.size() * sizeof(uint32_t);

    VkBuffer stg;
    VkDeviceMemory stgMem;
    createBuffer(device, pd, vboSz + iboSz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, stg, stgMem);
    void* mapped;
    vkMapMemory(device, stgMem, 0, vboSz + iboSz, 0, &mapped);
    std::memcpy(mapped, verts.data(), vboSz);
    std::memcpy((char*)mapped + vboSz, idxs.data(), iboSz);
    vkUnmapMemory(device, stgMem);

    createBuffer(device, pd, vboSz,
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vbo, vboMem);
    createBuffer(device, pd, iboSz,
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ibo, iboMem);

    copyBuffer(device, gfxQueue, cmdPool, stg, vbo, vboSz);
    copyBuffer(device, gfxQueue, cmdPool, stg, ibo, iboSz);

    vkDestroyBuffer(device, stg, nullptr);
    vkFreeMemory(device, stgMem, nullptr);
}

// ============================================================================
// Uniform buffers
// ============================================================================
void PBRApp::createUniformBuffers() {
    uboMVPBuf.resize(MAX_FRAMES_IN_FLIGHT);
    uboMVPMem.resize(MAX_FRAMES_IN_FLIGHT);
    uboMatBuf.resize(MAX_FRAMES_IN_FLIGHT);
    uboMatMem.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        createBuffer(device, pd, sizeof(UBO_MVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, uboMVPBuf[i], uboMVPMem[i]);
        createBuffer(device, pd, sizeof(UBO_Material), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, uboMatBuf[i], uboMatMem[i]);
    }
}

// ============================================================================
// Descriptor pool & sets
// ============================================================================
void PBRApp::createDescriptorPool() {
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)(MAX_FRAMES_IN_FLIGHT * 2)}};
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = MAX_FRAMES_IN_FLIGHT * 2;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device, &dpi, nullptr, &descPool) != VK_SUCCESS)
        throw std::runtime_error("descriptor pool failed");
}

void PBRApp::createDescriptorSets() {
    // 跳过，不再使用 descriptor sets
    descSetsMVP.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    descSetsMat.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
}

// ============================================================================
// Command buffers
// ============================================================================
void PBRApp::createCommandBuffers() {
    cmdBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = (uint32_t)cmdBuffers.size();
    if (vkAllocateCommandBuffers(device, &ai, cmdBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("command buffer alloc failed");
}

void PBRApp::recordCommandBuffer(uint32_t imgIdx) {
    VkCommandBuffer cmd = cmdBuffers[frameIdx];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    VkClearValue clears[2];
    clears[0].color = {{0.2f, 0.02f, 0.03f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = renderPass;
    rpi.framebuffer = framebuffers[imgIdx];
    rpi.renderArea.extent = scExtent;
    rpi.clearValueCount = 2;
    rpi.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkViewport vp{0, 0, (float)scExtent.width, (float)scExtent.height, 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, scExtent};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

// ============================================================================
// Sync
// ============================================================================
void PBRApp::createSyncObjects() {
    semImgAvail.resize(MAX_FRAMES_IN_FLIGHT);
    semRendDone.resize(MAX_FRAMES_IN_FLIGHT);
    fences.resize(MAX_FRAMES_IN_FLIGHT);
    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(device, &sci, nullptr, &semImgAvail[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &sci, nullptr, &semRendDone[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fci, nullptr, &fences[i]) != VK_SUCCESS)
            throw std::runtime_error("sync object creation failed");
    }
}

// ============================================================================
// UBO updates
// ============================================================================
void PBRApp::updateUBOs() {
    float cx = std::cos(camPitch) * std::sin(camYaw);
    float cy = std::sin(camPitch);
    float cz = std::cos(camPitch) * std::cos(camYaw);
    Vec3 camDir{cx, cy, cz};
    Vec3 target = camPos + camDir;
    Vec3 up{0, 1, 0};

    Mat4 view = Mat4::lookAt(camPos, target, up);
    float aspect = (float)scExtent.width / scExtent.height;
    Mat4 proj = Mat4::perspective(45.0f * M_PI / 180.0f, aspect, 0.1f, 100.0f);
    Mat4 model = Mat4::identity();

    UBO_MVP mvp{model, view, proj, camPos};
    void* p;
    vkMapMemory(device, uboMVPMem[frameIdx], 0, sizeof(mvp), 0, &p);
    std::memcpy(p, &mvp, sizeof(mvp));
    vkUnmapMemory(device, uboMVPMem[frameIdx]);

    struct MatPreset {
        Vec3 albedo;
        float metallic;
        float roughness;
    };
    MatPreset presets[] = {
        {{0.95f, 0.35f, 0.10f}, 0.0f, 0.7f},   // 红色塑料
        {{0.95f, 0.93f, 0.88f}, 1.0f, 0.1f},   // 银
        {{1.00f, 0.77f, 0.33f}, 1.0f, 0.3f},   // 金
        {{0.97f, 0.96f, 0.91f}, 1.0f, 0.2f},   // 铝
        {{0.30f, 0.85f, 0.39f}, 0.0f, 0.4f},   // 绿色塑料
        {{0.50f, 0.50f, 0.50f}, 1.0f, 0.5f},   // 铁
        {{0.98f, 0.99f, 1.00f}, 0.0f, 0.05f},  // 玻璃
    };
    auto& pr = presets[matPreset % 7];

    UBO_Material mat{};
    mat.albedo = pr.albedo;
    mat.metallic = pr.metallic;
    mat.roughness = pr.roughness;
    mat.ao = 1.0f;
    mat.ior = 1.5f;
    mat.opacity = 1.0f;
    if ((matPreset % 7) == 6 && glassEnabled) {
        mat.ior = 1.52f;
        mat.opacity = 0.3f;
    }
    mat.cameraPos = camPos;
    mat.ambientLight = {0.03f, 0.03f, 0.03f};
    mat.lights[0] = {{10, 10, 10}, {300, 300, 300}, 1.0f};
    mat.lights[1] = {{-10, 10, 10}, {300, 100, 100}, 1.0f};
    mat.lights[2] = {{10, -10, -10}, {100, 300, 100}, 1.0f};
    mat.lights[3] = {{-10, -10, -10}, {100, 100, 300}, 1.0f};

    if (emissiveEnabled) {
        mat.emissive = {1.0f, 0.5f, 0.1f};
        mat.emissiveStrength = 2.0f;
    } else {
        mat.emissive = {0.0f, 0.0f, 0.0f};
        mat.emissiveStrength = 0.0f;
    }

    vkMapMemory(device, uboMatMem[frameIdx], 0, sizeof(mat), 0, &p);
    std::memcpy(p, &mat, sizeof(mat));
    vkUnmapMemory(device, uboMatMem[frameIdx]);
}

// ============================================================================
// Main loop / draw
// ============================================================================
void PBRApp::mainLoop() {
    std::cout << "Entering main loop...\n" << std::flush;
    int loopCount = 0;
    while (!glfwWindowShouldClose(window)) {
        loopCount++;
        if (loopCount % 100 == 0) std::cout << "Loop iteration " << loopCount << "\n" << std::flush;
        glfwPollEvents();

        float dt = 0.016f;
        float speed = 5.0f * dt;
        Vec3 fwd = (Vec3{std::cos(camPitch) * std::sin(camYaw), std::sin(camPitch),
                          std::cos(camPitch) * std::cos(camYaw)})
                        .normalize();
        Vec3 right = fwd.cross({0, 1, 0}).normalize();
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camPos = camPos + fwd * speed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camPos = camPos - fwd * speed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camPos = camPos - right * speed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camPos = camPos + right * speed;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) camPos.y -= speed;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) camPos.y += speed;

        // 玻璃 (G)
        static bool gLast = false;
        bool gNow = glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS;
        if (gNow && !gLast) {
            glassEnabled = !glassEnabled;
            std::cout << "Glass: " << (glassEnabled ? "ON" : "OFF") << "\n";
        }
        gLast = gNow;

        // 自发光 (F)
        static bool eLast = false;
        bool eNow = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        if (eNow && !eLast) {
            emissiveEnabled = !emissiveEnabled;
            std::cout << "Emissive: " << (emissiveEnabled ? "ON" : "OFF") << "\n";
        }
        eLast = eNow;

        drawFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    vkDeviceWaitIdle(device);
}

void PBRApp::drawFrame() {
    static int frameCount = 0;
    if (frameCount == 0) {
        std::cout << "First drawFrame() call\n";
        std::cout << "Camera position: " << camPos.x << ", " << camPos.y << ", " << camPos.z << "\n";
    }
    if (frameCount % 60 == 0) {
        std::cout << "Frame " << frameCount << "\n";
    }
    frameCount++;

    vkWaitForFences(device, 1, &fences[frameIdx], VK_TRUE, UINT64_MAX);

    uint32_t imgIdx;
    VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                       semImgAvail[frameIdx], VK_NULL_HANDLE, &imgIdx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || fbResized) {
        fbResized = false;
        recreateSwapchain();
        return;
    }

    updateUBOs();
    recordCommandBuffer(imgIdx);

    vkResetFences(device, 1, &fences[frameIdx]);

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore wait[] = {semImgAvail[frameIdx]};
    VkSemaphore sig[] = {semRendDone[frameIdx]};

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = wait;
    si.pWaitDstStageMask = waitStages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmdBuffers[frameIdx];
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = sig;

    if (vkQueueSubmit(gfxQueue, 1, &si, fences[frameIdx]) != VK_SUCCESS)
        throw std::runtime_error("queue submit failed");

    VkSwapchainKHR sw[] = {swapchain};
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = sig;
    pi.swapchainCount = 1;
    pi.pSwapchains = sw;
    pi.pImageIndices = &imgIdx;
    r = vkQueuePresentKHR(presQueue, &pi);

    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || fbResized) {
        fbResized = false;
        recreateSwapchain();
    }

    frameIdx = (frameIdx + 1) % MAX_FRAMES_IN_FLIGHT;
}

void PBRApp::recreateSwapchain() {
    vkDeviceWaitIdle(device);
    for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    for (auto iv : scImageViews) vkDestroyImageView(device, iv, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyImageView(device, depthImageView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthMemory, nullptr);

    createSwapchain();
    createImageViews();
    createDepthBuffer();
    createFramebuffers();
}

// ============================================================================
// Cleanup
// ============================================================================
void PBRApp::cleanup() {
    vkDeviceWaitIdle(device);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(device, semImgAvail[i], nullptr);
        vkDestroySemaphore(device, semRendDone[i], nullptr);
        vkDestroyFence(device, fences[i], nullptr);
        vkDestroyBuffer(device, uboMVPBuf[i], nullptr);
        vkDestroyBuffer(device, uboMatBuf[i], nullptr);
        vkFreeMemory(device, uboMVPMem[i], nullptr);
        vkFreeMemory(device, uboMatMem[i], nullptr);
    }
    vkDestroyDescriptorPool(device, descPool, nullptr);
    vkDestroyDescriptorSetLayout(device, dslMVP, nullptr);
    vkDestroyDescriptorSetLayout(device, dslMat, nullptr);
    vkDestroyBuffer(device, vbo, nullptr);
    vkDestroyBuffer(device, ibo, nullptr);
    vkFreeMemory(device, vboMem, nullptr);
    vkFreeMemory(device, iboMem, nullptr);
    vkDestroyCommandPool(device, cmdPool, nullptr);
    for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    vkDestroyImageView(device, depthImageView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthMemory, nullptr);
    for (auto iv : scImageViews) vkDestroyImageView(device, iv, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
}

// ============================================================================
// run()
// ============================================================================
void PBRApp::run() {
    std::cerr << "[DEBUG] run() - before initWindow\n" << std::flush;
    initWindow();
    std::cerr << "[DEBUG] run() - before initVulkan\n" << std::flush;
    initVulkan();
    std::cerr << "[DEBUG] run() - before mainLoop\n" << std::flush;
    mainLoop();
    std::cerr << "[DEBUG] run() - before cleanup\n" << std::flush;
    cleanup();
}
