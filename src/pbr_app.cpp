#include "pbr_app.h"  // PBR 主应用类声明
#include "vulkan_utils.h"  // Vulkan 辅助函数（缓冲区创建、着色器加载等）

#include <algorithm>   // std::min/max
#include <array>       // std::array
#include <chrono>      // 时间计算（帧率控制）
#include <cmath>       // 数学函数（sin/cos/sqrt 等）
#include <cstring>     // C 字符串操作（memcpy 等）
#include <filesystem>  // 文件系统路径操作
#include <iostream>    // 标准输出（std::cout 用于调试信息）
#include <stdexcept>   // 异常处理（std::runtime_error）
#include <thread>      // 线程休眠（帧率限制）

using namespace config;  // 使用配置常量（WIDTH/HEIGHT/TITLE/MAX_FRAMES_IN_FLIGHT 等）

// ============================================================================
// 生命周期管理
// ============================================================================

// 构造函数：初始化可执行文件路径和着色器目录
// 参数: argc - 命令行参数数量, argv - 命令行参数数组
PBRApp::PBRApp(int argc, char* argv[]) {
    try {
        // canonical(argv[0]) 获取可执行文件的绝对路径，parent_path() 获取所在目录
        exeDir = std::filesystem::canonical(argv[0]).parent_path();
    } catch (...) {
        // 如果获取失败，使用当前工作目录作为备选
        exeDir = std::filesystem::current_path();
    }
    // shaderDir 设置为项目根目录（exeDir 的父目录），用于查找 shaders/*.spv 文件
    shaderDir = exeDir.parent_path().string();
}

// 应用主入口：按顺序执行初始化和主循环
void PBRApp::run() {
    initWindow();    // 第1步：创建 GLFW 窗口并注册回调
    initVulkan();    // 第2步：初始化 Vulkan 环境和所有管理器
    mainLoop();      // 第3步：进入主渲染循环
    cleanup();       // 第4步：清理资源（窗口关闭后）
}

// ============================================================================
// 窗口初始化
// ============================================================================

// 创建 GLFW 窗口并注册事件回调函数
void PBRApp::initWindow() {
    window.create();  // 创建 GLFW 窗口（使用 WIDTH/HEIGHT/TITLE 配置）

    // PBRApp 接管所有回调（覆盖 Window::create() 中的默认注册）。
    // 因此需要重新设置 user pointer 到 PBRApp*，并注册 framebuffer + 鼠标回调。
    GLFWwindow* h = window.getHandle();  // 获取原生 GLFW 窗口句柄
    glfwSetWindowUserPointer(h, this);   // 将 PBRApp* 存储为窗口的用户指针（回调中通过此指针访问 PBRApp）

    // 帧缓冲大小回调：窗口大小改变时设置 framebufferResized 标志
    // 该标志在 drawFrame() 中检查，触发 recreateSwapchain()
    glfwSetFramebufferSizeCallback(h, [](GLFWwindow* w, int, int) {
        auto s = reinterpret_cast<PBRApp*>(glfwGetWindowUserPointer(w));  // 从窗口指针恢复 PBRApp*
        s->window.notifyFramebufferResized();  // 设置 framebufferResized = true
    });

    // 光标位置回调：鼠标移动时更新相机旋转角度（仅当左键按下时）
    glfwSetCursorPosCallback(h, [](GLFWwindow* w, double x, double y) {
        auto s = reinterpret_cast<PBRApp*>(glfwGetWindowUserPointer(w));
        if (s->leftDown) {  // 仅当鼠标左键按下时才处理拖拽
            double dx = x - s->lastMX, dy = y - s->lastMY;  // 计算鼠标移动偏移量
            s->camera.handleMouseDrag(dx, dy);  // 更新相机 yaw/pitch 角度
        }
        s->lastMX = x;  // 更新鼠标位置记录
        s->lastMY = y;
    });

    // 鼠标按键回调：记录左键按下/释放状态
    glfwSetMouseButtonCallback(h, [](GLFWwindow* w, int btn, int act, int) {
        auto s = reinterpret_cast<PBRApp*>(glfwGetWindowUserPointer(w));
        if (btn == GLFW_MOUSE_BUTTON_LEFT) {  // 只关心左键
            s->leftDown = (act == GLFW_PRESS);  // 按下=true，释放=false
            glfwGetCursorPos(w, &s->lastMX, &s->lastMY);  // 记录按下时的鼠标位置
        }
    });
}

