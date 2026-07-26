#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <filesystem>
#include <vector>

#include "math_utils.h"
#include "types.h"

// ============================================================================
// PBR 主应用
// ============================================================================
class PBRApp {
public:
    PBRApp(int argc, char* argv[]);
    void run();

private:
    std::filesystem::path exeDir;
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
    VkCommandBuffer shadowCmdBuffer;  // Shadow pass 专用

    // Sync
    std::vector<VkSemaphore> semImgAvail, semRendDone;
    std::vector<VkFence> fences;
    uint32_t frameIdx = 0;
    uint32_t imageCount = 0;  // swapchain 图像数量，用于 semaphore 索引

    // Mesh
    VkBuffer vbo, ibo;
    VkDeviceMemory vboMem, iboMem;
    uint32_t indexCount = 0;

    // Ground plane mesh
    VkBuffer planeVbo, planeIbo;
    VkDeviceMemory planeVboMem, planeIboMem;
    uint32_t planeIndexCount = 0;

    // Uniform buffers
    std::vector<VkBuffer> uboMVPBuf;
    std::vector<VkDeviceMemory> uboMVPMem;
    std::vector<VkBuffer> uboMatBuf;
    std::vector<VkDeviceMemory> uboMatMem;

    // Descriptor
    VkDescriptorSetLayout dslMVP, dslMat;
    VkDescriptorPool descPool;
    std::vector<VkDescriptorSet> descSetsMVP, descSetsMat;

    // Shadow map resources
    static constexpr uint32_t SHADOW_MAP_SIZE = 2048;
    VkImage shadowMapImage;
    VkDeviceMemory shadowMapMemory;
    VkImageView shadowMapImageView;
    VkFramebuffer shadowMapFramebuffer;
    VkRenderPass shadowMapRenderPass;
    VkPipelineLayout shadowMapPipelineLayout;
    VkPipeline shadowMapPipeline;
    VkDescriptorSetLayout dslShadow;
    std::vector<VkBuffer> uboShadowBuf;
    std::vector<VkDeviceMemory> uboShadowMem;
    std::vector<VkDescriptorSet> descSetsShadow;

    // Light space matrices
    Mat4 lightView, lightProj;

    // Shadow sampler
    VkSampler shadowSampler;
    VkDescriptorSetLayout dslShadowSampler;
    std::vector<VkDescriptorSet> descSetsShadowSampler;

    // Camera
    Vec3 camPos{0, 2, 5};
    float camYaw = M_PI, camPitch = -0.5f;  // 稍微向下看，展示地面阴影
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

    // Shadow map
    void createShadowMap();
    void createShadowRenderPass();
    void createShadowPipeline();
    void createShadowFramebuffer();
    void createShadowDescriptorLayout();
    void createShadowDescriptorSets();
    void createShadowSampler();
    void createShadowSamplerDescriptorLayout();
    void createShadowSamplerDescriptorSets();
    void recordShadowCommandBuffer();

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
