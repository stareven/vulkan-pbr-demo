#include "shadow_system.h"
#include "vulkan_utils.h"
using namespace vulkan;

#include <filesystem>
#include <stdexcept>
#include <cstring>
#include <string>

// 析构函数：资源清理由外部显式调用 cleanup() 完成，析构函数不自动销毁 Vulkan 对象
ShadowSystem::~ShadowSystem() {
    // Cleanup should be called explicitly
}

// ============================================================================
// initialize - 完整初始化阴影系统
// ============================================================================
// 按严格顺序创建所有资源，确保依赖关系正确：
//   1. createShadowMap:          创建深度纹理图像（阴影贴图）
//   2. createShadowRenderPass:   创建深度-only 渲染 pass
//   3. createShadowDescriptorLayout: 创建 UBO descriptor 布局
//   4. createShadowSampler:      创建采样器（主 pass 用于采样阴影贴图）
//   5. createShadowSamplerDescriptorLayout: 创建采样器 descriptor 布局
//   6. createShadowPipeline:     创建图形管线（需要 shader、render pass、descriptor 布局）
//   7. createShadowFramebuffer:  创建帧缓冲（需要 render pass 和深度图像视图）
//   8. createShadowDescriptorSets:     分配 UBO descriptors（需要 descriptor pool 和布局）
//   9. createShadowSamplerDescriptorSets: 分配采样器 descriptors（需要 descriptor pool 和布局）
void ShadowSystem::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                              const std::string& shaderDir, uint32_t imageCount) {
    this->device = device;
    this->physicalDevice = physicalDevice;
    createShadowMap(physicalDevice);
    createShadowRenderPass();
    createShadowDescriptorLayout();
    createShadowSampler();
    createShadowSamplerDescriptorLayout();
    createShadowPipeline(shaderDir);
    createShadowFramebuffer();
    createShadowDescriptorSets(physicalDevice, imageCount);
    createShadowSamplerDescriptorSets(imageCount, shadowMapImageView);
}

// ============================================================================
// createShadowMap - 创建阴影贴图（深度纹理）
// ============================================================================
// 阴影贴图是一个 2048x2048 的深度纹理，格式为 VK_FORMAT_D32_SFLOAT（32位浮点深度）。
// 它从光源视角记录场景中每个像素的深度值。
//
// 关键设置：
//   - usage: VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
//     DEPTH_STENCIL_ATTACHMENT: 在阴影 pass 中作为深度附件写入
//     SAMPLED: 在主 pass 的片段着色器中被采样读取
//   - tiling: VK_IMAGE_TILING_OPTIMAL（GPU 最优布局）
//   - samples: VK_SAMPLE_COUNT_1_BIT（无多重采样，阴影贴图通常不需要 MSAA）
//   - initialLayout: VK_IMAGE_LAYOUT_UNDEFINED（初始布局未定义，首次使用前会转换）
//
// 内存类型选择：VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT（设备本地内存，GPU 访问最快）
void ShadowSystem::createShadowMap(VkPhysicalDevice physicalDevice) {
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;         // 2D 图像
    ii.format = VK_FORMAT_D32_SFLOAT;        // 32位浮点深度格式
    ii.extent = {MAP_SIZE, MAP_SIZE, 1};     // 尺寸 2048x2048x1
    ii.mipLevels = 1;                        // 无 mipmap
    ii.arrayLayers = 1;                      // 单层
    ii.samples = VK_SAMPLE_COUNT_1_BIT;      // 无多重采样
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;     // GPU 最优平铺
    // 用法：既作为深度附件写入（阴影 pass），又作为采样纹理读取（主 pass）
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;  // 独占访问（不跨队列共享）
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // 初始布局未定义

    if (vkCreateImage(device, &ii, nullptr, &shadowMapImage) != VK_SUCCESS)
        throw std::runtime_error("shadow map image creation failed");

    // 查询内存需求并分配设备本地内存
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device, shadowMapImage, &mr);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &mp);
    uint32_t ti = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((mr.memoryTypeBits & (1 << i)) &&
            (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            ti = i;
            break;
        }
    }

    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = ti;
    if (vkAllocateMemory(device, &ai, nullptr, &shadowMapMemory) != VK_SUCCESS ||
        vkBindImageMemory(device, shadowMapImage, shadowMapMemory, 0) != VK_SUCCESS)
        throw std::runtime_error("shadow map memory setup failed");

    // 创建图像视图（ImageView），用于在着色器中访问图像
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = shadowMapImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_D32_SFLOAT;
    // 只访问深度方面（aspect），不包含模板
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &vi, nullptr, &shadowMapImageView) != VK_SUCCESS)
        throw std::runtime_error("shadow map view creation failed");
}

