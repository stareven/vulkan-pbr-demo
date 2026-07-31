#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include <string>
#include "types.h"

// 前向声明
class ImGuiManager;

// ============================================================================
// 渲染管线 - 渲染通道、管线布局、图形管线、帧缓冲、主 pass 录制
// ============================================================================
// RenderPipeline 类封装了 Vulkan 的核心渲染基础设施：
// 1. VkRenderPass（渲染通道）：定义附件（颜色/深度）的使用方式和布局转换规则
// 2. VkPipelineLayout（管线布局）：声明着色器使用的描述符集布局和推送常量范围
// 3. VkPipeline（图形管线）：包含顶点/片元着色器以及光栅化、深度测试、混合等状态
// 4. VkFramebuffer（帧缓冲）：将渲染通道与实际图像视图（交换链和深度图）绑定
class RenderPipeline {
private:
    // Vulkan 逻辑设备句柄，用于创建所有 Vulkan 对象
    VkDevice device = VK_NULL_HANDLE;

    // 渲染通道对象：定义了渲染过程中的附件配置（颜色附件 + 深度附件）
    // RenderPass 描述了：
    // - 有哪些附件（attachments），它们的格式、加载/存储操作（loadOp/storeOp）
    // - 子通道（subpass）如何使用这些附件
    // - 子通道之间的依赖关系（dependency），包括布局转换和内存屏障
    VkRenderPass renderPass = VK_NULL_HANDLE;

    // 管线布局：描述了着色器可以访问的资源布局
    // - 3 个描述符集布局（MVP 矩阵、材质数据、阴影采样器）
    // - 1 个推送常量范围（emissiveTarget）
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    // 两条图形管线，用于处理不同的透明度需求：
    // - 不透明管线：启用深度写入（depthWriteEnable = VK_TRUE）
    //   用于地面和不透明球体，确保正确的深度测试和遮挡
    // - 半透明管线：禁用深度写入（depthWriteEnable = VK_FALSE）
    //   用于玻璃效果球体，避免半透明物体写入深度导致后续物体被错误遮挡
    VkPipeline pipelineOpaque = VK_NULL_HANDLE;    // 不透明物体（深度写入开）
    VkPipeline pipelineTransparent = VK_NULL_HANDLE; // 半透明物体（深度写入关）

    // 帧缓冲数组：每个帧缓冲对应交换链的一张图像
    // 帧缓冲将渲染通道中定义的附件与实际图像视图绑定：
    // - 颜色附件视图（来自交换链）
    // - 深度附件视图（我们创建的深度缓冲区）
    std::vector<VkFramebuffer> framebuffers;

public:
    RenderPipeline() = default;
    ~RenderPipeline();

    // 初始化设备句柄（必须在其他方法之前调用）
    void init(VkDevice dev) { device = dev; }

    // 创建渲染通道（RenderPass）
    // RenderPass 是 Vulkan 中最重要的概念之一，它定义了：
    // 1. 附件（Attachments）：颜色和深度缓冲区的格式、采样数、加载/存储操作
    // 2. 子通道（Subpasses）：渲染的步骤，可以使用不同的附件组合
    // 3. 依赖（Dependencies）：子通道之间或外部与子通道之间的同步规则
    //
    // 参数 swapchainFormat：交换链图像的格式（如 VK_FORMAT_B8G8R8A8_SRGB）
    //                       必须与附件格式匹配
    void createRenderPass(VkFormat swapchainFormat);

    // 创建管线布局（PipelineLayout）
    // PipelineLayout 声明了着色器可以访问的资源类型和数量：
    // 1. 描述符集布局（DescriptorSetLayout）：定义每个集合包含哪些描述符（uniform buffer、sampler 等）
    //    本例使用 3 个集合：
    //    - Set 0: MVP 矩阵和光照空间矩阵（每帧更新）
    //    - Set 1: PBR 材质参数（反照率、金属度、粗糙度等）
    //    - Set 2: 阴影贴图采样器
    // 2. 推送常量（Push Constants）：小量数据直接推送到着色器，比 uniform buffer 更高效
    //    本例使用 1 个 float 传递 emissiveTarget（自发光强度）
    //
    // 参数 mvpLayout: MVP 矩阵的描述符集布局
    // 参数 materialLayout: 材质数据的描述符集布局
    // 参数 shadowSamplerLayout: 阴影采样器的描述符集布局
    void createPipelineLayout(VkDescriptorSetLayout mvpLayout,
                             VkDescriptorSetLayout materialLayout,
                             VkDescriptorSetLayout shadowSamplerLayout);

