#pragma once

#include <filesystem>
#include <string>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "types.h"
#include "camera.h"  // 相机控制器，管理视图矩阵和相机状态

#include "window.h"              // GLFW 窗口封装
#include "vulkan_context.h"      // Vulkan 实例/设备/队列管理
#include "swapchain_manager.h"   // 交换链生命周期管理
#include "render_pipeline.h"     // 渲染通道/管线/帧缓冲
#include "mesh_manager.h"        // 网格数据（顶点/索引缓冲）
#include "descriptor_manager.h"  // 描述符集和布局管理
#include "command_manager.h"     // 命令池和命令缓冲管理
#include "sync_manager.h"        // 同步对象（围栏/信号量）
#include "shadow_system.h"       // 阴影贴图渲染系统
#include "material_system.h"     // 材质预设和 PBR 材质状态
#include "imgui_manager.h"       // Dear ImGui 渲染统计界面

// ============================================================================
// PBR 主应用类
//
// 采用管理器/系统组合架构：PBRApp 只负责整体编排，不直接管理 Vulkan 资源。
//
// 【初始化顺序】（见 initVulkan()）：
//   1. VulkanContext      - 创建 Vulkan 实例、物理设备、逻辑设备
//   2. 各 Manager 缓存句柄  - 保存 device/queue 等常用句柄
//   3. Swapchain          - 创建交换链、图像视图、深度缓冲
//   4. CommandPool        - 创建命令池（后续资源上传需要）
//   5. Mesh               - 创建顶点缓冲和索引缓冲
//   6. UniformBuffers     - 创建 MVP 和材质的统一缓冲区
//   7. DescriptorLayouts  - 创建描述符布局
//   8. ShadowSystem       - 完整初始化阴影系统（管线+描述符）
//   9. RenderPass+Pipeline+Framebuffers - 创建渲染管线和帧缓冲
//   10. SyncObjects       - 创建围栏和信号量
//   11. DescriptorPool+Sets - 分配描述符集并绑定到 UBO
//   12. CommandBuffers    - 分配命令缓冲
//
// 【渲染流程】（见 drawFrame()）：
//   1. 等待围栏确保上一帧完成
//   2. 获取下一张交换链图像
//   3. 检查是否需要重建交换链
//   4. 更新相机矩阵和 UBO（包括阴影 UBO 和主渲染 UBO）
//   5. 阴影通道：从光源视角渲染深度图到阴影贴图
//   6. 主渲染通道：使用阴影贴图进行 PBR 光照计算
//   7. 提交渲染命令并呈现到屏幕
//   8. 推进帧索引
//
// 【输入处理】（见 handleInput()）：
//   - WASD/QE: 相机前后/左右/上下移动
//   - 鼠标拖拽（左键按下）: 相机旋转
//   - M键: 循环切换材质预设（金属/玻璃/荧光等）
//   - G键: 开关玻璃效果（透明/折射）
//   - F键: 开关自发光效果
//   - F1键: 切换 ImGui 渲染统计窗口显示/隐藏
//
// 【交换链重建】（见 recreateSwapchain()）：
//   触发条件：
//   - vkAcquireNextImageKHR 返回 VK_ERROR_OUT_OF_DATE_KHR
//   - vkQueuePresentKHR 返回 VK_ERROR_OUT_OF_DATE_KHR 或 VK_SUBOPTIMAL_KHR
//   - 窗口大小改变（framebufferResized 标志被设置）
//
//   重建过程：
//   1. 等待设备空闲（vkDeviceWaitIdle）
//   2. 销毁依赖交换链 extent 的资源（RenderPipeline 的帧缓冲、管线等）
//   3. 重新创建交换链（新的图像、视图、深度缓冲）
//   4. 重建 RenderPipeline（渲染通道、管线布局、图形管线、帧缓冲）
//
// 【清理顺序】（见 cleanup()，初始化的逆序）：
//   1. 等待设备空闲
//   2. 命令缓冲（cmdManager）
//   3. 阴影系统（shadowSystem）
//   4. 描述符资源（descManager）
//   5. 同步对象（syncManager）
//   6. 渲染管线（renderPipeline）
//   7. 网格资源（meshManager）
//   8. 交换链（swapchain）
//   9. Vulkan 上下文（ctx：设备、表面、实例）
//   10. 窗口（window）
//   11. GLFW 终止
// ============================================================================
class PBRApp {
public:
    // 构造函数：解析可执行文件路径，确定着色器搜索目录
    // 参数: argc - 命令行参数数量, argv - 命令行参数数组
    PBRApp(int argc, char* argv[]);

    // 应用主入口函数
    // 按顺序调用：initWindow() → initVulkan() → mainLoop() → cleanup()
    void run();

private:
    std::filesystem::path exeDir;   // 可执行文件所在目录
    std::string shaderDir;          // 着色器文件搜索目录（项目根目录，用于查找 shaders/*.spv）

