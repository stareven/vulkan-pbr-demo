#include "pbr_app.h"
#include "vulkan_utils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace config;

// ============================================================================
// 生命周期
// ============================================================================
PBRApp::PBRApp(int argc, char* argv[]) {
    try {
        exeDir = std::filesystem::canonical(argv[0]).parent_path();
    } catch (...) {
        exeDir = std::filesystem::current_path();
    }
    shaderDir = exeDir.parent_path().string();
}

void PBRApp::run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
}

// ============================================================================
// Window
// ============================================================================
void PBRApp::initWindow() {
    window.create();

    // PBRApp 接管所有回调（覆盖 Window::create() 中的默认注册）。
    // 因此需要重新设置 user pointer 到 PBRApp*，并注册 framebuffer + 鼠标回调。
    GLFWwindow* h = window.getHandle();
    glfwSetWindowUserPointer(h, this);

    glfwSetFramebufferSizeCallback(h, [](GLFWwindow* w, int, int) {
        auto s = reinterpret_cast<PBRApp*>(glfwGetWindowUserPointer(w));
        s->window.notifyFramebufferResized();
    });

    glfwSetCursorPosCallback(h, [](GLFWwindow* w, double x, double y) {
        auto s = reinterpret_cast<PBRApp*>(glfwGetWindowUserPointer(w));
        if (s->leftDown) {
            double dx = x - s->lastMX, dy = y - s->lastMY;
            s->camera.handleMouseDrag(dx, dy);
        }
        s->lastMX = x;
        s->lastMY = y;
    });

    glfwSetMouseButtonCallback(h, [](GLFWwindow* w, int btn, int act, int) {
        auto s = reinterpret_cast<PBRApp*>(glfwGetWindowUserPointer(w));
        if (btn == GLFW_MOUSE_BUTTON_LEFT) {
            s->leftDown = (act == GLFW_PRESS);
            glfwGetCursorPos(w, &s->lastMX, &s->lastMY);
        }
    });
}

// ============================================================================
// Vulkan Init — 编排各 Manager 的初始化顺序
// ============================================================================
void PBRApp::initVulkan() {
    // 1. 核心 Vulkan 上下文
    ctx.initialize(window.getHandle());

    // 初始化各 Manager（缓存 device/queue 等句柄，后续方法不再需要传）
    renderPipeline.init(ctx.getDevice());
    descManager.init(ctx.getDevice());
    cmdManager.init(ctx.getDevice());
    syncManager.init(ctx.getDevice());
    meshManager.init(ctx.getDevice(), ctx.getPhysicalDevice());
    swapchain.init(ctx.getDevice(), ctx.getPhysicalDevice());

    // 2. Swapchain（包含 image views + depth buffer）
    swapchain.create(ctx.getSurface(), window.getHandle());

    // 3. 命令池（mesh upload 需要它）
    cmdManager.createPool(ctx.getGraphicsFamily());

    // 4. Mesh（顶点/索引缓冲）
    meshManager.createMeshes(ctx.getGraphicsQueue(), cmdManager.getPool());

    // 5. 主 pass 的 Uniform 缓冲（MVP + Material）
    meshManager.createUniformBuffers(MAX_FRAMES_IN_FLIGHT);

    // 6. Descriptor 布局（MVP + Material）
    descManager.createLayouts();

    // 7. 阴影系统（一次性完整初始化，包括 pipeline 和 descriptor sets）
    shadowSystem.initialize(ctx.getDevice(), ctx.getPhysicalDevice(), shaderDir, swapchain.getImageCount());

    // 8. 主渲染通道 + 管线布局 + 图形管线 + 帧缓冲
    renderPipeline.createRenderPass(swapchain.getFormat());
    renderPipeline.createPipelineLayout(
        descManager.getMVPLayout(),
        descManager.getMaterialLayout(),
        shadowSystem.getSamplerLayout());
    renderPipeline.createGraphicsPipeline(swapchain.getExtent(), shaderDir);
    renderPipeline.createFramebuffers(swapchain.getExtent(),
        swapchain.getImageViews(), swapchain.getDepthImageView());

    // 9. Sync objects
    syncManager.create(swapchain.getImageCount());

    // 10. Descriptor pool + 主 pass 的 descriptor sets
    descManager.createPool(MAX_FRAMES_IN_FLIGHT);
    descManager.allocateSets(MAX_FRAMES_IN_FLIGHT);

    // 绑定每帧的 descriptor set 到对应的 UBO（一次性完成，之后每帧只写 UBO 内存）
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        descManager.updateSets(i,
            meshManager.getMVPBuffer(i),
            meshManager.getMaterialBuffer(i));
    }

    // 11. 命令缓冲
    cmdManager.allocateBuffers(MAX_FRAMES_IN_FLIGHT);
    cmdManager.allocateShadowCommandBuffer();
}