// ============================================================================
// Vulkan 初始化 — 编排各 Manager 的初始化顺序
// ============================================================================

// 初始化 Vulkan 环境和所有管理器（严格遵循依赖关系）
void PBRApp::initVulkan() {
    // --------------------------------------------------------------------------
    // 第1步：创建 Vulkan 核心上下文
    // --------------------------------------------------------------------------
    ctx.initialize(window.getHandle());  // 创建 Vulkan instance、选择 physical device、创建 logical device

    // 初始化各 Manager（缓存 device/queue 等句柄，后续方法不再需要传）
    renderPipeline.init(ctx.getDevice());          // 缓存 VkDevice
    descManager.init(ctx.getDevice());             // 缓存 VkDevice
    cmdManager.init(ctx.getDevice());              // 缓存 VkDevice
    syncManager.init(ctx.getDevice());             // 缓存 VkDevice
    meshManager.init(ctx.getDevice(), ctx.getPhysicalDevice());  // 缓存 VkDevice 和 VkPhysicalDevice（缓冲区创建需要物理设备属性）
    swapchain.init(ctx.getDevice(), ctx.getPhysicalDevice());    // 缓存 VkDevice 和 VkPhysicalDevice

    // --------------------------------------------------------------------------
    // 第2步：创建交换链
    // --------------------------------------------------------------------------
    // 创建交换链、图像视图、深度缓冲图像
    // 参数: surface - Vulkan 表面（由 ctx 创建），window - GLFW 窗口句柄（用于查询 extent）
    swapchain.create(ctx.getSurface(), window.getHandle());

    // --------------------------------------------------------------------------
    // 第3步：创建命令池
    // --------------------------------------------------------------------------
    // 命令池用于分配命令缓冲，必须在网格数据上传之前创建
    cmdManager.createPool(ctx.getGraphicsFamily());  // 使用图形队列族索引创建命令池

    // --------------------------------------------------------------------------
    // 第4步：创建网格数据
    // --------------------------------------------------------------------------
    // 创建球体和平面的顶点缓冲（VBO）和索引缓冲（IBO）
    // 这些缓冲通过临时命令缓冲从 CPU 可见内存传输到 GPU 本地内存
    meshManager.createMeshes(ctx.getGraphicsQueue(), cmdManager.getPool());

    // --------------------------------------------------------------------------
    // 第5步：创建统一缓冲区（UBO）
    // --------------------------------------------------------------------------
    // 为每帧创建 MVP（模型-视图-投影）和材质参数的 UBO
    // MAX_FRAMES_IN_FLIGHT 定义最大并发帧数（通常为 2-3）
    meshManager.createUniformBuffers(MAX_FRAMES_IN_FLIGHT);

    // --------------------------------------------------------------------------
    // 第6步：创建描述符布局
    // --------------------------------------------------------------------------
    // 创建 MVP 描述符布局（绑定 MVP UBO）和材质描述符布局（绑定材质 UBO）
    // 这些布局定义了 shader 如何访问 UBO 数据
    descManager.createLayouts();

    // --------------------------------------------------------------------------
    // 第7步：初始化阴影系统
    // --------------------------------------------------------------------------
    // 一次性完整初始化阴影系统，包括：
    // - 阴影贴图（depth image + image view）
    // - 阴影渲染通道和管线
    // - 阴影采样器描述符布局
    // - 阴影 UBO（存储光源空间矩阵）
    shadowSystem.initialize(ctx.getDevice(), ctx.getPhysicalDevice(), shaderDir, swapchain.getImageCount());

    // --------------------------------------------------------------------------
    // 第8步：创建主渲染通道、管线布局、图形管线、帧缓冲
    // --------------------------------------------------------------------------
    // 创建渲染通道（定义颜色附件和深度附件）
    renderPipeline.createRenderPass(swapchain.getFormat());  // 使用交换链的图像格式

    // 创建管线布局（定义描述符集布局的顺序）：
    // set 0: MVP 布局, set 1: 材质布局, set 2: 阴影采样器布局
    renderPipeline.createPipelineLayout(
        descManager.getMVPLayout(),       // 第0套描述符布局（MVP 矩阵）
        descManager.getMaterialLayout(),  // 第1套描述符布局（材质参数）
        shadowSystem.getSamplerLayout()); // 第2套描述符布局（阴影贴图采样器）

    // 创建图形管线（编译着色器程序，设置渲染状态）
    renderPipeline.createGraphicsPipeline(swapchain.getExtent(), shaderDir);

    // 为每张交换链图像创建帧缓冲（关联渲染通道和图像视图）
    renderPipeline.createFramebuffers(swapchain.getExtent(),
        swapchain.getImageViews(),    // 颜色附件图像视图
        swapchain.getDepthImageView()); // 深度附件图像视图

    // --------------------------------------------------------------------------
    // 第9步：创建同步对象
    // --------------------------------------------------------------------------
    // 为每帧创建围栏（fence）、图像可用信号量（imageAvailableSemaphore）、渲染完成信号量（renderFinishedSemaphore）
    syncManager.create(swapchain.getImageCount());  // 数量与交换链图像数一致

    // --------------------------------------------------------------------------
    // 第10步：创建描述符池并分配描述符集
    // --------------------------------------------------------------------------
    descManager.createPool(MAX_FRAMES_IN_FLIGHT);  // 创建描述符池（足够分配 MAX_FRAMES_IN_FLIGHT 套描述符）
    descManager.allocateSets(MAX_FRAMES_IN_FLIGHT);  // 为每帧分配 MVP 描述符集和材质描述符集

    // 绑定每帧的描述符集到对应的 UBO（一次性完成，之后每帧只写 UBO 内存）
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        // 绑定 MVP 和材质描述符集到对应的 UBO
        descManager.updateSets(i,
            meshManager.getMVPBuffer(i),         // 第 i 帧的 MVP UBO
            meshManager.getMaterialBuffer(i));   // 第 i 帧的材质 UBO
        // 绑定地面材质描述符集（地面有独立的材质参数）
        descManager.updateGroundSets(i,
            meshManager.getMaterialGroundBuffer(i));  // 地面的材质 UBO
    }

    // --------------------------------------------------------------------------
    // 第11步：分配命令缓冲
    // --------------------------------------------------------------------------
    cmdManager.allocateBuffers(MAX_FRAMES_IN_FLIGHT);  // 为每帧分配主渲染命令缓冲
    cmdManager.allocateShadowCommandBuffer();            // 分配一个共享的阴影渲染命令缓冲
}