// ============================================================================
// createShadowRenderPass - 创建阴影渲染通道（深度-only）
// ============================================================================
// 阴影 pass 只渲染深度信息，不输出颜色。它的唯一目的是生成阴影贴图。
//
// 关键设置：
//   - Attachment: VK_FORMAT_D32_SFLOAT，加载操作为 CLEAR（清除到 1.0 = 最远深度），
//     存储操作为 STORE（渲染完成后保存深度值到纹理）
//   - Subpass: 只有一个子 pass，使用 depth/stencil attachment
//   - Dependency: 确保在阴影 pass 开始之前，外部阶段对深度附件的写入已完成
//     srcStageMask = EARLY/LATE_FRAGMENT_TESTS（深度测试阶段）
//     dstStageMask = EARLY_FRAGMENT_TESTS（当前 pass 的深度测试）
//     srcAccessMask = DEPTH_STENCIL_ATTACHMENT_WRITE（允许写入深度）
//     dstAccessMask = READ | WRITE（允许读写深度）
//   - finalLayout: VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
//     渲染完成后转换为只读布局，供主 pass 采样
void ShadowSystem::createShadowRenderPass() {
    VkAttachmentDescription att{};
    att.format = VK_FORMAT_D32_SFLOAT;       // 深度格式
    att.samples = VK_SAMPLE_COUNT_1_BIT;     // 无多重采样
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;    // 清除深度到 1.0
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // 保存深度值到纹理
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;   // 不使用模板
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;         // 初始布局未定义
    att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;  // 转为只读供主 pass 采样

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0;                 // 附件索引 0
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;  // 渲染时的布局

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.pDepthStencilAttachment = &depthRef;

    // Subpass dependency：确保阴影贴图的写入同步
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;    // 外部阶段
    dep.dstSubpass = 0;                      // 当前子 pass
    dep.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1;
    rpi.pAttachments = &att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    rpi.dependencyCount = 1;
    rpi.pDependencies = &dep;

    if (vkCreateRenderPass(device, &rpi, nullptr, &renderPass) != VK_SUCCESS)
        throw std::runtime_error("shadow render pass creation failed");
}

// ============================================================================
// createShadowDescriptorLayout - 创建阴影 UBO descriptor 布局
// ============================================================================
// 阴影顶点着色器需要一个 Uniform Buffer Object（UBO）来传递 lightSpaceMatrix 和光源位置。
// 这个布局描述了 UBO 如何绑定到着色器。
//
// 关键设置：
//   - binding = 0: UBO 绑定到着色器的 binding 0
//   - descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
//   - stageFlags = VK_SHADER_STAGE_VERTEX_BIT: 只在顶点着色器中使用
void ShadowSystem::createShadowDescriptorLayout() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1;
    li.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(device, &li, nullptr, &dslShadow) != VK_SUCCESS)
        throw std::runtime_error("shadow descriptor layout creation failed");
}

