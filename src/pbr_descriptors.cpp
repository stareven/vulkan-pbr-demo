#include "pbr_app.h"
#include "vulkan_utils.h"
#include "mesh.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

void PBRApp::createDescriptorPool() {
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)(MAX_FRAMES_IN_FLIGHT * 3)},  // MVP + Material + Shadow
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)imageCount}  // Shadow map
    };
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = MAX_FRAMES_IN_FLIGHT * 3 + (uint32_t)imageCount;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device, &dpi, nullptr, &descPool) != VK_SUCCESS)
        throw std::runtime_error("descriptor pool failed");
}

void PBRApp::createDescriptorSets() {
    descSetsMVP.resize(MAX_FRAMES_IN_FLIGHT);
    descSetsMat.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = descPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &dslMVP;
        if (vkAllocateDescriptorSets(device, &ai, &descSetsMVP[i]) != VK_SUCCESS)
            throw std::runtime_error("alloc mvp desc set failed");

        VkDescriptorBufferInfo bi{};
        bi.buffer = uboMVPBuf[i];
        bi.range = sizeof(UBO_MVP);
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = descSetsMVP[i];
        w.dstBinding = 0;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.descriptorCount = 1;
        w.pBufferInfo = &bi;
        vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);

        ai.pSetLayouts = &dslMat;
        if (vkAllocateDescriptorSets(device, &ai, &descSetsMat[i]) != VK_SUCCESS)
            throw std::runtime_error("alloc mat desc set failed");

        bi.buffer = uboMatBuf[i];
        bi.range = sizeof(UBO_Material);
        w.dstSet = descSetsMat[i];
        w.pBufferInfo = &bi;
        vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    }
}

// ============================================================================
// Command buffers
// ============================================================================