// ============================================================================
// 输入处理
// ============================================================================

// 处理键盘输入，更新相机位置和材质状态
// 参数: dt - 时间增量（秒），用于控制移动速度
void PBRApp::handleInput(float dt) {
    float speed = 5.0f * dt;  // 移动速度（单位：米/秒），dt 确保帧率无关的移动一致性

    GLFWwindow* h = window.getHandle();  // 获取 GLFW 窗口句柄

    // ==========================================================================
    // 相机移动（WASD/QE）
    // ==========================================================================
    if (glfwGetKey(h, GLFW_KEY_W) == GLFW_PRESS) camera.moveForward(speed);   // 向前移动（沿前向向量）
    if (glfwGetKey(h, GLFW_KEY_S) == GLFW_PRESS) camera.moveForward(-speed);  // 向后移动（反向前向向量）
    if (glfwGetKey(h, GLFW_KEY_A) == GLFW_PRESS) camera.moveRight(-speed);    // 向左移动（沿左向向量取反）
    if (glfwGetKey(h, GLFW_KEY_D) == GLFW_PRESS) camera.moveRight(speed);     // 向右移动（沿右向向量）
    if (glfwGetKey(h, GLFW_KEY_Q) == GLFW_PRESS) camera.moveUp(-speed);       // 向下移动（沿下向向量）
    if (glfwGetKey(h, GLFW_KEY_E) == GLFW_PRESS) camera.moveUp(speed);        // 向上移动（沿上向向量）

    // ==========================================================================
    // 材质切换 (M键)
    // ==========================================================================
    static bool mLast = false;  // 静态变量：记录上一帧 M 键的状态（用于边沿检测）
    bool mNow = glfwGetKey(h, GLFW_KEY_M) == GLFW_PRESS;  // 当前帧 M 键是否按下
    if (mNow && !mLast) {  // 仅在按下瞬间触发一次（上升沿检测）
        materialSystem.nextPreset();  // 切换到下一个材质预设（循环遍历：默认→金属→非金属→...）
        std::cout << "Material preset: " << materialSystem.getPreset() << "\n";  // 输出当前材质名称
    }
    mLast = mNow;  // 更新状态记录

    // ==========================================================================
    // 玻璃效果开关 (G键)
    // ==========================================================================
    static bool gLast = false;  // 静态变量：记录上一帧 G 键的状态
    bool gNow = glfwGetKey(h, GLFW_KEY_G) == GLFW_PRESS;  // 当前帧 G 键是否按下
    if (gNow && !gLast) {  // 仅在按下瞬间触发一次
        materialSystem.toggleGlass();  // 切换玻璃效果（透明/折射开关）
        std::cout << "Glass: " << (materialSystem.isGlassEnabled() ? "ON" : "OFF") << "\n";  // 输出当前状态
    }
    gLast = gNow;  // 更新状态记录

    // ==========================================================================
    // 自发光开关 (F键)
    // ==========================================================================
    static bool eLast = false;  // 静态变量：记录上一帧 F 键的状态
    bool eNow = glfwGetKey(h, GLFW_KEY_F) == GLFW_PRESS;  // 当前帧 F 键是否按下
    if (eNow && !eLast) {  // 仅在按下瞬间触发一次
        materialSystem.toggleEmissive();  // 切换自发光效果（物体自身发光开关）
        std::cout << "Emissive: " << (materialSystem.isEmissiveEnabled() ? "ON" : "OFF") << "\n";  // 输出当前状态
    }
    eLast = eNow;  // 更新状态记录
}

