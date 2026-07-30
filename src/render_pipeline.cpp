#include "render_pipeline.h"
#include "vulkan_utils.h"
#include "types.h"
using namespace vulkan;

#include <array>
#include <stdexcept>

// 析构函数：目前不需要特殊清理，cleanup() 需要被显式调用
RenderPipeline::~RenderPipeline() {
    // Cleanup should be called explicitly
}

// ============================================================================
// 创建渲染通道（RenderPass）
// ============================================================================
// RenderPass 是 Vulkan 中定义渲染过程的核心对象，它描述了：
// 1. 附件（Attachments）：渲染过程中使用的颜色和深度缓冲区
// 2. 子通道（Subpasses）：渲染的步骤，可以有多个子通道实现延迟渲染等高级技术
// 3. 依赖关系（Dependencies）：确保正确的内存访问顺序和图像布局转换
//
// 本例使用单个子通道，包含两个附件：
// - 颜色附件：用于存储最终输出的像素颜色
// - 深度附件：用于深度测试，决定哪些片段可见
//
// 关键概念说明：
// - loadOp：渲染开始时对附件的操作
//   * VK_ATTACHMENT_LOAD_OP_CLEAR：清除为指定颜色（用于颜色附件）或深度值
//   * VK_ATTACHMENT_LOAD_OP_LOAD：保留之前的内容
//   * VK_ATTACHMENT_LOAD_OP_DONT_CARE：不关心初始内容（性能最优）
//
// - storeOp：渲染结束后对附件的操作
//   * VK_ATTACHMENT_STORE_OP_STORE：保存渲染结果到内存
//   * VK_ATTACHMENT_STORE_OP_DONT_CARE：丢弃结果（性能最优，适用于中间渲染目标）
//
// - initialLayout/finalLayout：图像在渲染前后的布局
//   Vulkan 要求图像在使用前处于正确的布局，布局转换会自动进行
void RenderPipeline::createRenderPass(VkFormat swapchainFormat) {
    // 定义两个附件的描述
    std::array<VkAttachmentDescription, 2> att{};

    // --- 颜色附件配置 ---
    // 格式：使用交换链的格式，确保与显示兼容
    att[0].format = swapchainFormat;
    // 采样数：1x 无多重采样（MSAA 需要更多采样点）
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    // 加载操作：渲染开始前清除为 clearColor（在 render pass begin 时指定）
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // 存储操作：渲染结束后保存结果，以便呈现到屏幕
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    // 模板缓冲：不使用，设为 DONT_CARE 以获得最佳性能
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // 初始布局：UNDEFINED 表示我们不关心之前的内容（驱动可以选择最优方式处理）
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // 最终布局：PRESENT_SRC_KHR 是呈现到屏幕所必需的布局
    att[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // --- 深度附件配置 ---
    // 格式：D32_SFLOAT 提供高精度的 32 位浮点深度值
    att[1].format = VK_FORMAT_D32_SFLOAT;
    // 采样数：与颜色附件匹配
    att[1].samples = VK_SAMPLE_COUNT_1_BIT;
    // 加载操作：清除为深度值 1.0（最远距离）
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // 存储操作：渲染后不需要保存深度图，设为 DONT_CARE 提升性能
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // 初始布局：UNDEFINED，不关心之前内容
    att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // 最终布局：DEPTH_STENCIL_ATTACHMENT_OPTIMAL 是深度测试的最优布局
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // --- 附件引用（Attachment Reference）---
    // 引用定义了子通道如何使用附件，以及使用时的图像布局
    // 参数 0/1：对应 attachments 数组中的索引
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    // --- 子通道描述 ---
    // 子通道是 RenderPass 的核心执行单元
    // 本例只有一个子通道，直接使用颜色和深度附件进行渲染
    VkSubpassDescription sub{};
    // 绑定类型：图形管线（区别于计算管线）
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    // 颜色附件数量：1 个（支持多渲染目标 MRT，但本例只用一个）
    sub.colorAttachmentCount = 1;
    // 指向颜色附件引用数组
    sub.pColorAttachments = &colorRef;
    // 指向深度/模板附件引用
    sub.pDepthStencilAttachment = &depthRef;

    // --- 子通道依赖 ---
    // 依赖确保了渲染开始前的正确同步和图像布局转换
    // 这里定义了外部（VK_SUBPASS_EXTERNAL）与第一个子通道（dstSubpass=0）之间的依赖
    VkSubpassDependency dep{};
    // 源子通道：EXTERNAL 表示来自 RenderPass 外部的操作（如交换链获取图像）
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    // 目标子通道：我们的主渲染子通道
    dep.dstSubpass = 0;
    // 源阶段掩码：等待这些管线阶段完成后再开始渲染
    // COLOR_ATTACHMENT_OUTPUT_BIT：颜色附件输出阶段
    // EARLY_FRAGMENT_TESTS_BIT：深度/模板测试阶段
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    // 目标阶段掩码：这些阶段需要等待依赖满足后才能执行
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    // 源访问掩码：0 表示不等待任何特定的内存写入
    dep.srcAccessMask = 0;
    // 目标访问掩码：允许对颜色和深度附件进行写入操作
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // --- 创建渲染通道 ---
    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    // 附件数量：2 个（颜色 + 深度）
    rpi.attachmentCount = (uint32_t)att.size();
    rpi.pAttachments = att.data();
    // 子通道数量：1 个
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    // 依赖数量：1 个
    rpi.dependencyCount = 1;
    rpi.pDependencies = &dep;

    if (vkCreateRenderPass(device, &rpi, nullptr, &renderPass) != VK_SUCCESS)
        throw std::runtime_error("render pass creation failed");
}

// ============================================================================
// 创建管线布局（PipelineLayout）
// ============================================================================
// PipelineLayout 声明了着色器可以访问的资源接口：
// 1. 描述符集布局（DescriptorSetLayout）：每个集合包含一组描述符（uniform buffer、sampler 等）
// 2. 推送常量（Push Constants）：小量数据直接推送到 GPU，比 uniform buffer 更高效
//
// 本例使用了 3 个描述符集布局：
// - Set 0: MVP 矩阵和光照空间矩阵（每帧更新一次）
// - Set 1: PBR 材质参数（反照率、金属度、粗糙度、IOR、不透明度等）
// - Set 2: 阴影贴图采样器
//
// 推送常量：
// - 传递 emissiveTarget（自发光强度），通过顶点着色器传递给片元着色器
// - 大小仅 4 字节（1 个 float），非常适合 push constants 的小容量限制
void RenderPipeline::createPipelineLayout(VkDescriptorSetLayout mvpLayout,
                                         VkDescriptorSetLayout materialLayout,
                                         VkDescriptorSetLayout shadowSamplerLayout) {
    // 按顺序排列描述符集布局数组，与着色器中的 layout(set=N, ...) 对应
    VkDescriptorSetLayout layouts[] = {mvpLayout, materialLayout, shadowSamplerLayout};

    // 推送常量范围：定义可推送的数据区域
    VkPushConstantRange pushRange{};
    // 阶段标志：只在顶点着色器中使用（从顶点着色器传递到片元着色器）
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    // 偏移量：从 0 字节开始
    pushRange.offset = 0;
    // 大小：推送 1 个 float（emissiveTarget）
    pushRange.size = sizeof(float);  // emissiveTarget

    // 管线布局创建信息
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    // 描述符集布局数量：3 个
    pli.setLayoutCount = 3;
    pli.pSetLayouts = layouts;
    // 推送常量范围数量：1 个
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device, &pli, nullptr, &pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("pipeline layout creation failed");
}

// ============================================================================
// 创建图形管线（Graphics Pipeline）
// ============================================================================
// Vulkan 的图形管线由多个固定功能阶段组成：
// 1. Vertex Input：定义顶点数据的格式和布局
// 2. Input Assembly：指定如何组装顶点（三角形列表、扇形、条带等）
// 3. Viewport & Scissor：设置视口变换和裁剪区域
// 4. Rasterization：将几何体光栅化为片段，设置剔除模式和填充模式
// 5. Multisample：配置多重采样抗锯齿（MSAA）
// 6. Depth/Stencil：启用深度测试和模板测试
// 7. Color Blend：设置颜色混合方程（用于半透明效果）
// 8. Dynamic State：可以在运行时动态改变的状态（如视口大小）
//
// 本函数创建两条管线以支持不同的渲染需求：
// - 不透明管线：启用深度写入，用于普通物体渲染
// - 半透明管线：禁用深度写入，用于玻璃等透明效果（避免遮挡后方物体）
void RenderPipeline::createGraphicsPipeline(VkExtent2D extent, const std::string& shaderDir) {
    // 构建着色器文件路径
    // .spv 文件是通过 glslangValidator 编译 GLSL 生成的 SPIR-V 字节码
    std::string vsPath = shaderDir + "/shaders/shader.vert.spv";
    std::string fsPath = shaderDir + "/shaders/shader.frag.spv";

    // 读取着色器二进制文件
    auto vsCode = readFile(vsPath);
    auto fsCode = readFile(fsPath);

    // 创建着色器模块：将 SPIR-V 代码包装成 Vulkan 对象
    VkShaderModule vs = createShaderModule(device, vsCode);
    VkShaderModule fs = createShaderModule(device, fsCode);

    // --- 顶点着色器阶段配置 ---
    VkPipelineShaderStageCreateInfo vsInfo{};
    vsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    // 指定这是顶点着色器阶段
    vsInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    // 着色器模块（包含 SPIR-V 代码）
    vsInfo.module = vs;
    // 入口函数名称（GLSL 中的 main 函数）
    vsInfo.pName = "main";

    // --- 片元着色器阶段配置 ---
    VkPipelineShaderStageCreateInfo fsInfo{};
    fsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    // 指定这是片元着色器阶段
    fsInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fsInfo.module = fs;
    fsInfo.pName = "main";

    // 着色器阶段数组：按顺序传递顶点和片元着色器
    VkPipelineShaderStageCreateInfo stages[] = {vsInfo, fsInfo};

    // --- 顶点输入状态 ---
    // 定义顶点数据的格式和如何从缓冲区读取顶点属性
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    // 顶点绑定描述：定义顶点数据的步长和读取速率
    VkVertexInputBindingDescription bd{};
    // 绑定索引：0（使用第一个也是唯一的顶点缓冲区）
    bd.binding = 0;
    // 步长：每个顶点占用的字节数（sizeof(Vertex)）
    bd.stride = sizeof(Vertex);
    // 输入速率：VERTEX 表示每个顶点取一个数据（INSTANCE 表示每个实例取一个）
    bd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bd;

    // 顶点属性描述：定义每个属性的位置、格式和偏移
    static std::array<VkVertexInputAttributeDescription, 3> attrs{};

    // 属性 0：位置（pos）
    attrs[0].location = 0;          // 着色器中的 location = 0
    attrs[0].binding = 0;           // 绑定到 binding 0
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;  // 3 个 32 位浮点数（x, y, z）
    attrs[0].offset = offsetof(Vertex, pos);  // 在 Vertex 结构体中的偏移量

    // 属性 1：法线（normal）
    attrs[1].location = 1;          // 着色器中的 location = 1
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;  // 3 个 32 位浮点数（nx, ny, nz）
    attrs[1].offset = offsetof(Vertex, normal);

    // 属性 2：纹理坐标（uv）
    attrs[2].location = 2;          // 着色器中的 location = 2
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;     // 2 个 32 位浮点数（u, v）
    attrs[2].offset = offsetof(Vertex, uv);

    vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vi.pVertexAttributeDescriptions = attrs.data();

    // --- 输入装配状态 ---
    // 定义如何将顶点组装为图元
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    // 拓扑结构：三角形列表（每 3 个顶点构成一个三角形）
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // --- 视口和裁剪状态 ---
    // 定义从裁剪空间到屏幕空间的映射
    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    // 视口数量：1 个（支持多视口渲染）
    vpState.viewportCount = 1;
    // 裁剪矩形数量：1 个
    vpState.scissorCount = 1;

    // --- 光栅化状态 ---
    // 控制光栅化过程中的行为
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    // 多边形模式：FILL 填充整个多边形（LINE 只绘制边框，POINT 只绘制顶点）
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    // 剔除模式：NONE 不剔除任何面（BACK 剔除背面，FRONT 剔除正面）
    rs.cullMode = VK_CULL_MODE_NONE;
    // 正面朝向：COUNTER_CLOCKWISE 逆时针为正面
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    // 线宽：仅在 LINE 模式下有效
    rs.lineWidth = 1.0f;

    // --- 多重采样状态 ---
    // 配置抗锯齿（MSAA）
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    // 采样数：1 表示无多重采样（4 表示 4x MSAA）
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // --- 深度/模板状态 ---
    // 控制深度测试和模板测试的行为
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    // 启用深度测试：比较片段的深度值决定是否可见
    ds.depthTestEnable = VK_TRUE;
    // 启用深度写入：将可见片段的深度写入深度缓冲区
    // 注意：这个值会在创建两条管线时被分别覆盖
    ds.depthWriteEnable = VK_TRUE;
    // 深度比较操作：LESS 表示新片段深度小于已有值时才可见
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    // --- 颜色混合附件状态 ---
    // 定义如何将新颜色与帧缓冲中的现有颜色混合
    VkPipelineColorBlendAttachmentState cbAtt{};
    // 颜色写入掩码：允许写入 RGBA 所有通道
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    // 启用混合：开启 alpha 混合实现半透明效果
    cbAtt.blendEnable = VK_TRUE;
    // 混合方程：
    // 最终颜色 = srcColor * srcFactor + dstColor * dstFactor
    cbAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;   // 源颜色因子 = src alpha
    cbAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;  // 目标颜色因子 = 1 - src alpha
    cbAtt.colorBlendOp = VK_BLEND_OP_ADD;  // 混合操作：相加
    // Alpha 混合方程：
    // 最终 alpha = srcAlpha * srcFactor + dstAlpha * dstFactor
    cbAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;         // 源 alpha 因子 = 1
    cbAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;        // 目标 alpha 因子 = 0
    cbAtt.alphaBlendOp = VK_BLEND_OP_ADD;  // 混合操作：相加

    // --- 颜色混合状态 ---
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    // 附件数量：1 个颜色附件
    cb.attachmentCount = 1;
    cb.pAttachments = &cbAtt;

    // --- 动态状态 ---
    // 定义哪些状态可以在录制命令缓冲时动态修改，而无需重新创建管线
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,  // 视口大小可以动态设置
        VK_DYNAMIC_STATE_SCISSOR    // 裁剪区域可以动态设置
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
    dynamicState.pDynamicStates = dynamicStates.data();

    // --- 图形管线创建信息 ---
    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    // 着色器阶段数量：2 个（顶点 + 片元）
    gpi.stageCount = 2;
    gpi.pStages = stages;
    // 连接之前定义的所有状态结构体
    gpi.pVertexInputState = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vpState;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms;
    gpi.pDepthStencilState = &ds;
    gpi.pColorBlendState = &cb;
    gpi.pDynamicState = &dynamicState;
    // 关联管线布局和渲染通道
    gpi.layout = pipelineLayout;
    gpi.renderPass = renderPass;
    // 子通道索引：0 表示使用 RenderPass 的第一个（也是唯一一个）子通道
    gpi.subpass = 0;

    // ========================================================================
    // 创建两条管线：不透明和半透明
    // ========================================================================
    // 由于深度写入启用/禁用在管线创建时是固定的，我们需要两条管线：
    // 1. 不透明管线：启用深度写入，用于地面和不透明球体
    // 2. 半透明管线：禁用深度写入，用于玻璃球体（避免遮挡后方物体）

    // --- 创建不透明管线 ---
    {
        // 复制深度/模板状态，确保深度写入开启
        VkPipelineDepthStencilStateCreateInfo dsOpaque = ds;
        dsOpaque.depthWriteEnable = VK_TRUE;  // 不透明物体写入深度
        gpi.pDepthStencilState = &dsOpaque;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpi, nullptr, &pipelineOpaque) != VK_SUCCESS)
            throw std::runtime_error("opaque pipeline creation failed");
    }

    // --- 创建半透明管线 ---
    {
        // 复制深度/模板状态，禁用深度写入
        VkPipelineDepthStencilStateCreateInfo dsTrans = ds;
        dsTrans.depthWriteEnable = VK_FALSE;  // 半透明物体不写入深度
        gpi.pDepthStencilState = &dsTrans;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpi, nullptr, &pipelineTransparent) != VK_SUCCESS)
            throw std::runtime_error("transparent pipeline creation failed");
    }

    // 销毁临时着色器模块（管线创建后不再需要）
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
}

