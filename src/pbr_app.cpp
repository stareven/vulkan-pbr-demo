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
    // set 0 = MVP (vertex shader)
    VkDescriptorSetLayoutBinding b0{};
    b0.binding = 0;
    b0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b0.descriptorCount = 1;
    b0.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1;
    li.pBindings = &b0;
    if (vkCreateDescriptorSetLayout(device, &li, nullptr, &dslMVP) != VK_SUCCESS)
        throw std::runtime_error("dsl mvp failed");

    // set 1 = Material (fragment shader)
    VkDescriptorSetLayoutBinding b1{};
    b1.binding = 0;
    b1.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b1.descriptorCount = 1;
    b1.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    li.pBindings = &b1;
    if (vkCreateDescriptorSetLayout(device, &li, nullptr, &dslMat) != VK_SUCCESS)
        throw std::runtime_error("dsl mat failed");
}

// ============================================================================
// Graphics pipeline
// ============================================================================
void PBRApp::createGraphicsPipeline() {
    auto vs = readFile(exeDir.parent_path().string() + "/shaders/shader.vert.spv");
    auto fs = readFile(exeDir.parent_path().string() + "/shaders/shader.frag.spv");
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

    // Vertex input
    VkVertexInputBindingDescription bib = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription aib[3] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
    };
    VkPipelineVertexInputStateCreateInfo vici{};
    vici.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vici.vertexBindingDescriptionCount = 1;
    vici.pVertexBindingDescriptions = &bib;
    vici.vertexAttributeDescriptionCount = 3;
    vici.pVertexAttributeDescriptions = aib;

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
    dsci.depthTestEnable = VK_TRUE;
    dsci.depthWriteEnable = VK_TRUE;
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

    VkDescriptorSetLayout layouts[] = {dslMVP, dslMat, dslShadowSampler};
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 3;
    pli.pSetLayouts = layouts;
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
    auto verts = generateSphere(32, 64);
    auto idxs = generateSphereIndices(32, 64);
    indexCount = (uint32_t)idxs.size();
    std::cout << "[MESH] vertices=" << verts.size() << " indices=" << idxs.size() << "\n" << std::flush;

    VkDeviceSize vboSz = verts.size() * sizeof(Vertex);
    VkDeviceSize iboSz = idxs.size() * sizeof(uint32_t);

    // 顶点 staging buffer
    VkBuffer stgV; VkDeviceMemory stgVM;
    createBuffer(device, pd, vboSz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, stgV, stgVM);
    void* mappedV;
    vkMapMemory(device, stgVM, 0, vboSz, 0, &mappedV);
    std::memcpy(mappedV, verts.data(), vboSz);
    vkUnmapMemory(device, stgVM);

    // 索引 staging buffer
    VkBuffer stgI; VkDeviceMemory stgIM;
    createBuffer(device, pd, iboSz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, stgI, stgIM);
    void* mappedI;
    vkMapMemory(device, stgIM, 0, iboSz, 0, &mappedI);
    std::memcpy(mappedI, idxs.data(), iboSz);
    vkUnmapMemory(device, stgIM);

    // Device-local buffers
    createBuffer(device, pd, vboSz,
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vbo, vboMem);
    createBuffer(device, pd, iboSz,
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ibo, iboMem);

    copyBuffer(device, gfxQueue, cmdPool, stgV, vbo, vboSz);
    copyBuffer(device, gfxQueue, cmdPool, stgI, ibo, iboSz);

    vkDestroyBuffer(device, stgV, nullptr);
    vkFreeMemory(device, stgVM, nullptr);
    vkDestroyBuffer(device, stgI, nullptr);
    vkFreeMemory(device, stgIM, nullptr);

    // 地面平面（用于接收阴影）
    auto planeVerts = generatePlane(10.0f, -1.5f);
    auto planeIdxs = generatePlaneIndices();
    planeIndexCount = (uint32_t)planeIdxs.size();

    VkDeviceSize pvboSz = planeVerts.size() * sizeof(Vertex);
    VkDeviceSize piboSz = planeIdxs.size() * sizeof(uint32_t);

    VkBuffer stgPV; VkDeviceMemory stgPVM;
    createBuffer(device, pd, pvboSz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, stgPV, stgPVM);
    void* mPV; vkMapMemory(device, stgPVM, 0, pvboSz, 0, &mPV);
    std::memcpy(mPV, planeVerts.data(), pvboSz); vkUnmapMemory(device, stgPVM);

    VkBuffer stgPI; VkDeviceMemory stgPIM;
    createBuffer(device, pd, piboSz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, stgPI, stgPIM);
    void* mPI; vkMapMemory(device, stgPIM, 0, piboSz, 0, &mPI);
    std::memcpy(mPI, planeIdxs.data(), piboSz); vkUnmapMemory(device, stgPIM);

    createBuffer(device, pd, pvboSz,
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, planeVbo, planeVboMem);
    createBuffer(device, pd, piboSz,
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, planeIbo, planeIboMem);

    copyBuffer(device, gfxQueue, cmdPool, stgPV, planeVbo, pvboSz);
    copyBuffer(device, gfxQueue, cmdPool, stgPI, planeIbo, piboSz);

    vkDestroyBuffer(device, stgPV, nullptr); vkFreeMemory(device, stgPVM, nullptr);
    vkDestroyBuffer(device, stgPI, nullptr); vkFreeMemory(device, stgPIM, nullptr);
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
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)(MAX_FRAMES_IN_FLIGHT * 3)},  // MVP + Material + Shadow
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)imageCount}  // Shadow map
    };
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = MAX_FRAMES_IN_FLIGHT * 3 + (uint32_t)imageCount;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device, &dpi, nullptr, &descPool) != VK_SUCCESS)
        throw std::runtime_error("descriptor pool failed");
}