// ============================================================================
// 主循环
// ============================================================================

// 主渲染循环：持续处理事件、输入、渲染，直到窗口关闭
void PBRApp::mainLoop() {
    while (!window.shouldClose()) {  // 当窗口未关闭时循环（shouldClose() 在用户点击关闭按钮时返回 true）
        window.pollEvents();         // 处理 GLFW 事件（键盘、鼠标、窗口事件等）
        handleInput(0.016f);         // 处理键盘输入（假设 ~60 FPS，传入 16ms 作为时间增量）
        drawFrame();                 // 渲染一帧
        std::this_thread::sleep_for(std::chrono::milliseconds(16));  // 休眠 16ms，限制帧率约 60 FPS
    }
    vkDeviceWaitIdle(ctx.getDevice());  // 等待设备空闲，确保所有渲染完成后再清理资源
}

// ============================================================================
// 单帧渲染
// ============================================================================

// 单帧渲染核心逻辑：同步 → 更新 UBO → 阴影通道 → 主渲染通道 → 呈现
void PBRApp::drawFrame() {
    // --------------------------------------------------------------------------
    // 第1步：等待围栏，确保上一帧完成
    // --------------------------------------------------------------------------
    syncManager.waitForFence();  // 阻塞等待当前帧的围栏被触发（防止多帧同时使用同一资源）

    // --------------------------------------------------------------------------
    // 第2步：获取下一张交换链图像
    // --------------------------------------------------------------------------
    uint32_t imgIdx;  // 接收获取到的交换链图像索引
    // 获取下一张可用图像，使用图像可用信号量通知 GPU 图像何时可用
    VkResult r = vkAcquireNextImageKHR(ctx.getDevice(), swapchain.getSwapchain(), UINT64_MAX,
        syncManager.getImageAvailableSemaphore(),  // 信号量：图像可用时触发
        VK_NULL_HANDLE,                            // 不使用围栏
        &imgIdx);                                  // 输出：图像索引

    // --------------------------------------------------------------------------
    // 第3步：检查交换链状态
    // --------------------------------------------------------------------------
    // 如果交换链过期或窗口大小改变，则重建交换链
    if (r == VK_ERROR_OUT_OF_DATE_KHR || window.isFramebufferResized()) {
        window.resetFramebufferResized();  // 重置 framebufferResized 标志
        recreateSwapchain();               // 重建交换链及相关资源
        return;                            // 跳过本帧渲染
    }

    // --------------------------------------------------------------------------
    // 第4步：标记图像使用中
    // --------------------------------------------------------------------------
    syncManager.markImageInFlight(imgIdx);  // 防止同一图像被多帧同时使用
    uint32_t frameIdx = syncManager.getCurrentFrame();  // 获取当前帧索引（0 或 1）

    // --------------------------------------------------------------------------
    // 第5步：计算相机和投影矩阵
    // --------------------------------------------------------------------------
    Mat4 view = camera.getViewMatrix();  // 获取相机视图矩阵（根据相机位置和朝向计算）
    float aspect = (float)swapchain.getExtent().width / (float)swapchain.getExtent().height;  // 计算宽高比
    Mat4 proj = Mat4::perspective(45.0f * static_cast<float>(M_PI) / 180.0f, aspect, 0.1f, 100.0f);  // 透视投影矩阵（FOV 45°，近裁剪面 0.1m，远裁剪面 100m）
    Mat4 model = Mat4::identity();  // 模型矩阵为单位矩阵（物体位于原点，无变换）

    // --------------------------------------------------------------------------
    // 第6步：更新阴影 UBO
    // --------------------------------------------------------------------------
    // Shadow UBO 必须先更新（内部会计算 lightView / lightProj）
    // 注意：shadow UBO 数量 = MAX_FRAMES_IN_FLIGHT，用 frameIdx 索引
    shadowSystem.updateShadowUBO(frameIdx, model);  // 计算光源空间矩阵并存入 UBO

    // --------------------------------------------------------------------------
    // 第7步：获取光源空间矩阵
    // --------------------------------------------------------------------------
    // 光源空间矩阵（用于主 pass 的 MVP UBO，以便 shader 做阴影坐标变换）
    Mat4 lightSpaceMatrix = shadowSystem.getLightProj() * shadowSystem.getLightView();  // 光源投影矩阵 × 光源视图矩阵

    // --------------------------------------------------------------------------
    // 第8步：更新主渲染 UBO
    // --------------------------------------------------------------------------
    // 更新主 pass UBOs（MVP 矩阵、材质参数、光源参数等）
    meshManager.updateUniformBuffers(frameIdx,
        model, view, proj, lightSpaceMatrix,  // 矩阵数据
        camera.getPosition(),                  // 相机位置（用于视角相关计算）
        materialSystem.getPreset(),            // 当前材质预设
        materialSystem.isGlassEnabled(),       // 玻璃效果开关
        materialSystem.isEmissiveEnabled());   // 自发光开关

    // descriptor set 已经在 init 时绑定到每帧 UBO，无需每帧更新

    // --------------------------------------------------------------------------
    // 第9步：阴影通道渲染
    // --------------------------------------------------------------------------
    // 1) Shadow pass — 独立提交并等待完成（主 pass 需要采样 shadow map）
    VkCommandBuffer shadowCmd = cmdManager.getShadowCommandBuffer();  // 获取阴影命令缓冲
    uint32_t sFrameIdx = syncManager.getCurrentFrame();  // 获取当前帧索引
    // 记录阴影渲染命令：将球体渲染到阴影贴图（深度图）
    shadowSystem.recordShadowPass(shadowCmd, sFrameIdx,
        meshManager.getSphereVBO(),           // 球体顶点缓冲
        meshManager.getSphereIBO(),           // 球体索引缓冲
        meshManager.getSphereIndexCount());   // 球体索引数量
    // 提交阴影命令缓冲
    VkSubmitInfo shadowSubmit{};
    shadowSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    shadowSubmit.commandBufferCount = 1;
    shadowSubmit.pCommandBuffers = &shadowCmd;
    vkQueueSubmit(ctx.getGraphicsQueue(), 1, &shadowSubmit, VK_NULL_HANDLE);  // 不使用围栏，直接提交
    vkQueueWaitIdle(ctx.getGraphicsQueue());  // 等待阴影渲染完成（确保主渲染时阴影贴图已就绪）

    // --------------------------------------------------------------------------
    // 第10步：主渲染通道记录
    // --------------------------------------------------------------------------
    // 2) 主 pass
    VkCommandBuffer mainCmd = cmdManager.getBuffer(frameIdx);  // 获取当前帧的主渲染命令缓冲
    {
        // 准备描述符集数组（按顺序绑定到管线的 set 0/1/2）
        VkDescriptorSet descSets[] = {
            descManager.getMVPSet(frameIdx),           // set 0: MVP 描述符集（包含 MVP UBO）
            descManager.getMaterialSet(frameIdx),      // set 1: 材质描述符集（包含材质 UBO）
            shadowSystem.getSamplerSet(imgIdx),        // set 2: 阴影采样器描述符集（包含阴影贴图）
        };
        std::vector<VkDescriptorSet> dsVec(descSets, descSets + 3);  // 转为 vector 传递给渲染管线
        // 记录主渲染命令：绘制球体和平面
        renderPipeline.recordMainPass(mainCmd, imgIdx, swapchain.getExtent(), dsVec,
            descManager.getMaterialGroundSet(frameIdx),  // 地面材质描述符集（独立于球体材质）
            meshManager.getSphereVBO(), meshManager.getSphereIBO(), meshManager.getSphereIndexCount(),  // 球体数据
            meshManager.getPlaneVBO(), meshManager.getPlaneIBO(), meshManager.getPlaneIndexCount(),     // 平面数据
            materialSystem.isEmissiveEnabled(),  // 自发光开关
            materialSystem.isGlassEnabled());    // 玻璃效果开关
    }

    // --------------------------------------------------------------------------
    // 第11步：重置围栏并提交渲染命令
    // --------------------------------------------------------------------------
    syncManager.resetFence();  // 重置围栏为未触发状态（为本次提交做准备）

    // 准备提交信息
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};  // 等待阶段：颜色附件输出前
    VkSemaphore wait[] = {syncManager.getImageAvailableSemaphore()};  // 等待的信号量：图像可用
    VkSemaphore sig[]  = {syncManager.getRenderFinishedSemaphore()};  // 触发的信号量：渲染完成

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;       // 等待 1 个信号量
    si.pWaitSemaphores = wait;       // 等待图像可用信号量
    si.pWaitDstStageMask = waitStages;  // 在颜色附件输出阶段之前等待
    si.commandBufferCount = 1;       // 提交 1 个命令缓冲
    si.pCommandBuffers = &mainCmd;   // 主渲染命令缓冲
    si.signalSemaphoreCount = 1;     // 触发 1 个信号量
    si.pSignalSemaphores = sig;      // 渲染完成后触发信号量

    // 提交渲染命令，并使用 in-flight 围栏跟踪
    if (vkQueueSubmit(ctx.getGraphicsQueue(), 1, &si, syncManager.getInFlightFence()) != VK_SUCCESS)
        throw std::runtime_error("queue submit failed");  // 提交失败抛出异常

    // --------------------------------------------------------------------------
    // 第12步：呈现到屏幕
    // --------------------------------------------------------------------------
    VkSwapchainKHR sc = swapchain.getSwapchain();  // 获取交换链句柄
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;       // 等待 1 个信号量
    pi.pWaitSemaphores = sig;        // 等待渲染完成信号量
    pi.swapchainCount = 1;           // 呈现 1 个交换链
    pi.pSwapchains = &sc;            // 交换链句柄
    pi.pImageIndices = &imgIdx;      // 呈现的图像索引

    r = vkQueuePresentKHR(ctx.getPresentQueue(), &pi);  // 将渲染结果呈现到屏幕

    // --------------------------------------------------------------------------
    // 第13步：检查呈现状态
    // --------------------------------------------------------------------------
    // 如果交换链过期或次优，或者窗口大小改变，则重建交换链
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || window.isFramebufferResized()) {
        window.resetFramebufferResized();  // 重置标志
        recreateSwapchain();               // 重建交换链
    }

    // --------------------------------------------------------------------------
    // 第14步：推进帧索引
    // --------------------------------------------------------------------------
    syncManager.advanceFrame();  // 更新当前帧索引（0 → 1 → 0 → ... 循环）
}