// ============================================================================
// createShadowSampler - 创建阴影采样器
// ============================================================================
// 采样器控制如何在着色器中采样纹理。对于阴影贴图，我们需要精确的深度比较。
//
// 关键设置：
//   - magFilter/minFilter = VK_FILTER_LINEAR: 线性插值，产生柔和的阴影边缘
//   - addressMode = CLAMP_TO_EDGE: 超出 UV 范围时钳制到边缘
//   - compareEnable = VK_FALSE: MoltenVK（macOS）不支持比较采样器，使用普通采样
//     在支持的平台（Windows/Linux）上应设为 VK_TRUE 以实现硬件深度比较
//   - compareOp = VK_COMPARE_OP_ALWAYS: 当 compareEnable 为 false 时无效
//
// 注意：MoltenVK 是 macOS 上的 Vulkan 实现，它将 Vulkan 转换为 Metal。
// Metal 不支持 Vulkan 的比较采样器功能，因此这里禁用。
void ShadowSystem::createShadowSampler() {
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;         // 放大时线性插值
    si.minFilter = VK_FILTER_LINEAR;         // 缩小时线性插值
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.anisotropyEnable = VK_FALSE;
    // MoltenVK 不支持比较采样器，使用普通采样
    si.compareEnable = VK_FALSE;
    si.compareOp = VK_COMPARE_OP_ALWAYS;

    if (vkCreateSampler(device, &si, nullptr, &shadowSampler) != VK_SUCCESS)
        throw std::runtime_error("shadow sampler creation failed");
}

// ============================================================================
// createShadowSamplerDescriptorLayout - 创建采样器 descriptor 布局
// ============================================================================
// 主渲染 pass 的片段着色器需要采样阴影贴图来判断片段是否在阴影中。
// 这个布局描述了一个 COMBINED_IMAGE_SAMPLER（组合图像采样器）descriptor。
//
// 关键设置：
//   - binding = 0: 绑定到着色器的 binding 0
//   - descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: 同时包含图像和采样器
//   - stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT: 在片段着色器中使用
void ShadowSystem::createShadowSamplerDescriptorLayout() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1;
    li.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(device, &li, nullptr, &dslShadowSampler) != VK_SUCCESS)
        throw std::runtime_error("shadow sampler descriptor layout creation failed");
}

