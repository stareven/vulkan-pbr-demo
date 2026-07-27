#pragma once

#include <filesystem>
#include <string>

#include "math_utils.h"
#include "types.h"
#include "camera.h"

#include "window.h"
#include "vulkan_context.h"
#include "swapchain_manager.h"
#include "render_pipeline.h"
#include "mesh_manager.h"
#include "descriptor_manager.h"
#include "command_manager.h"
#include "sync_manager.h"
#include "shadow_system.h"
#include "material_system.h"

// ============================================================================
// PBR 主应用
//
// PBRApp 只负责编排（init / main loop / per-frame orchestration / cleanup），
// 具体的 Vulkan 资源由各个 Manager 类管理；相机状态由 Camera 类管理。
// ============================================================================
class PBRApp {
public:
    PBRApp(int argc, char* argv[]);
    void run();

private:
    std::filesystem::path exeDir;
    std::string shaderDir;  // 项目根目录(用于查找 shaders)

    // 管理器实例
    Window window{WIDTH, HEIGHT, TITLE};
    VulkanContext ctx;
    SwapchainManager swapchain;
    RenderPipeline renderPipeline;
    MeshManager meshManager;
    DescriptorManager descManager;
    CommandManager cmdManager;
    SyncManager syncManager;
    ShadowSystem shadowSystem;
    MaterialSystem materialSystem;

    // 相机 + 鼠标状态（相机逻辑在 Camera 类；鼠标状态是输入层的细节）
    Camera camera;
    bool leftDown = false;
    double lastMX = 0, lastMY = 0;

    // ------------------------------------------------------------------
    // 初始化
    // ------------------------------------------------------------------
    void initWindow();
    void initVulkan();

    // ------------------------------------------------------------------
    // 运行时
    // ------------------------------------------------------------------
    void mainLoop();
    void handleInput(float dt);
    void drawFrame();
    void recreateSwapchain();

    // ------------------------------------------------------------------
    // 清理
    // ------------------------------------------------------------------
    void cleanup();
};