void PBRApp::createDescriptorSets() {
    descSetsMVP.resize(MAX_FRAMES_IN_FLIGHT);
    descSetsMat.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = descPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &dslMVP;
        if (vkAllocateDescriptorSets(device, &ai, &descSetsMVP[i]) != VK_SUCCESS)
            throw std::runtime_error("alloc mvp desc set failed");

        VkDescriptorBufferInfo bi{};
        bi.buffer = uboMVPBuf[i];
        bi.range = sizeof(UBO_MVP);
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = descSetsMVP[i];
        w.dstBinding = 0;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.descriptorCount = 1;
        w.pBufferInfo = &bi;
        vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);

        ai.pSetLayouts = &dslMat;
        if (vkAllocateDescriptorSets(device, &ai, &descSetsMat[i]) != VK_SUCCESS)
            throw std::runtime_error("alloc mat desc set failed");

        bi.buffer = uboMatBuf[i];
        bi.range = sizeof(UBO_Material);
        w.dstSet = descSetsMat[i];
        w.pBufferInfo = &bi;
        vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    }
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

    // 创建 Shadow 专用 command buffer
    ai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &ai, &shadowCmdBuffer) != VK_SUCCESS)
        throw std::runtime_error("shadow command buffer alloc failed");
}

void PBRApp::recordCommandBuffer(uint32_t imgIdx) {
    VkCommandBuffer cmd = cmdBuffers[frameIdx];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    VkClearValue clears[2];
    clears[0].color = {{0.02f, 0.02f, 0.05f, 1.0f}};
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

    // 绑定 descriptor sets (set 0 = MVP, set 1 = Material, set 2 = Shadow Sampler)
    uint32_t shadowSamplerIdx = imgIdx % imageCount;
    VkDescriptorSet descSets[] = {descSetsMVP[frameIdx], descSetsMat[frameIdx], descSetsShadowSampler[shadowSamplerIdx]};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            0, 3, descSets, 0, nullptr);

    VkViewport vp{0, 0, (float)scExtent.width, (float)scExtent.height, 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, scExtent};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // 画球体
    VkBuffer vertexBuffers[] = {vbo};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, ibo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);

    // 画地面（接收阴影，复用同一材质）
    VkBuffer planeBuffers[] = {planeVbo};
    vkCmdBindVertexBuffers(cmd, 0, 1, planeBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, planeIbo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, planeIndexCount, 1, 0, 0, 0);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