// ============================================================================
// createShadowPipeline - 创建阴影管线
// ============================================================================
// 阴影管线负责从光源视角渲染场景，生成阴影贴图。它是一个深度-only 管线，不输出颜色。
//
// 关键设置：
//   - Vertex Input: 只需要位置属性（location 0），因为阴影计算不需要法线/UV
//   - Cull Mode: VK_CULL_MODE_FRONT_BIT - 剔除正面三角形
//     原因：减少阴影痤疮（shadow acne）。背面更容易产生自遮挡伪影，剔除正面可以
//     避免背面的深度值与正面的深度值过于接近导致的精度问题
//   - Depth Bias: 启用深度偏移来进一步消除阴影痤疮
//     depthBiasConstantFactor = 1.25f: 固定偏移量，给所有深度值加上一个常数
//     depthBiasSlopeFactor = 1.75f: 基于表面斜率的偏移量，斜面偏移更多
//     depthBiasClamp = 0.0f: 不限制最大偏移量
//     原理：给渲染到阴影贴图的深度值添加微小偏移，使其不与几何体表面完全重合，
//     从而避免由于浮点精度限制导致的自遮挡判断错误
//   - Depth Test: VK_COMPARE_OP_LESS - 深度值小于缓冲中的值时通过（标准深度测试）
//   - Color Blend: attachmentCount = 0 - 没有颜色附件（深度-only 渲染）
//   - Dynamic State: VIEWPORT 和 SCISSOR 是动态状态，在录制命令时设置
void ShadowSystem::createShadowPipeline(const std::string& shaderDir) {
    // 读取并编译顶点着色器
    std::string vsPath = shaderDir + "/shaders/shadow.vert.spv";
    auto vsCode = readFile(vsPath);
    VkShaderModule vs = createShaderModule(device, vsCode);

    VkPipelineShaderStageCreateInfo vsInfo{};
    vsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vsInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vsInfo.module = vs;
    vsInfo.pName = "main";

    // 顶点输入：只需要位置属性
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkVertexInputBindingDescription bd{};
    bd.binding = 0;
    bd.stride = sizeof(Vertex);              // 顶点步长
    bd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bd;
    VkVertexInputAttributeDescription attr{};
    attr.location = 0;                       // 着色器中的 location 0
    attr.binding = 0;
    attr.format = VK_FORMAT_R32G32B32_SFLOAT;  // vec3 位置
    attr.offset = offsetof(Vertex, pos);     // 位置属性偏移量
    vi.vertexAttributeDescriptionCount = 1;
    vi.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.scissorCount = 1;

    // 光栅化状态：关键设置 - 深度偏移和正面剔除
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_FRONT_BIT;    // 剔除正面三角形，减少阴影痤疮
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    rs.depthBiasEnable = VK_TRUE;            // 启用深度偏移
    rs.depthBiasConstantFactor = 1.25f;      // 固定偏移量
    rs.depthBiasClamp = 0.0f;                // 无最大偏移限制
    rs.depthBiasSlopeFactor = 1.75f;         // 斜率因子，斜面偏移更多

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 深度测试状态
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;            // 启用深度测试
    ds.depthWriteEnable = VK_TRUE;           // 启用深度写入（生成阴影贴图）
    ds.depthCompareOp = VK_COMPARE_OP_LESS;  // 标准深度测试：新深度 < 旧深度时通过

    // 颜色混合状态：无颜色附件（深度-only 渲染）
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 0;

    // 动态状态：viewport 和 scissor 在命令缓冲中设置
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
    dynamicState.pDynamicStates = dynamicStates.data();

    // 管线布局：使用阴影 UBO descriptor 布局
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &dslShadow;
    if (vkCreatePipelineLayout(device, &pli, nullptr, &pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("shadow pipeline layout creation failed");

    // 创建图形管线
    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount = 1;
    gpi.pStages = &vsInfo;
    gpi.pVertexInputState = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vpState;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms;
    gpi.pDepthStencilState = &ds;
    gpi.pColorBlendState = &cb;
    gpi.pDynamicState = &dynamicState;
    gpi.layout = pipelineLayout;
    gpi.renderPass = renderPass;
    gpi.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpi, nullptr, &pipeline) != VK_SUCCESS)
        throw std::runtime_error("shadow graphics pipeline creation failed");

    vkDestroyShaderModule(device, vs, nullptr);
}

// ============================================================================
// createShadowFramebuffer - 创建阴影帧缓冲
// ============================================================================
// 帧缓冲将渲染 pass 的附件与实际图像关联起来。
// 阴影帧缓冲只包含一个附件：shadowMapImageView（深度纹理）。
// 尺寸必须与阴影贴图分辨率匹配（2048x2048）。
void ShadowSystem::createShadowFramebuffer() {
    VkFramebufferCreateInfo fbi{};
    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass = renderPass;
    fbi.attachmentCount = 1;
    fbi.pAttachments = &shadowMapImageView;
    fbi.width = MAP_SIZE;                  // 2048
    fbi.height = MAP_SIZE;                 // 2048
    fbi.layers = 1;

    if (vkCreateFramebuffer(device, &fbi, nullptr, &framebuffer) != VK_SUCCESS)
        throw std::runtime_error("shadow framebuffer creation failed");
}