    // ==========================================================================
    // 管理器实例（按初始化依赖顺序排列）
    // ==========================================================================
    Window window{config::WIDTH, config::HEIGHT, config::TITLE};  // GLFW 窗口管理（窗口创建、事件轮询、帧缓冲回调）
    VulkanContext ctx;              // Vulkan 核心上下文（实例创建、物理设备选择、逻辑设备创建、队列获取）
    SwapchainManager swapchain;     // 交换链管理（交换链创建、图像视图、深度缓冲图像、extent 查询）
    RenderPipeline renderPipeline;  // 渲染管线（渲染通道、管线布局、图形管线、帧缓冲创建）
    MeshManager meshManager;        // 网格资源（球体/平面的顶点缓冲 VBO、索引缓冲 IBO、统一缓冲区 UBO）
    DescriptorManager descManager;  // 描述符管理（MVP 描述符布局/集、材质描述符布局/集）
    CommandManager cmdManager;      // 命令管理（命令池创建、命令缓冲分配、主渲染命令记录）
    SyncManager syncManager;        // 同步管理（每帧围栏、图像可用信号量、渲染完成信号量）
    ShadowSystem shadowSystem;      // 阴影系统（阴影贴图、阴影渲染通道、阴影管线、阴影 UBO 更新）
    MaterialSystem materialSystem;  // 材质系统（材质预设管理、玻璃效果开关、自发光效果开关）
    ImGuiManager imguiManager;      // ImGui 管理器（渲染统计界面、帧率显示）

    // ==========================================================================
    // 相机和输入状态
    // ==========================================================================
    Camera camera;                  // 相机控制器（位置、朝向、视图矩阵计算、透视投影）
    bool leftDown = false;          // 鼠标左键是否按下（用于拖拽旋转相机）
    double lastMX = 0, lastMY = 0;  // 上一帧鼠标位置（用于计算拖拽偏移量 dx/dy）

    // ==========================================================================
    // 渲染统计和帧时间
    // ==========================================================================
    RenderStats renderStats;        // 渲染统计信息（FPS、Draw Calls 等）
    std::chrono::high_resolution_clock::time_point lastFrameTime;  // 上一帧时间点

    // --------------------------------------------------------------------------
    // 初始化阶段
    // --------------------------------------------------------------------------
    // 创建 GLFW 窗口，注册回调函数
    //
    // 注册的回调：
    // - framebufferSizeCallback: 窗口大小改变时设置 framebufferResized 标志
    // - cursorPosCallback: 鼠标移动时更新相机旋转（仅当左键按下时）
    // - mouseButtonCallback: 鼠标按键时更新 leftDown 状态和鼠标位置
    void initWindow();

    // 初始化 Vulkan 环境和所有管理器
    //
    // 详细初始化顺序（严格遵循依赖关系）：
    // 1. ctx.initialize()           - 创建 Vulkan 实例、选择物理设备、创建逻辑设备
    // 2. 各 Manager.init()          - 缓存 device/queue 等句柄
    // 3. swapchain.create()         - 创建交换链、图像视图、深度缓冲
    // 4. cmdManager.createPool()    - 创建命令池（后续 mesh upload 需要）
    // 5. meshManager.createMeshes() - 创建球体和平面的 VBO/IBO
    // 6. meshManager.createUniformBuffers() - 创建每帧的 MVP 和材质 UBO
    // 7. descManager.createLayouts() - 创建 MVP 和材质的描述符布局
    // 8. shadowSystem.initialize()  - 完整初始化阴影系统（管线+描述符集）
    // 9. renderPipeline.*           - 创建渲染通道、管线布局、图形管线、帧缓冲
    // 10. syncManager.create()      - 创建每帧的围栏和信号量
    // 11. descManager.createPool() + allocateSets() - 分配描述符集
    // 12. descManager.updateSets()  - 绑定描述符集到对应的 UBO
    // 13. cmdManager.allocate*()    - 分配主渲染命令缓冲和阴影命令缓冲
    void initVulkan();

    // --------------------------------------------------------------------------
    // 运行阶段
    // --------------------------------------------------------------------------
    // 主循环：持续处理事件、输入、渲染，直到窗口关闭
    //
    // 循环体：
    // 1. window.pollEvents()     - 处理 GLFW 事件
    // 2. handleInput(0.016f)    - 处理键盘输入（~60 FPS 假设）
    // 3. drawFrame()            - 渲染一帧
    // 4. sleep_for(16ms)        - 限制帧率约 60 FPS
    //
    // 退出条件：window.shouldClose() 返回 true（用户点击关闭按钮）
    // 退出后：调用 vkDeviceWaitIdle 确保所有渲染完成
    void mainLoop();