// ============================================================================
// Sync
// ============================================================================
void PBRApp::createSyncObjects() {
    // 为每个 swapchain 图像创建 semaphore（避免重用冲突）
    imageCount = (uint32_t)scImages.size();
    semImgAvail.resize(imageCount);
    semRendDone.resize(imageCount);
    // fence 仍然按 MAX_FRAMES_IN_FLIGHT 创建（用于帧 pacing）
    fences.resize(MAX_FRAMES_IN_FLIGHT);
    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < imageCount; ++i) {
        if (vkCreateSemaphore(device, &sci, nullptr, &semImgAvail[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &sci, nullptr, &semRendDone[i]) != VK_SUCCESS)
            throw std::runtime_error("sync object creation failed");
    }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateFence(device, &fci, nullptr, &fences[i]) != VK_SUCCESS)
            throw std::runtime_error("fence creation failed");
    }
}

// ============================================================================
// Shadow Map
// ============================================================================
void PBRApp::createShadowMap() {
    // 创建深度图像
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = SHADOW_MAP_SIZE;
    imageInfo.extent.height = SHADOW_MAP_SIZE;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_D32_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &shadowMapImage) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow map image!");

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, shadowMapImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(pd, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &shadowMapMemory) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate shadow map memory!");

    vkBindImageMemory(device, shadowMapImage, shadowMapMemory, 0);

    // 创建 Image View
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = shadowMapImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &shadowMapImageView) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow map image view!");
}

void PBRApp::createShadowRenderPass() {
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // 必须保留供主 pass 采样
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &shadowMapRenderPass) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow render pass!");
}

void PBRApp::createShadowPipeline() {
    // 加载阴影着色器
    auto vertShaderCode = readFile(exeDir.parent_path().string() + "/shaders/shadow.vert.spv");
    VkShaderModule vertShaderModule = makeShaderModule(device, vertShaderCode);

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertShaderModule;
    vertStageInfo.pName = "main";

    // 顶点输入（仅位置）
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescription{};
    attributeDescription.binding = 0;
    attributeDescription.location = 0;
    attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription.offset = offsetof(Vertex, pos);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;  // 剔除正面，减少阴影 acne
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &dslShadow;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &shadowMapPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow pipeline layout!");

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 1;
    pipelineInfo.pStages = &vertStageInfo;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = shadowMapPipelineLayout;
    pipelineInfo.renderPass = shadowMapRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowMapPipeline) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow pipeline!");

    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void PBRApp::createShadowFramebuffer() {
    VkImageView attachments[] = { shadowMapImageView };

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = shadowMapRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = SHADOW_MAP_SIZE;
    framebufferInfo.height = SHADOW_MAP_SIZE;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &shadowMapFramebuffer) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow framebuffer!");
}

void PBRApp::createShadowDescriptorLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &dslShadow) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow descriptor set layout!");
}

void PBRApp::createShadowDescriptorSets() {
    uboShadowBuf.resize(MAX_FRAMES_IN_FLIGHT);
    uboShadowMem.resize(MAX_FRAMES_IN_FLIGHT);
    descSetsShadow.resize(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &dslShadow;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createBuffer(device, pd, sizeof(UBO_Shadow), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, uboShadowBuf[i], uboShadowMem[i]);

        vkAllocateDescriptorSets(device, &allocInfo, &descSetsShadow[i]);

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uboShadowBuf[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UBO_Shadow);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descSetsShadow[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }
}

void PBRApp::recordShadowCommandBuffer() {
    VkCommandBuffer cmd = shadowCmdBuffer;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = shadowMapRenderPass;
    renderPassInfo.framebuffer = shadowMapFramebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};

    VkClearValue clearValue{};
    clearValue.depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowMapPipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)SHADOW_MAP_SIZE;
    viewport.height = (float)SHADOW_MAP_SIZE;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowMapPipelineLayout,
                            0, 1, &descSetsShadow[frameIdx], 0, nullptr);

    VkBuffer vertexBuffers[] = { vbo };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, ibo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

void PBRApp::createShadowSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    // MoltenVK 不支持比较采样器，使用普通采样
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &shadowSampler) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow sampler!");
}