// ============================================================================
// createShadowDescriptorSets - 创建阴影 UBO descriptor sets
// ============================================================================
// 为每一帧分配一个 descriptor set，每个 set 包含一个 UBO binding。
// UBO 存储 lightSpaceMatrix（proj * view）和光源位置，每帧更新。
//
// 流程：
//   1. 创建 descriptor pool（包含 imageCount 个 UNIFORM_BUFFER descriptor）
//   2. 分配 descriptor sets（每帧一个）
//   3. 为每帧创建 UBO buffer（HOST_VISIBLE | HOST_COHERENT，CPU 可直接写入）
//   4. 将 UBO buffer 绑定到 descriptor set 的 binding 0
void ShadowSystem::createShadowDescriptorSets(VkPhysicalDevice physicalDevice, uint32_t imageCount) {
    // 创建 descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = imageCount;

    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &poolSize;
    pi.maxSets = imageCount;

    VkDescriptorPool pool;
    if (vkCreateDescriptorPool(device, &pi, nullptr, &pool) != VK_SUCCESS)
        throw std::runtime_error("shadow descriptor pool creation failed");

    // 分配 descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(imageCount, dslShadow);
    descSetsShadow.resize(imageCount);

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool;
    ai.descriptorSetCount = imageCount;
    ai.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &ai, descSetsShadow.data()) != VK_SUCCESS)
        throw std::runtime_error("shadow descriptor set allocation failed");

    // 为每帧创建 UBO 并绑定
    uboShadowBuf.resize(imageCount);
    uboShadowMem.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        createBuffer(device, physicalDevice, sizeof(UBO_Shadow),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    uboShadowBuf[i], uboShadowMem[i]);

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uboShadowBuf[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UBO_Shadow);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descSetsShadow[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
}

// ============================================================================
// createShadowSamplerDescriptorSets - 创建采样器 descriptor sets
// ============================================================================
// 为每一帧分配一个 descriptor set，每个 set 包含一个 COMBINED_IMAGE_SAMPLER。
// 这使得主渲染 pass 的片段着色器可以采样阴影贴图。
//
// 关键设置：
//   - imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
//     阴影贴图在采样时必须处于此布局（由 render pass 的 finalLayout 保证）
//   - imageView = shadowMapView: 阴影贴图视图
//   - sampler = shadowSampler: 采样器对象
void ShadowSystem::createShadowSamplerDescriptorSets(uint32_t imageCount, VkImageView shadowMapView) {
    // 创建 descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = imageCount;

    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &poolSize;
    pi.maxSets = imageCount;

    VkDescriptorPool pool;
    if (vkCreateDescriptorPool(device, &pi, nullptr, &pool) != VK_SUCCESS)
        throw std::runtime_error("shadow sampler descriptor pool creation failed");

    // 分配 descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(imageCount, dslShadowSampler);
    descSetsShadowSampler.resize(imageCount);

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool;
    ai.descriptorSetCount = imageCount;
    ai.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &ai, descSetsShadowSampler.data()) != VK_SUCCESS)
        throw std::runtime_error("shadow sampler descriptor set allocation failed");

    // 绑定图像和采样器
    for (uint32_t i = 0; i < imageCount; ++i) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;  // 必须是只读布局
        imageInfo.imageView = shadowMapView;
        imageInfo.sampler = shadowSampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descSetsShadowSampler[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
}