    // 创建图形管线（Graphics Pipeline）
    // Vulkan 的图形管线是一系列固定的渲染阶段：
    // 1. 顶点着色器（Vertex Shader）：处理顶点位置和属性
    // 2. 输入装配（Input Assembly）：指定如何组装顶点（三角形列表、线等）
    // 3. 光栅化（Rasterization）：将几何体转换为像素片段
    // 4. 深度/模板测试（Depth/Stencil）：决定哪些片段可见
    // 5. 片元着色器（Fragment Shader）：计算每个像素的最终颜色
    // 6. 颜色混合（Color Blending）：将新颜色与现有颜色混合
    //
    // 本函数创建两条管线：
    // - 不透明管线：depthWriteEnable = VK_TRUE，用于普通渲染
    // - 半透明管线：depthWriteEnable = VK_FALSE，用于玻璃等透明效果
    //
    // 参数 extent：渲染目标的尺寸（宽度和高度），用于设置视口和裁剪区域
    // 参数 shaderDir：着色器文件所在目录，从中读取 .spv 编译后的着色器代码
    void createGraphicsPipeline(VkExtent2D extent, const std::string& shaderDir);

    // 创建帧缓冲（Framebuffer）
    // Framebuffer 将 RenderPass 中定义的附件与实际图像视图绑定：
    // - 颜色附件：绑定到交换链的图像视图
    // - 深度附件：绑定到我们创建的深度缓冲区图像视图
    //
    // 每个交换链图像都有一个对应的帧缓冲，这样我们可以在它们之间切换渲染
    //
    // 参数 extent：渲染目标尺寸，传递给帧缓冲的宽高
    // 参数 swapchainViews：交换链图像视图数组，每个视图对应一张交换链图像
    // 参数 depthView：深度缓冲区的图像视图，所有帧缓冲共享同一个深度视图
    void createFramebuffers(VkExtent2D extent,
                           const std::vector<VkImageView>& swapchainViews,
                           VkImageView depthView);

    // 清理所有 Vulkan 资源
    // 按相反顺序销毁：帧缓冲 → 管线 → 管线布局 → 渲染通道
    void cleanup();

    // 录制主渲染通道的命令
    // 这个函数负责将所有绘制命令记录到命令缓冲区中，包括：
    // 1. 开始渲染通道（beginRenderPass）
    // 2. 绑定描述符集（bindDescriptorSets）
    // 3. 绑定顶点和索引缓冲区
    // 4. 根据材质切换管线（不透明 vs 半透明）
    // 5. 通过推送常量传递自发光强度
    // 6. 执行绘制调用（drawIndexed）
    // 7. 结束渲染通道
    //
    // 设计原则：从各 Manager 传入所需句柄，避免 RenderPipeline 直接依赖其他 Manager
    //
    // 参数 cmd：命令缓冲区，用于记录渲染命令
    // 参数 imgIdx：当前帧的交换链图像索引，用于选择对应的帧缓冲
    // 参数 extent：渲染区域尺寸
    // 参数 descSets：描述符集数组 [MVP set, 球体材质 set, 阴影 sampler set]
    // 参数 matGroundSet：地面材质的描述符集
    // 参数 sphereVbo/sphereIbo/sphereIndexCount：球体的顶点缓冲、索引缓冲、索引数
    // 参数 planeVbo/planeIbo/planeIndexCount：地面的顶点缓冲、索引缓冲、索引数
    // 参数 emissiveEnabled：是否启用了自发光效果
    // 参数 glassEnabled：是否启用了玻璃效果（使用半透明管线）
    // 参数 imguiManager：可选的 ImGui 管理器，用于在渲染通道结束前绘制 ImGui 界面
    void recordMainPass(VkCommandBuffer cmd, uint32_t imgIdx, VkExtent2D extent,
                        const std::vector<VkDescriptorSet>& descSets,
                        VkDescriptorSet matGroundSet,
                        VkBuffer sphereVbo, VkBuffer sphereIbo, uint32_t sphereIndexCount,
                        VkBuffer planeVbo, VkBuffer planeIbo, uint32_t planeIndexCount,
                        bool emissiveEnabled, bool glassEnabled,
                        ImGuiManager* imguiManager = nullptr) const;

    // 获取内部对象的句柄（只读访问）
    VkRenderPass getRenderPass() const { return renderPass; }
    VkPipeline getPipeline() const { return pipelineOpaque; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    const std::vector<VkFramebuffer>& getFramebuffers() const { return framebuffers; }
};