void PBRApp::createShadowSamplerDescriptorLayout() {
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &dslShadowSampler) != VK_SUCCESS)
        throw std::runtime_error("failed to create shadow sampler descriptor set layout!");
}

void PBRApp::createShadowSamplerDescriptorSets() {
    descSetsShadowSampler.resize(imageCount);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &dslShadowSampler;

    for (uint32_t i = 0; i < imageCount; i++) {
        vkAllocateDescriptorSets(device, &allocInfo, &descSetsShadowSampler[i]);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfo.imageView = shadowMapImageView;
        imageInfo.sampler = shadowSampler;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descSetsShadowSampler[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
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

    // 光源空间矩阵（主光源在 (10,10,10) 看向原点）
    Vec3 lightPos{10, 10, 10};
    lightView = Mat4::lookAt(lightPos, Vec3(0, 0, 0), Vec3(0, 1, 0));
    lightProj = Mat4::ortho(-8, 8, -8, 8, 0.1f, 50.0f);
    Mat4 lightSpaceMatrix = lightProj * lightView;

    // C++ 是 row-major，GLSL 是 column-major，需要转置
    UBO_MVP mvp{model.transposed(), view.transposed(), proj.transposed(),
                lightSpaceMatrix.transposed(), camPos, 0.0f};
    void* p;
    vkMapMemory(device, uboMVPMem[frameIdx], 0, sizeof(mvp), 0, &p);
    std::memcpy(p, &mvp, sizeof(mvp));
    vkUnmapMemory(device, uboMVPMem[frameIdx]);

    // 更新 Shadow UBO（光源空间矩阵，用于 shadow pass 顶点变换）
    UBO_Shadow shadowUBO{lightSpaceMatrix.transposed(), lightPos, 0.0f};
    vkMapMemory(device, uboShadowMem[frameIdx], 0, sizeof(shadowUBO), 0, &p);
    std::memcpy(p, &shadowUBO, sizeof(shadowUBO));
    vkUnmapMemory(device, uboShadowMem[frameIdx]);

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
    mat.lights[0] = {{10, 10, 10}, 0.0f, {300, 300, 300}, 1.0f};
    mat.lights[1] = {{-10, 10, 10}, 0.0f, {300, 100, 100}, 1.0f};
    mat.lights[2] = {{10, -10, -10}, 0.0f, {100, 300, 100}, 1.0f};
    mat.lights[3] = {{-10, -10, -10}, 0.0f, {100, 100, 300}, 1.0f};

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
    while (!glfwWindowShouldClose(window)) {
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

        // 材质切换 (M)
        static bool mLast = false;
        bool mNow = glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS;
        if (mNow && !mLast) {
            matPreset = (matPreset + 1) % 7;
            std::cout << "Material preset: " << matPreset << "\n";
        }
        mLast = mNow;

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

    // 先渲染 Shadow Map（独立提交并等待完成）
    recordShadowCommandBuffer();
    VkSubmitInfo shadowSubmitInfo{};
    shadowSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    shadowSubmitInfo.commandBufferCount = 1;
    shadowSubmitInfo.pCommandBuffers = &shadowCmdBuffer;
    vkQueueSubmit(gfxQueue, 1, &shadowSubmitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(gfxQueue);

    // 渲染主场景
    recordCommandBuffer(imgIdx);

    vkResetFences(device, 1, &fences[frameIdx]);

    // semaphore 用 imgIdx 索引，避免 presentation 重用冲突
    uint32_t semIdx = imgIdx % imageCount;
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore wait[] = {semImgAvail[frameIdx]};
    VkSemaphore sig[] = {semRendDone[semIdx]};

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

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = sig;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
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
    for (uint32_t i = 0; i < imageCount; ++i) {
        vkDestroySemaphore(device, semImgAvail[i], nullptr);
        vkDestroySemaphore(device, semRendDone[i], nullptr);
    }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
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
    vkDestroyBuffer(device, planeVbo, nullptr);
    vkDestroyBuffer(device, planeIbo, nullptr);
    vkFreeMemory(device, planeVboMem, nullptr);
    vkFreeMemory(device, planeIboMem, nullptr);
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
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
}
