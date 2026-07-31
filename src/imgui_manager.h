#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vulkan.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "types.h"

// 前向声明
class VulkanContext;
class Window;

// ============================================================================
// ImGui 管理器 - 管理 Dear ImGui 的集成
// ============================================================================
// ImGuiManager 负责:
//   - ImGui 上下文的初始化和清理
//   - ImGui 的 Vulkan 资源管理 (descriptor pool, fonts)
//   - 每帧的 ImGui 更新和渲染
//   - 显示渲染统计信息窗口
//
// 集成方式:
//   - 使用现有的主渲染通道
//   - 在所有 3D 几何体绘制完成后渲染 ImGui
//   - ImGui pipeline 禁用深度测试,启用 alpha 混合
//
// 按键控制:
//   - F1 键: 切换 ImGui 窗口显示/隐藏
// ============================================================================
class ImGuiManager {
public:
    // 默认构造
    ImGuiManager() = default;

    // 析构时自动清理 (但建议在应用退出前显式调用 cleanup())
    ~ImGuiManager();

    // 初始化 ImGui: 创建上下文、设置样式、初始化后端、上传字体
    // ctx: Vulkan 上下文 (提供 device, queues 等)
    // window: GLFW 窗口 (提供窗口句柄给 ImGui GLFW 后端)
    // renderPass: 主渲染通道 (ImGui 会在这个 render pass 中绘制)
    // imageCount: swapchain 图像数量 (用于 descriptor pool 大小)
    void initialize(VulkanContext& ctx, Window& window, VkRenderPass renderPass, uint32_t imageCount);

    // 清理 ImGui: 销毁后端、上下文和 Vulkan 资源
    void cleanup();

    // 开始新帧: 调用 ImGui_ImplGlfw_NewFrame 和 ImGui_ImplVulkan_NewFrame
    void beginFrame();

    // 结束帧: 调用 ImGui::Render() 生成绘制数据
    void endFrame();

    // 渲染 ImGui: 将 ImGui 绘制数据提交到命令缓冲
    // cmdBuffer: 当前帧的命令缓冲 (必须在 render pass 内)
    void render(VkCommandBuffer cmdBuffer);

    // 显示渲染统计窗口
    // stats: 渲染统计信息
    void showStatsWindow(const RenderStats& stats);

    // 设置是否显示 ImGui (由 F1 键控制)
    void setVisible(bool visible) { visible = visible; }
    bool isVisible() const { return visible; }

private:
    // ImGui 是否可见
    bool visible = true;

    // ImGui 使用的 descriptor pool
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;

    // Vulkan 设备句柄 (从 VulkanContext 获取)
    VkDevice device = VK_NULL_HANDLE;

    // 帧时间历史记录 (用于绘制曲线图)
    float frameTimes[120];
    int frameTimeIndex = 0;

    // 创建 ImGui 需要的 descriptor pool
    void createDescriptorPool(VkDevice device, uint32_t imageCount);

    // 上传字体纹理到 GPU
    void uploadFonts(VkCommandBuffer cmdBuffer);
};
