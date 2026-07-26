#include "shadow_system.h"
#include "vulkan_utils.h"

#include <filesystem>
#include <stdexcept>
#include <cstring>

ShadowSystem::~ShadowSystem() {
    // Cleanup should be called explicitly
}

void ShadowSystem::initialize(VkDevice device, VkPhysicalDevice physicalDevice) {
    createShadowMap(device, physicalDevice);
    createShadowRenderPass(device);
    createShadowDescriptorLayout(device);
    createShadowSampler(device);
    createShadowSamplerDescriptorLayout(device);
    createShadowPipeline(device, dslShadowSampler);
    createShadowFramebuffer(device);
}

void ShadowSystem::createShadowMap(VkDevice device, VkPhysicalDevice physicalDevice) {
    createShadowMapInternal(device, physicalDevice);
}

void ShadowSystem::createShadowRenderPass(VkDevice device) {
    createShadowRenderPassInternal(device);
}

void ShadowSystem::createShadowPipeline(VkDevice device, VkDescriptorSetLayout shadowSamplerLayout) {
    createShadowPipelineInternal(device, shadowSamplerLayout);
}

void ShadowSystem::createShadowFramebuffer(VkDevice device) {
    createShadowFramebufferInternal(device);
}

void ShadowSystem::createShadowDescriptorLayout(VkDevice device) {
    createShadowDescriptorLayoutInternal(device);
}

void ShadowSystem::createShadowDescriptorSets(VkDevice device, uint32_t imageCount) {
    // Similar to descriptor manager but for shadow
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

    std::vector<VkDescriptorSetLayout> layouts(imageCount, dslShadow);
    descSetsShadow.resize(imageCount);

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool;
    ai.descriptorSetCount = imageCount;
    ai.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &ai, descSetsShadow.data()) != VK_SUCCESS)
        throw std::runtime_error("shadow descriptor set allocation failed");

    // Create UBOs
    uboShadowBuf.resize(imageCount);
    uboShadowMem.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        createBuffer(device, VK_NULL_HANDLE, sizeof(UBO_Shadow),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    uboShadowBuf[i], uboShadowMem[i]);
    }
}

void ShadowSystem::createShadowSampler(VkDevice device) {
    createShadowSamplerInternal(device);
}

void ShadowSystem::createShadowSamplerDescriptorLayout(VkDevice device) {
    createShadowSamplerDescriptorLayoutInternal(device);
}

void ShadowSystem::createShadowSamplerDescriptorSets(VkDevice device, uint32_t imageCount, VkImageView shadowMapView) {
    // Create sampler descriptor sets
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

    std::vector<VkDescriptorSetLayout> layouts(imageCount, dslShadowSampler);
    descSetsShadowSampler.resize(imageCount);

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool;
    ai.descriptorSetCount = imageCount;
    ai.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &ai, descSetsShadowSampler.data()) != VK_SUCCESS)
        throw std::runtime_error("shadow sampler descriptor set allocation failed");

    // Update descriptor sets
    for (uint32_t i = 0; i < imageCount; ++i) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
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

void ShadowSystem::updateShadowUBO(uint32_t imageIndex, const Mat4& model) {
    // Calculate light matrices
    Vec3 lightPos{5, 10, 5};
    Vec3 lightTarget{0, 0, 0};
    Vec3 lightUp{0, 1, 0};

    lightView = Mat4::lookAt(lightPos, lightTarget, lightUp);
    lightProj = Mat4::perspective(45.0f, 1.0f, 0.1f, 50.0f);
    // perspective 已经处理了 Y 翻转

    UBO_Shadow ubo{};
    ubo.lightSpaceMatrix = lightProj * lightView;
    ubo.lightPos = lightPos;

    void* data;
    vkMapMemory(VK_NULL_HANDLE, uboShadowMem[imageIndex], 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(VK_NULL_HANDLE, uboShadowMem[imageIndex]);
}

void ShadowSystem::cleanup(VkDevice device) {
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

void ShadowSystem::createShadowMapInternal(VkDevice device, VkPhysicalDevice physicalDevice) {
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_D32_SFLOAT;
    ii.extent = {MAP_SIZE, MAP_SIZE, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &ii, nullptr, &shadowMapImage) != VK_SUCCESS)
        throw std::runtime_error("shadow map image creation failed");

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

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = shadowMapImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_D32_SFLOAT;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &vi, nullptr, &shadowMapImageView) != VK_SUCCESS)
        throw std::runtime_error("shadow map view creation failed");
}

void ShadowSystem::createShadowRenderPassInternal(VkDevice device) {
    VkAttachmentDescription att{};
    att.format = VK_FORMAT_D32_SFLOAT;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
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

void ShadowSystem::createShadowPipelineInternal(VkDevice device, VkDescriptorSetLayout shadowSamplerLayout) {
    std::filesystem::path exeDir = std::filesystem::current_path();
    std::string vsPath = exeDir.parent_path().string() + "/shaders/shadow.vert.spv";
    std::string fsPath = exeDir.parent_path().string() + "/shaders/shadow.frag.spv";

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
    std::array<VkVertexInputAttributeDescription, 3> attrs{};
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

    VkViewport vp{};
    vp.x = 0;
    vp.y = 0;
    vp.width = (float)MAP_SIZE;
    vp.height = (float)MAP_SIZE;
    vp.minDepth = 0;
    vp.maxDepth = 1;

    VkRect2D sc{};
    sc.offset = {0, 0};
    sc.extent = {MAP_SIZE, MAP_SIZE};

    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.pViewports = &vp;
    vpState.scissorCount = 1;
    vpState.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
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

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 0;

    // Pipeline layout with shadow sampler
    std::array<VkDescriptorSetLayout, 2> layouts = {dslShadow, shadowSamplerLayout};
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = (uint32_t)layouts.size();
    pli.pSetLayouts = layouts.data();
    if (vkCreatePipelineLayout(device, &pli, nullptr, &pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("shadow pipeline layout creation failed");

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
    gpi.layout = pipelineLayout;
    gpi.renderPass = renderPass;
    gpi.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpi, nullptr, &pipeline) != VK_SUCCESS)
        throw std::runtime_error("shadow graphics pipeline creation failed");

    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
}

void ShadowSystem::createShadowFramebufferInternal(VkDevice device) {
    VkFramebufferCreateInfo fbi{};
    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass = renderPass;
    fbi.attachmentCount = 1;
    fbi.pAttachments = &shadowMapImageView;
    fbi.width = MAP_SIZE;
    fbi.height = MAP_SIZE;
    fbi.layers = 1;

    if (vkCreateFramebuffer(device, &fbi, nullptr, &framebuffer) != VK_SUCCESS)
        throw std::runtime_error("shadow framebuffer creation failed");
}

void ShadowSystem::createShadowDescriptorLayoutInternal(VkDevice device) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1;
    li.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(device, &li, nullptr, &dslShadow) != VK_SUCCESS)
        throw std::runtime_error("shadow descriptor layout creation failed");
}

void ShadowSystem::createShadowSamplerInternal(VkDevice device) {
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.anisotropyEnable = VK_FALSE;
    si.compareEnable = VK_TRUE;
    si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    if (vkCreateSampler(device, &si, nullptr, &shadowSampler) != VK_SUCCESS)
        throw std::runtime_error("shadow sampler creation failed");
}

void ShadowSystem::createShadowSamplerDescriptorLayoutInternal(VkDevice device) {
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