// ============================================================================
// Swapchain 重建
// ============================================================================

// 重建交换链及相关资源（窗口大小改变或交换链过期时调用）
void PBRApp::recreateSwapchain() {
    vkDeviceWaitIdle(ctx.getDevice());  // 等待设备空闲，确保没有正在使用的资源

    // 销毁依赖 swapchain extent 的资源
    renderPipeline.cleanup();  // 销毁帧缓冲、图形管线、渲染通道、管线布局

    // 重新创建交换链（新的 extent、图像、图像视图、深度缓冲）
    swapchain.recreate(ctx.getSurface(), window.getHandle());

    // 重建依赖 swapchain 的资源
    renderPipeline.createRenderPass(swapchain.getFormat());  // 重新创建渲染通道（可能需要新的图像格式）
    renderPipeline.createPipelineLayout(
        descManager.getMVPLayout(),       // MVP 描述符布局（不变）
        descManager.getMaterialLayout(),  // 材质描述符布局（不变）
        shadowSystem.getSamplerLayout()); // 阴影采样器布局（不变）
    renderPipeline.createGraphicsPipeline(swapchain.getExtent(), shaderDir);  // 重新创建图形管线（viewport/scissor 依赖 extent）
    renderPipeline.createFramebuffers(swapchain.getExtent(),
        swapchain.getImageViews(),    // 新的颜色附件图像视图
        swapchain.getDepthImageView()); // 新的深度附件图像视图
}

// ============================================================================
// 清理
// ============================================================================

// 清理所有 Vulkan 资源和窗口资源（按初始化的逆序销毁）
void PBRApp::cleanup() {
    vkDeviceWaitIdle(ctx.getDevice());  // 等待设备空闲，确保所有渲染完成

    // 按 reverse-init 顺序清理各 Manager
    cmdManager.cleanup();     // 释放命令缓冲和命令池
    shadowSystem.cleanup();   // 销毁阴影贴图、阴影管线、阴影 UBO
    descManager.cleanup();    // 释放描述符池和描述符集
    syncManager.cleanup();    // 销毁围栏和信号量
    renderPipeline.cleanup(); // 销毁渲染管线、渲染通道、帧缓冲、管线布局
    meshManager.cleanup();    // 释放顶点/索引缓冲、UBO
    swapchain.cleanup();      // 销毁交换链、图像视图、深度缓冲

    // 核心 Vulkan 上下文（device + surface + instance）
    ctx.cleanup();  // 销毁逻辑设备、Vulkan 表面、Vulkan 实例

    // 窗口
    window.destroy();  // 销毁 GLFW 窗口
    glfwTerminate();   // 终止 GLFW 库，释放所有资源
}