// ============================================================================
// 输入处理
// ============================================================================
void PBRApp::handleInput(float dt) {
    float speed = 5.0f * dt;

    GLFWwindow* h = window.getHandle();
    if (glfwGetKey(h, GLFW_KEY_W) == GLFW_PRESS) camera.moveForward(speed);
    if (glfwGetKey(h, GLFW_KEY_S) == GLFW_PRESS) camera.moveForward(-speed);
    if (glfwGetKey(h, GLFW_KEY_A) == GLFW_PRESS) camera.moveRight(-speed);
    if (glfwGetKey(h, GLFW_KEY_D) == GLFW_PRESS) camera.moveRight(speed);
    if (glfwGetKey(h, GLFW_KEY_Q) == GLFW_PRESS) camera.moveUp(-speed);
    if (glfwGetKey(h, GLFW_KEY_E) == GLFW_PRESS) camera.moveUp(speed);

    // 材质切换 (M)
    static bool mLast = false;
    bool mNow = glfwGetKey(h, GLFW_KEY_M) == GLFW_PRESS;
    if (mNow && !mLast) {
        materialSystem.nextPreset();
        std::cout << "Material preset: " << materialSystem.getPreset() << "\n";
    }
    mLast = mNow;

    // 玻璃 (G)
    static bool gLast = false;
    bool gNow = glfwGetKey(h, GLFW_KEY_G) == GLFW_PRESS;
    if (gNow && !gLast) {
        materialSystem.toggleGlass();
        std::cout << "Glass: " << (materialSystem.isGlassEnabled() ? "ON" : "OFF") << "\n";
    }
    gLast = gNow;

    // 自发光 (F)
    static bool eLast = false;
    bool eNow = glfwGetKey(h, GLFW_KEY_F) == GLFW_PRESS;
    if (eNow && !eLast) {
        materialSystem.toggleEmissive();
        std::cout << "Emissive: " << (materialSystem.isEmissiveEnabled() ? "ON" : "OFF") << "\n";
    }
    eLast = eNow;
}

