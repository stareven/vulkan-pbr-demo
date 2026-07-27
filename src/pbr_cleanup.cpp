#include "pbr_app.h"
#include "vulkan_utils.h"

void PBRApp::cleanup() {
    vkDeviceWaitIdle(ctx.device);

    // 按 reverse-init 顺序清理各 Manager
    cmdManager.cleanup(ctx.device);
    shadowSystem.cleanup(ctx.device);
    descManager.cleanup(ctx.device);
    syncManager.cleanup(ctx.device);
    renderPipeline.cleanup(ctx.device);
    meshManager.cleanup(ctx.device);
    swapchain.cleanup(ctx.device);

    // 核心 Vulkan 上下文（device + surface + instance）
    ctx.cleanup();

    // 窗口
    window.destroy();
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
