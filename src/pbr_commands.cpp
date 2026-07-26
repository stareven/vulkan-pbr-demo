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

void PBRApp::createCommandBuffers() {
    cmdBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = (uint32_t)cmdBuffers.size();
    if (vkAllocateCommandBuffers(device, &ai, cmdBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("command buffer alloc failed");

    // 创建 Shadow 专用 command buffer
    ai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &ai, &shadowCmdBuffer) != VK_SUCCESS)
        throw std::runtime_error("shadow command buffer alloc failed");
}

void PBRApp::recordCommandBuffer(uint32_t imgIdx) {
    VkCommandBuffer cmd = cmdBuffers[frameIdx];
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
    rpi.renderArea.extent = scExtent;
    rpi.clearValueCount = 2;
    rpi.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // 绑定 descriptor sets (set 0 = MVP, set 1 = Material, set 2 = Shadow Sampler)
    uint32_t shadowSamplerIdx = imgIdx % imageCount;
    VkDescriptorSet descSets[] = {descSetsMVP[frameIdx], descSetsMat[frameIdx], descSetsShadowSampler[shadowSamplerIdx]};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            0, 3, descSets, 0, nullptr);

    VkViewport vp{0, 0, (float)scExtent.width, (float)scExtent.height, 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, scExtent};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // 画球体
    VkBuffer vertexBuffers[] = {vbo};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, ibo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);

    // 画地面（接收阴影，复用同一材质）
    VkBuffer planeBuffers[] = {planeVbo};
    vkCmdBindVertexBuffers(cmd, 0, 1, planeBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, planeIbo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, planeIndexCount, 1, 0, 0, 0);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

// ============================================================================
// Sync
// ============================================================================
