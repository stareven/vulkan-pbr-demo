#include "imgui_manager.h"
#include "vulkan_context.h"
#include "window.h"

#include <stdexcept>
#include <cstring>

// ============================================================================
// 析构函数
// ============================================================================
ImGuiManager::~ImGuiManager() {
    // 注意: 清理工作应该在 cleanup() 中完成
    // 析构函数只是一个安全网
}

// ============================================================================
// 初始化 ImGui
// ============================================================================
void ImGuiManager::initialize(VulkanContext& ctx, Window& window, VkRenderPass renderPass, uint32_t imageCount) {
    device = ctx.getDevice();

    // 初始化帧时间历史记录数组
    memset(frameTimes, 0, sizeof(frameTimes));

    // 1. 创建 ImGui 上下文
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // 2. 设置 ImGui 样式 (深色主题)
    ImGui::StyleColorsDark();

    // 3. 创建 descriptor pool
    createDescriptorPool(device, imageCount);

    // 4. 初始化 GLFW 后端
    // installCallbacks = true: 让 ImGui 安装鼠标/键盘回调
    if (!ImGui_ImplGlfw_InitForVulkan(window.getHandle(), true)) {
        throw std::runtime_error("Failed to initialize ImGui GLFW backend");
    }

    // 5. 初始化 Vulkan 后端
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = ctx.getInstance();
    initInfo.PhysicalDevice = ctx.getPhysicalDevice();
    initInfo.Device = device;
    initInfo.QueueFamily = ctx.getGraphicsFamily();
    initInfo.Queue = ctx.getGraphicsQueue();
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = imguiPool;
    initInfo.Allocator = nullptr;
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;
    initInfo.PipelineInfoMain.RenderPass = renderPass;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.CheckVkResultFn = [](VkResult err) {
        if (err != VK_SUCCESS) {
            throw std::runtime_error("ImGui Vulkan error");
        }
    };

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
    }

    // 6. 字体纹理会自动上传,无需手动调用
    // 在新版本的 ImGui 中,字体纹理在第一次 NewFrame 时自动创建
}

// ============================================================================
// 清理 ImGui
// ============================================================================
void ImGuiManager::cleanup() {
    if (device == VK_NULL_HANDLE) {
        return; // 未初始化
    }

    // 等待设备空闲
    vkDeviceWaitIdle(device);

    // 1. 关闭 Vulkan 后端
    ImGui_ImplVulkan_Shutdown();

    // 2. 关闭 GLFW 后端
    ImGui_ImplGlfw_Shutdown();

    // 3. 销毁 ImGui 上下文
    ImGui::DestroyContext();

    // 4. 销毁 descriptor pool
    if (imguiPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, imguiPool, nullptr);
        imguiPool = VK_NULL_HANDLE;
    }

    device = VK_NULL_HANDLE;
}

// ============================================================================
// 开始新帧
// ============================================================================
void ImGuiManager::beginFrame() {
    // 开始新帧
    ImGui_ImplGlfw_NewFrame();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
}

// ============================================================================
// 结束帧
// ============================================================================
void ImGuiManager::endFrame() {
    // 结束帧,生成绘制数据
    ImGui::Render();
}

// ============================================================================
// 渲染 ImGui
// ============================================================================
void ImGuiManager::render(VkCommandBuffer cmdBuffer) {
    if (!visible) {
        return;
    }

    // 将 ImGui 绘制数据提交到命令缓冲
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData) {
        ImGui_ImplVulkan_RenderDrawData(drawData, cmdBuffer);
    }
}

// ============================================================================
// 显示渲染统计窗口
// ============================================================================
void ImGuiManager::showStatsWindow(const RenderStats& stats) {
    if (!visible) {
        return;
    }

    // 更新帧时间历史
    frameTimes[frameTimeIndex] = stats.frameTime;
    frameTimeIndex = (frameTimeIndex + 1) % 120;

    // 创建统计窗口
    ImGui::Begin("Render Stats", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    // 性能指标
    ImGui::Text("Performance:");
    ImGui::Text("FPS: %.1f", stats.fps);
    ImGui::Text("Frame Time: %.3f ms", stats.frameTime);

    // 帧时间曲线图
    ImGui::PlotLines("Frame Times", frameTimes, 120, 0, nullptr, 0.0f, 50.0f, ImVec2(300, 50));

    ImGui::Separator();

    // 渲染统计
    ImGui::Text("Rendering:");
    ImGui::Text("Draw Calls: %u", stats.drawCalls);
    ImGui::Text("Triangles: %u", stats.triangles);

    ImGui::Separator();

    // 资源统计
    ImGui::Text("Resources:");
    ImGui::Text("Descriptor Sets: %u", stats.descriptorSets);
    ImGui::Text("Uniform Buffers: %u", stats.uniformBuffers);
    ImGui::Text("Textures: %u", stats.textures);

    ImGui::End();
}

// ============================================================================
// 创建 Descriptor Pool
// ============================================================================
void ImGuiManager::createDescriptorPool(VkDevice device, uint32_t imageCount) {
    // ImGui 需要的 descriptor 类型和数量
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * IM_ARRAYSIZE(poolSizes);
    poolInfo.poolSizeCount = IM_ARRAYSIZE(poolSizes);
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &imguiPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool");
    }
}
