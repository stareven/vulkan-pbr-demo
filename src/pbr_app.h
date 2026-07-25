#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>

#include "math_utils.h"
#include "types.h"

// ============================================================================
// PBR 主应用
// ============================================================================
class PBRApp {
public:
    void run();

private:
    GLFWwindow* window = nullptr;

    // Vulkan 核心
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue gfxQueue, presQueue;

    // Swapchain
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat scFormat;
    VkExtent2D scExtent;
    std::vector<VkImage> scImages;
    std::vector<VkImageView> scImageViews;

    // Depth buffer
    VkImage depthImage;
    VkDeviceMemory depthMemory;
    VkImageView depthImageView;

    // Render Pass
    VkRenderPass renderPass;

    // Pipeline
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;

    // Framebuffers
    std::vector<VkFramebuffer> framebuffers;

    // Command pool & buffers
    VkCommandPool cmdPool;
    std::vector<VkCommandBuffer> cmdBuffers;

    // Sync
    std::vector<VkSemaphore> semImgAvail, semRendDone;
    std::vector<VkFence> fences;
    uint32_t frameIdx = 0;

    // Mesh
    VkBuffer vbo, ibo;
    VkDeviceMemory vboMem, iboMem;
    uint32_t indexCount = 0;

    // Uniform buffers
    std::vector<VkBuffer> uboMVPBuf;
    std::vector<VkDeviceMemory> uboMVPMem;
    std::vector<VkBuffer> uboMatBuf;
    std::vector<VkDeviceMemory> uboMatMem;

    // Descriptor
    VkDescriptorSetLayout dslMVP, dslMat;
    VkDescriptorPool descPool;
    std::vector<VkDescriptorSet> descSetsMVP, descSetsMat;

    // Camera
    Vec3 camPos{0, 0, 4};
    float camYaw = M_PI, camPitch = 0;  // 初始看向 -Z 方向（球体在原点）
    bool leftDown = false;
    double lastMX = 0, lastMY = 0;

    // Material preset toggle
    int matPreset = 0;
    bool glassEnabled = false;
    bool emissiveEnabled = false;

    bool fbResized = false;

    // ------------------------------------------------------------------
    // 初始化
    // ------------------------------------------------------------------
    void initWindow();
    void initVulkan();

    // ------------------------------------------------------------------
    // Vulkan 资源创建
    // ------------------------------------------------------------------
    void createSwapchain();
    void createImageViews();
    void createDepthBuffer();
    void createRenderPass();
    void createDescriptorLayouts();
    void createGraphicsPipeline();
    void createFramebuffers();
    void createCommandPool();
    void createMesh();
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void createCommandBuffers();
    void createSyncObjects();

    // ------------------------------------------------------------------
    // 运行时
    // ------------------------------------------------------------------
    void updateUBOs();
    void recordCommandBuffer(uint32_t imgIdx);
    void drawFrame();
    void recreateSwapchain();
    void mainLoop();

    // ------------------------------------------------------------------
    // 清理
    // ------------------------------------------------------------------
    void cleanup();
};
