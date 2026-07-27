#include "render_pipeline.h"
#include "vulkan_utils.h"
#include "types.h"

#include <array>
#include <stdexcept>

RenderPipeline::~RenderPipeline() {
    // Cleanup should be called explicitly
}

void RenderPipeline::createRenderPass(VkFormat swapchainFormat) {
    std::array<VkAttachmentDescription, 2> att{};
    // Color
    att[0].format = swapchainFormat;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    // Depth
    att[1].format = VK_FORMAT_D32_SFLOAT;
    att[1].samples = VK_SAMPLE_COUNT_1_BIT;
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = (uint32_t)att.size();
    rpi.pAttachments = att.data();
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    rpi.dependencyCount = 1;
    rpi.pDependencies = &dep;

    if (vkCreateRenderPass(device, &rpi, nullptr, &renderPass) != VK_SUCCESS)
        throw std::runtime_error("render pass creation failed");
}

void RenderPipeline::createPipelineLayout(VkDescriptorSetLayout mvpLayout,
                                         VkDescriptorSetLayout materialLayout,
                                         VkDescriptorSetLayout shadowSamplerLayout) {
    VkDescriptorSetLayout layouts[] = {mvpLayout, materialLayout, shadowSamplerLayout};
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 3;
    pli.pSetLayouts = layouts;
    if (vkCreatePipelineLayout(device, &pli, nullptr, &pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("pipeline layout creation failed");
}

void RenderPipeline::createGraphicsPipeline(VkExtent2D extent, const std::string& shaderDir) {
    std::string vsPath = shaderDir + "/shaders/shader.vert.spv";
    std::string fsPath = shaderDir + "/shaders/shader.frag.spv";

    auto vsCode = readFile(vsPath);
    auto fsCode = readFile(fsPath);
    VkShaderModule vs = createShaderModule(device, vsCode);
    VkShaderModule fs = createShaderModule(device, fsCode);

    VkPipelineShaderStageCreateInfo vsInfo{};
    vsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vsInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vsInfo.module = vs;
    vsInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fsInfo{};
    fsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fsInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fsInfo.module = fs;
    fsInfo.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vsInfo, fsInfo};

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkVertexInputBindingDescription bd{};
    bd.binding = 0;
    bd.stride = sizeof(Vertex);
    bd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bd;
    static std::array<VkVertexInputAttributeDescription, 3> attrs{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(Vertex, pos);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(Vertex, normal);
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = offsetof(Vertex, uv);
    vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vi.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cbAtt.blendEnable = VK_TRUE;
    cbAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cbAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbAtt.colorBlendOp = VK_BLEND_OP_ADD;
    cbAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cbAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cbAtt.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cbAtt;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount = 2;
    gpi.pStages = stages;
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
        throw std::runtime_error("graphics pipeline creation failed");

    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
}

void RenderPipeline::createFramebuffers(VkExtent2D extent,
                                       const std::vector<VkImageView>& swapchainViews,
                                       VkImageView depthView) {
    framebuffers.resize(swapchainViews.size());
    for (size_t i = 0; i < swapchainViews.size(); ++i) {
        std::array<VkImageView, 2> attachments = {swapchainViews[i], depthView};
        VkFramebufferCreateInfo fbi{};
        fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbi.renderPass = renderPass;
        fbi.attachmentCount = (uint32_t)attachments.size();
        fbi.pAttachments = attachments.data();
        fbi.width = extent.width;
        fbi.height = extent.height;
        fbi.layers = 1;
        if (vkCreateFramebuffer(device, &fbi, nullptr, &framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("framebuffer creation failed");
    }
}

void RenderPipeline::cleanup() {
    for (auto fb : framebuffers) {
        if (fb) vkDestroyFramebuffer(device, fb, nullptr);
    }
    framebuffers.clear();
    if (pipeline) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (renderPass) {
        vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
}

void RenderPipeline::recordMainPass(VkCommandBuffer cmd, uint32_t imgIdx, VkExtent2D extent,
                                    const std::vector<VkDescriptorSet>& descSets,
                                    VkBuffer sphereVbo, VkBuffer sphereIbo, uint32_t sphereIndexCount,
                                    VkBuffer planeVbo, VkBuffer planeIbo, uint32_t planeIndexCount) const {
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    VkClearValue clears[2];
    clears[0].color = {{0.02f, 0.02f, 0.05f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = renderPass;
    rpi.framebuffer = framebuffers[imgIdx];
    rpi.renderArea.extent = extent;
    rpi.clearValueCount = 2;
    rpi.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 0, (uint32_t)descSets.size(), descSets.data(), 0, nullptr);

    VkViewport vp{0, 0, (float)extent.width, (float)extent.height, 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // 球体
    VkBuffer vbos[] = {sphereVbo};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbos, offsets);
    vkCmdBindIndexBuffer(cmd, sphereIbo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, sphereIndexCount, 1, 0, 0, 0);

    // 地面
    VkBuffer planeVbos[] = {planeVbo};
    vkCmdBindVertexBuffers(cmd, 0, 1, planeVbos, offsets);
    vkCmdBindIndexBuffer(cmd, planeIbo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, planeIndexCount, 1, 0, 0, 0);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}