// ============================================================================
// 主循环
// ============================================================================
void PBRApp::mainLoop() {
    while (!window.shouldClose()) {
        window.pollEvents();
        handleInput(0.016f);
        drawFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    vkDeviceWaitIdle(ctx.getDevice());
}

// ============================================================================
// 单帧渲染
// ============================================================================
void PBRApp::drawFrame() {
    syncManager.waitForFence();

    uint32_t imgIdx;
    VkResult r = vkAcquireNextImageKHR(ctx.getDevice(), swapchain.getSwapchain(), UINT64_MAX,
        syncManager.getImageAvailableSemaphore(), VK_NULL_HANDLE, &imgIdx);

    if (r == VK_ERROR_OUT_OF_DATE_KHR || window.isFramebufferResized()) {
        window.resetFramebufferResized();
        recreateSwapchain();
        return;
    }

    syncManager.markImageInFlight(imgIdx);
    uint32_t frameIdx = syncManager.getCurrentFrame();

    // 计算相机 / 投影矩阵
    Mat4 view = camera.getViewMatrix();
    float aspect = (float)swapchain.getExtent().width / (float)swapchain.getExtent().height;
    Mat4 proj = Mat4::perspective(45.0f * static_cast<float>(M_PI) / 180.0f, aspect, 0.1f, 100.0f);
    Mat4 model = Mat4::identity();

    // Shadow UBO 必须先更新（内部会计算 lightView / lightProj）
    // 注意：shadow UBO 数量 = MAX_FRAMES_IN_FLIGHT，用 frameIdx 索引
    shadowSystem.updateShadowUBO(frameIdx, model);

    // 光源空间矩阵（用于主 pass 的 MVP UBO，以便 shader 做阴影坐标变换）
    Mat4 lightSpaceMatrix = shadowSystem.getLightProj() * shadowSystem.getLightView();

    // 更新主 pass UBOs
    meshManager.updateUniformBuffers(frameIdx,
        model, view, proj, lightSpaceMatrix, camera.getPosition(),
        materialSystem.getPreset(),
        materialSystem.isGlassEnabled(),
        materialSystem.isEmissiveEnabled());

    // descriptor set 已经在 init 时绑定到每帧 UBO，无需每帧更新

    // 1) Shadow pass — 独立提交并等待完成（主 pass 需要采样 shadow map）
    VkCommandBuffer shadowCmd = cmdManager.getShadowCommandBuffer();
    uint32_t sFrameIdx = syncManager.getCurrentFrame();
    shadowSystem.recordShadowPass(shadowCmd, sFrameIdx,
        meshManager.getSphereVBO(), meshManager.getSphereIBO(), meshManager.getSphereIndexCount());
    VkSubmitInfo shadowSubmit{};
    shadowSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    shadowSubmit.commandBufferCount = 1;
    shadowSubmit.pCommandBuffers = &shadowCmd;
    vkQueueSubmit(ctx.getGraphicsQueue(), 1, &shadowSubmit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.getGraphicsQueue());

    // 2) 主 pass
    VkCommandBuffer mainCmd = cmdManager.getBuffer(frameIdx);
    {
        VkDescriptorSet descSets[] = {
            descManager.getMVPSet(frameIdx),
            descManager.getMaterialSet(frameIdx),
            shadowSystem.getSamplerSet(imgIdx),
        };
        std::vector<VkDescriptorSet> dsVec(descSets, descSets + 3);
        renderPipeline.recordMainPass(mainCmd, imgIdx, swapchain.getExtent(), dsVec,
            meshManager.getSphereVBO(), meshManager.getSphereIBO(), meshManager.getSphereIndexCount(),
            meshManager.getPlaneVBO(), meshManager.getPlaneIBO(), meshManager.getPlaneIndexCount(),
            materialSystem.isEmissiveEnabled(), materialSystem.isGlassEnabled());
    }

    syncManager.resetFence();

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore wait[] = {syncManager.getImageAvailableSemaphore()};
    VkSemaphore sig[]  = {syncManager.getRenderFinishedSemaphore()};

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = wait;
    si.pWaitDstStageMask = waitStages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &mainCmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = sig;

    if (vkQueueSubmit(ctx.getGraphicsQueue(), 1, &si, syncManager.getInFlightFence()) != VK_SUCCESS)
        throw std::runtime_error("queue submit failed");

    VkSwapchainKHR sc = swapchain.getSwapchain();
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = sig;
    pi.swapchainCount = 1;
    pi.pSwapchains = &sc;
    pi.pImageIndices = &imgIdx;
    r = vkQueuePresentKHR(ctx.getPresentQueue(), &pi);

    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || window.isFramebufferResized()) {
        window.resetFramebufferResized();
        recreateSwapchain();
    }

    syncManager.advanceFrame();
}

// ============================================================================
// Swapchain 重建
// ============================================================================
void PBRApp::recreateSwapchain() {
    vkDeviceWaitIdle(ctx.getDevice());

    // 销毁依赖 swapchain extent 的资源
    renderPipeline.cleanup();
    swapchain.recreate(ctx.getSurface(), window.getHandle());

    // 重建依赖 swapchain 的资源
    renderPipeline.createRenderPass(swapchain.getFormat());
    renderPipeline.createPipelineLayout(
        descManager.getMVPLayout(),
        descManager.getMaterialLayout(),
        shadowSystem.getSamplerLayout());
    renderPipeline.createGraphicsPipeline(swapchain.getExtent(), shaderDir);
    renderPipeline.createFramebuffers(swapchain.getExtent(),
        swapchain.getImageViews(), swapchain.getDepthImageView());
}

// ============================================================================
// 清理
// ============================================================================
void PBRApp::cleanup() {
    vkDeviceWaitIdle(ctx.getDevice());

    // 按 reverse-init 顺序清理各 Manager
    cmdManager.cleanup();
    shadowSystem.cleanup();
    descManager.cleanup();
    syncManager.cleanup();
    renderPipeline.cleanup();
    meshManager.cleanup();
    swapchain.cleanup();

    // 核心 Vulkan 上下文（device + surface + instance）
    ctx.cleanup();

    // 窗口
    window.destroy();
    glfwTerminate();
}