// ============================================================================
// 创建帧缓冲（Framebuffer）
// ============================================================================
// Framebuffer 将 RenderPass 中定义的附件与实际图像视图绑定：
// - 颜色附件：绑定到交换链的图像视图（每张交换链图像一个帧缓冲）
// - 深度附件：绑定到我们创建的深度缓冲区图像视图（所有帧缓冲共享）
//
// 渲染时，我们通过选择对应的帧缓冲来渲染到特定的交换链图像
void RenderPipeline::createFramebuffers(VkExtent2D extent,
                                       const std::vector<VkImageView>& swapchainViews,
                                       VkImageView depthView) {
    // 调整帧缓冲数组大小，与交换链图像数量一致
    framebuffers.resize(swapchainViews.size());

    for (size_t i = 0; i < swapchainViews.size(); ++i) {
        // 附件数组：颜色附件（交换链图像）+ 深度附件
        std::array<VkImageView, 2> attachments = {swapchainViews[i], depthView};

        // 帧缓冲创建信息
        VkFramebufferCreateInfo fbi{};
        fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        // 关联渲染通道：此帧缓冲只能用于该渲染通道
        fbi.renderPass = renderPass;
        // 附件数量：2 个（颜色 + 深度）
        fbi.attachmentCount = (uint32_t)attachments.size();
        fbi.pAttachments = attachments.data();
        // 帧缓冲尺寸
        fbi.width = extent.width;
        fbi.height = extent.height;
        // 层数：1 层（非 VRAM 多层渲染）
        fbi.layers = 1;

        if (vkCreateFramebuffer(device, &fbi, nullptr, &framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("framebuffer creation failed");
    }
}

// ============================================================================
// 清理资源
// ============================================================================
// 按相反顺序销毁 Vulkan 对象，确保没有对象仍在使用其他对象
void RenderPipeline::cleanup() {
    // 销毁所有帧缓冲
    for (auto fb : framebuffers) {
        if (fb) vkDestroyFramebuffer(device, fb, nullptr);
    }
    framebuffers.clear();

    // 销毁两条图形管线
    if (pipelineOpaque) {
        vkDestroyPipeline(device, pipelineOpaque, nullptr);
        pipelineOpaque = VK_NULL_HANDLE;
    }
    if (pipelineTransparent) {
        vkDestroyPipeline(device, pipelineTransparent, nullptr);
        pipelineTransparent = VK_NULL_HANDLE;
    }

    // 销毁管线布局
    if (pipelineLayout) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }

    // 销毁渲染通道
    if (renderPass) {
        vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
}

// ============================================================================
// 录制主渲染通道的命令
// ============================================================================
// 这个函数将所有绘制命令记录到命令缓冲区中，包括：
// 1. 重置并开始命令缓冲
// 2. 开始渲染通道（beginRenderPass）
// 3. 绑定描述符集（bindDescriptorSets）
// 4. 设置视口和裁剪区域
// 5. 先绘制地面（切换地面材质，使用不透明管线）
// 6. 再绘制球体（根据是否玻璃选择管线，推送自发光强度）
// 7. 结束渲染通道并完成命令缓冲
//
// 设计原则：从各 Manager 传入所需句柄，避免 RenderPipeline 直接依赖其他 Manager
void RenderPipeline::recordMainPass(VkCommandBuffer cmd, uint32_t imgIdx, VkExtent2D extent,
                                    const std::vector<VkDescriptorSet>& descSets,
                                    VkDescriptorSet matGroundSet,
                                    VkBuffer sphereVbo, VkBuffer sphereIbo, uint32_t sphereIndexCount,
                                    VkBuffer planeVbo, VkBuffer planeIbo, uint32_t planeIndexCount,
                                    bool emissiveEnabled, bool glassEnabled) const {
    // 重置命令缓冲，准备重新录制
    // RESET_BIT_NONE：不释放任何资源，只是重置状态
    vkResetCommandBuffer(cmd, 0);

    // 命令缓冲开始信息
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    // --- 清除值配置 ---
    // 定义渲染通道开始时要清除的值
    VkClearValue clears[2];
    // 清除值 [0]：颜色附件的清除颜色（深蓝色背景）
    clears[0].color = {{0.02f, 0.02f, 0.05f, 1.0f}};
    // 清除值 [1]：深度附件的清除深度（1.0 表示最远距离）和模板值（0）
    clears[1].depthStencil = {1.0f, 0};

    // --- 渲染通道开始信息 ---
    VkRenderPassBeginInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    // 关联渲染通道
    rpi.renderPass = renderPass;
    // 选择当前帧对应的帧缓冲
    rpi.framebuffer = framebuffers[imgIdx];
    // 渲染区域尺寸：覆盖整个帧缓冲
    rpi.renderArea.extent = extent;
    // 清除值数量：2 个（颜色 + 深度）
    rpi.clearValueCount = 2;
    rpi.pClearValues = clears;

    // 开始渲染通道
    // VK_SUBPASS_CONTENTS_INLINE：子通道的命令将内联录制在此命令缓冲中
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    // --- 绑定描述符集 ---
    // 将之前创建的描述符集绑定到管线，使着色器可以访问其中的资源
    // 参数顺序：cmd, 绑定类型, 管线布局, 起始集合索引, 集合数量, 集合数组, 动态偏移数量, 动态偏移数组
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 0, (uint32_t)descSets.size(), descSets.data(), 0, nullptr);

    // --- 设置视口 ---
    // 视口定义了从裁剪空间到屏幕空间的映射
    VkViewport vp{0, 0, (float)extent.width, (float)extent.height, 0, 1};
    // 由于视口是动态状态，我们在这里动态设置而不需要在管线中硬编码
    vkCmdSetViewport(cmd, 0, 1, &vp);

    // --- 设置裁剪区域 ---
    // 裁剪区域定义了哪些像素会被丢弃（优化性能）
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // ========================================================================
    // 绘制顺序：先地面后球体
    // ========================================================================
    // 地面是不透明的，所以先用不透明管线绘制
    // 球体可能是玻璃（半透明），所以后用对应的管线绘制

    // --- 绘制地面 ---
    // 切换到地面材质的描述符集（set 1 位置）
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 1, 1, &matGroundSet, 0, nullptr);

    // 绑定不透明管线（地面无需半透明效果）
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineOpaque);

    // 推送常量：地面的自发光强度为 0（不自发光）
    float emissiveTarget = 0.0f;
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(float), &emissiveTarget);

    // 绑定地面的顶点缓冲区和索引缓冲区
    VkBuffer planeVbos[] = {planeVbo};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, planeVbos, offsets);
    vkCmdBindIndexBuffer(cmd, planeIbo, 0, VK_INDEX_TYPE_UINT32);

    // 执行索引绘制调用
    // 参数：命令缓冲, 索引数, 实例数, 第一个索引, 顶点偏移, 实例偏移
    vkCmdDrawIndexed(cmd, planeIndexCount, 1, 0, 0, 0);

    // --- 绘制球体 ---
    // 切换回球体的材质描述符集（descSets[1]）
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 1, 1, &descSets[1], 0, nullptr);

    // 根据是否启用玻璃效果选择管线
    // 玻璃需要半透明管线（禁用深度写入），否则用不透明管线
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      glassEnabled ? pipelineTransparent : pipelineOpaque);

    // 推送常量：根据自发光开关设置 emissiveTarget
    emissiveTarget = emissiveEnabled ? 1.0f : 0.0f;
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(float), &emissiveTarget);

    // 绑定球体的顶点缓冲区和索引缓冲区
    VkBuffer vbos[] = {sphereVbo};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbos, offsets);
    vkCmdBindIndexBuffer(cmd, sphereIbo, 0, VK_INDEX_TYPE_UINT32);

    // 执行索引绘制调用
    vkCmdDrawIndexed(cmd, sphereIndexCount, 1, 0, 0, 0);

    // 结束渲染通道
    vkCmdEndRenderPass(cmd);

    // 完成命令缓冲录制
    vkEndCommandBuffer(cmd);
}
