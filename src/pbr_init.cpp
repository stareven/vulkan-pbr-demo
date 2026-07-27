#include "pbr_app.h"
#include "vulkan_utils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

PBRApp::PBRApp(int argc, char* argv[]) {
    try {
        exeDir = std::filesystem::canonical(argv[0]).parent_path();
    } catch (...) {
        exeDir = std::filesystem::current_path();
    }
    shaderDir = exeDir.parent_path().string();
}

// ============================================================================
// Window
// ============================================================================
void PBRApp::initWindow() {
    window.create();

    // 重新注册回调，使用 PBRApp 自己的相机状态（覆盖 Window 内部的默认回调）
    GLFWwindow* h = window.getHandle();
    glfwSetWindowUserPointer(h, this);

    glfwSetCursorPosCallback(h, [](GLFWwindow* w, double x, double y) {
        auto s = reinterpret_cast<PBRApp*>(glfwGetWindowUserPointer(w));
        if (s->leftDown) {
            double dx = x - s->lastMX, dy = y - s->lastMY;
            s->camYaw   += dx * 0.005f;
            s->camPitch -= dy * 0.005f;
            s->camPitch = std::clamp(s->camPitch, -1.5f, 1.5f);
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
    // framebuffer resize 直接用 Window 内部的状态即可（通过 window.isFramebufferResized()）
}

// ============================================================================
// Vulkan Init — 编排各 Manager 的初始化顺序
// ============================================================================
void PBRApp::initVulkan() {
    // 1. 核心 Vulkan 上下文
    ctx.initialize(window.getHandle());

    // 2. Swapchain（包含 image views + depth buffer）
    swapchain.create(ctx.device, ctx.physicalDevice, ctx.surface, window.getHandle());

    // 3. 命令池（mesh upload 需要它）
    cmdManager.createPool(ctx.device, ctx.graphicsFamily);

    // 4. Mesh（顶点/索引缓冲）
    meshManager.createMeshes(ctx.device, ctx.physicalDevice,
                             ctx.graphicsQueue, cmdManager.getPool());

    // 5. 主 pass 的 Uniform 缓冲（MVP + Material）
    meshManager.createUniformBuffers(ctx.device, ctx.physicalDevice, MAX_FRAMES_IN_FLIGHT);

    // 6. Descriptor 布局（MVP + Material）
    descManager.createLayouts(ctx.device);

    // 7. 阴影系统（shadow map + render pass + sampler + descriptor layout）
    //    注意：initialize() 内部会创建 shadow pipeline，但 shadow pipeline 需要
    //    render pipeline 的 descriptor set layout 在之前创建——所以这里先做 shadow 的
    //    资源准备，pipeline 稍后通过 createShadowPipeline() 单独创建。
    shadowSystem.createShadowMap(ctx.device, ctx.physicalDevice);
    shadowSystem.createShadowRenderPass(ctx.device);
    shadowSystem.createShadowDescriptorLayout(ctx.device);
    shadowSystem.createShadowSampler(ctx.device);
    shadowSystem.createShadowSamplerDescriptorLayout(ctx.device);

    // 8. 主渲染通道 + 管线布局 + 图形管线 + 帧缓冲
    renderPipeline.createRenderPass(ctx.device, swapchain.getFormat());
    renderPipeline.createPipelineLayout(ctx.device,
        descManager.getMVPLayout(),
        descManager.getMaterialLayout(),
        shadowSystem.getSamplerLayout());   // set 2 = shadow sampler
    renderPipeline.createGraphicsPipeline(ctx.device, swapchain.getExtent(), shaderDir);
    renderPipeline.createFramebuffers(ctx.device, swapchain.getExtent(),
        swapchain.getImageViews(), swapchain.getDepthImageView());

    // 9. Shadow pipeline / framebuffer（依赖前面的 shadow render pass + descriptor layout）
    shadowSystem.createShadowPipeline(ctx.device, shaderDir);
    shadowSystem.createShadowFramebuffer(ctx.device);

    // 10. Sync objects
    syncManager.create(ctx.device, swapchain.getImageCount());

    // 11. Descriptor pool + 主 pass 的 descriptor sets
    descManager.createPool(ctx.device, MAX_FRAMES_IN_FLIGHT);
    descManager.allocateSets(ctx.device, MAX_FRAMES_IN_FLIGHT);

    // 绑定每帧的 descriptor set 到对应的 UBO（一次性完成，之后每帧只写 UBO 内存）
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        descManager.updateSets(ctx.device, i,
            meshManager.getMVPBuffer(i),
            meshManager.getMaterialBuffer(i));
    }

    // 12. Shadow descriptor sets + sampler descriptor sets
    shadowSystem.createShadowDescriptorSets(ctx.device, ctx.physicalDevice, MAX_FRAMES_IN_FLIGHT);
    shadowSystem.createShadowSamplerDescriptorSets(ctx.device, swapchain.getImageCount(),
        shadowSystem.getShadowMapView());

    // 13. 命令缓冲
    cmdManager.allocateBuffers(ctx.device, MAX_FRAMES_IN_FLIGHT);
    cmdManager.allocateShadowCommandBuffer(ctx.device);
}