// ============================================================================
// updateShadowUBO - 更新阴影 UBO
// ============================================================================
// 计算光源的 view 和 projection 矩阵，构建 lightSpaceMatrix，
// 并将其写入对应帧的 UBO。
//
// 光源设置：
//   - 位置: (10, 10, 10)，场景右上角
//   - 目标: (0, 0, 0)，场景原点
//   - 上方向: (0, 1, 0)，Y 轴向上
//   - Projection: 正交投影（模拟平行光），范围 [-8, 8]，近裁剪面 0.1，远裁剪面 50
//
// lightSpaceMatrix = proj * view（转置后传给着色器，因为 GLSL 默认列主序）
void ShadowSystem::updateShadowUBO(uint32_t imageIndex, const Mat4& model) {
    Vec3 lightPos{10, 10, 10};
    Vec3 lightTarget{0, 0, 0};
    Vec3 lightUp{0, 1, 0};

    lightView = Mat4::lookAt(lightPos, lightTarget, lightUp);
    lightProj = Mat4::ortho(-8, 8, -8, 8, 0.1f, 50.0f);

    UBO_Shadow ubo{};
    ubo.lightSpaceMatrix = (lightProj * lightView).transposed();  // 转置以适应 GLSL 列主序
    ubo.lightPos = lightPos;

    // 映射内存并写入 UBO 数据
    void* data;
    vkMapMemory(device, uboShadowMem[imageIndex], 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(device, uboShadowMem[imageIndex]);
}

// ============================================================================
// cleanup - 清理所有 Vulkan 资源
// ============================================================================
// 按依赖逆序销毁资源：先销毁依赖于其他资源的对象，再销毁被依赖的资源。
// 例如：先销毁 pipeline（依赖 layout），再销毁 layout；
//      先销毁 ImageView，再销毁 Image。
void ShadowSystem::cleanup() {
    for (auto buf : uboShadowBuf) {
        if (buf) vkDestroyBuffer(device, buf, nullptr);
    }
    for (auto mem : uboShadowMem) {
        if (mem) vkFreeMemory(device, mem, nullptr);
    }

    if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
    if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
    if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
    if (shadowSampler) vkDestroySampler(device, shadowSampler, nullptr);
    if (shadowMapImageView) vkDestroyImageView(device, shadowMapImageView, nullptr);
    if (shadowMapImage) vkDestroyImage(device, shadowMapImage, nullptr);
    if (shadowMapMemory) vkFreeMemory(device, shadowMapMemory, nullptr);
    if (dslShadow) vkDestroyDescriptorSetLayout(device, dslShadow, nullptr);
    if (dslShadowSampler) vkDestroyDescriptorSetLayout(device, dslShadowSampler, nullptr);
}

// ============================================================================
// recordShadowPass - 录制阴影 pass 命令
// ============================================================================
// 将阴影 pass 的所有绘制命令编码到 command buffer。
// 这是在 main loop 中每帧调用的函数。
//
// 流程：
//   1. Reset command buffer（重置到初始状态）
//   2. Begin command buffer（开始录制）
//   3. Begin render pass（绑定帧缓冲，清除深度为 1.0）
//   4. Bind pipeline（绑定阴影管线）
//   5. Set viewport/scissor（设置为阴影贴图分辨率 2048x2048）
//   6. Bind descriptor sets（绑定 UBO，包含 lightSpaceMatrix）
//   7. Bind vertex/index buffers（绑定球体的几何数据）
//   8. Draw indexed（绘制球体，只有能投射阴影的物体才需要绘制）
//   9. End render pass
//  10. End command buffer
//
// 参数：
//   cmd: 要录制的命令缓冲
//   frameIdx: 当前帧索引，用于选择对应的 descriptor set
//   sphereVbo/sphereIbo: 球体顶点和索引缓冲
//   sphereIndexCount: 球体索引数量
void ShadowSystem::recordShadowPass(VkCommandBuffer cmd, uint32_t frameIdx,
                                     VkBuffer sphereVbo, VkBuffer sphereIbo, uint32_t sphereIndexCount) const {
    vkResetCommandBuffer(cmd, 0);  // 重置命令缓冲

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;  // 一次性提交
    vkBeginCommandBuffer(cmd, &bi);

    // 清除深度缓冲到 1.0（最远深度）
    VkClearValue clearValue{};
    clearValue.depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = renderPass;
    rpi.framebuffer = framebuffer;
    rpi.renderArea.offset = {0, 0};
    rpi.renderArea.extent = {MAP_SIZE, MAP_SIZE};  // 2048x2048
    rpi.clearValueCount = 1;
    rpi.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // 设置视口和裁剪区域为阴影贴图分辨率
    VkViewport vp{0, 0, (float)MAP_SIZE, (float)MAP_SIZE, 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, {MAP_SIZE, MAP_SIZE}};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // 绑定 UBO descriptor set（包含 lightSpaceMatrix）
    VkDescriptorSet descSets[] = {descSetsShadow[frameIdx]};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 0, 1, descSets, 0, nullptr);

    // 绑定球体顶点和索引缓冲
    VkBuffer vbos[] = {sphereVbo};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbos, offsets);
    vkCmdBindIndexBuffer(cmd, sphereIbo, 0, VK_INDEX_TYPE_UINT32);
    // 绘制球体（只有需要投射阴影的物体才在这里绘制）
    vkCmdDrawIndexed(cmd, sphereIndexCount, 1, 0, 0, 0);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}