    // 处理键盘输入，更新相机位置和材质状态
    // 参数: dt - 时间增量（秒），用于控制移动速度
    //
    // 支持的按键：
    // - W: 相机向前移动（沿前向向量）
    // - S: 相机向后移动（反向前向向量）
    // - A: 相机向左移动（沿左向向量）
    // - D: 相机向右移动（沿右向向量）
    // - Q: 相机向下移动（沿下向向量）
    // - E: 相机向上移动（沿上向向量）
    // - M: 切换材质预设（循环遍历：默认→金属→非金属→...）
    // - G: 切换玻璃效果（透明/折射效果开关）
    // - F: 切换自发光效果（物体自身发光开关）
    // - F1: 切换 ImGui 渲染统计窗口显示/隐藏
    //
    // 注意：M/G/F/F1 键使用边沿检测（按下瞬间触发一次），避免连续触发
    void handleInput(float dt);

    // 单帧渲染核心逻辑
    //
    // 详细渲染流程：
    // 1. 同步：waitForFence() 等待上一帧完成
    // 2. 获取交换链图像：vkAcquireNextImageKHR 获取下一张可用图像
    // 3. 检查交换链状态：如果 OUT_OF_DATE 则触发 recreateSwapchain()
    // 4. 标记图像使用中：markImageInFlight() 防止同一图像被多帧同时使用
    // 5. 计算相机矩阵：getViewMatrix() 和透视投影矩阵
    // 6. 更新阴影 UBO：shadowSystem.updateShadowUBO() 计算光源空间矩阵
    // 7. 更新主渲染 UBO：meshManager.updateUniformBuffers() 写入 MVP + 材质参数
    // 8. 阴影通道渲染：
    //    a. 记录阴影命令缓冲（渲染球体到阴影贴图）
    //    b. 提交阴影命令并等待完成（vkQueueWaitIdle）
    // 9. 主渲染通道记录：
    //    a. 绑定描述符集（MVP set + 材质 set + 阴影采样器 set）
    //    b. 记录渲染命令（球体和平面）
    // 10. 重置围栏：resetFence() 为本次提交做准备
    // 11. 提交渲染命令：vkQueueSubmit 提交主渲染命令缓冲
    // 12. 呈现：vkQueuePresentKHR 将渲染结果提交到屏幕
    // 13. 检查呈现状态：如果 OUT_OF_DATE 则触发 recreateSwapchain()
    // 14. 推进帧索引：advanceFrame() 更新当前帧计数器
    void drawFrame();

    // 重建交换链及相关资源
    //
    // 触发条件（满足任一即触发）：
    // - vkAcquireNextImageKHR 返回 VK_ERROR_OUT_OF_DATE_KHR
    // - vkQueuePresentKHR 返回 VK_ERROR_OUT_OF_DATE_KHR 或 VK_SUBOPTIMAL_KHR
    // - window.isFramebufferResized() 返回 true（窗口大小改变）
    //
    // 重建过程：
    // 1. vkDeviceWaitIdle(ctx.getDevice()) - 等待设备空闲，确保没有正在使用的资源
    // 2. renderPipeline.cleanup()          - 销毁依赖 swapchain extent 的资源
    // 3. swapchain.recreate()              - 重新创建交换链（新 extent、新图像、新视图）
    // 4. renderPipeline.createRenderPass() - 重新创建渲染通道
    // 5. renderPipeline.createPipelineLayout() - 重新创建管线布局
    // 6. renderPipeline.createGraphicsPipeline() - 重新创建图形管线
    // 7. renderPipeline.createFramebuffers() - 重新创建帧缓冲（关联新的交换链图像视图）
    void recreateSwapchain();

    // --------------------------------------------------------------------------
    // 清理阶段（按初始化的逆序销毁资源）
    // --------------------------------------------------------------------------
    // 清理所有 Vulkan 资源和窗口资源
    //
    // 清理顺序（必须严格遵守 Vulkan 资源依赖关系，先创建的後销毁）：
    // 1. vkDeviceWaitIdle()              - 等待设备空闲，确保所有渲染完成
    // 2. cmdManager.cleanup()            - 释放命令缓冲和命令池
    // 3. shadowSystem.cleanup()          - 销毁阴影相关资源
    // 4. descManager.cleanup()           - 释放描述符池和描述符集
    // 5. syncManager.cleanup()           - 销毁围栏和信号量
    // 6. renderPipeline.cleanup()        - 销毁渲染管线、渲染通道、帧缓冲
    // 7. meshManager.cleanup()           - 释放顶点/索引缓冲和 UBO
    // 8. swapchain.cleanup()             - 销毁交换链、图像视图、深度缓冲
    // 9. ctx.cleanup()                   - 销毁 Vulkan 设备、表面、实例
    // 10. window.destroy()               - 销毁 GLFW 窗口
    // 11. glfwTerminate()                - 终止 GLFW 库
    void cleanup();
};
