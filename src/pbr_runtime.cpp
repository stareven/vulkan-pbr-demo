#include "pbr_app.h"
#include "vulkan_utils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>

// ============================================================================
// Input
// ============================================================================
void PBRApp::handleInput(float dt) {
    float speed = 5.0f * dt;
    float cx = std::cos(camPitch) * std::sin(camYaw);
    float cy = std::sin(camPitch);
    float cz = std::cos(camPitch) * std::cos(camYaw);
    Vec3 fwd{cx, cy, cz};
    Vec3 right = fwd.cross({0, 1, 0}).normalize();

    GLFWwindow* h = window.getHandle();
    if (glfwGetKey(h, GLFW_KEY_W) == GLFW_PRESS) camPos = camPos + fwd * speed;
    if (glfwGetKey(h, GLFW_KEY_S) == GLFW_PRESS) camPos = camPos - fwd * speed;
    if (glfwGetKey(h, GLFW_KEY_A) == GLFW_PRESS) camPos = camPos - right * speed;
    if (glfwGetKey(h, GLFW_KEY_D) == GLFW_PRESS) camPos = camPos + right * speed;
    if (glfwGetKey(h, GLFW_KEY_Q) == GLFW_PRESS) camPos.y -= speed;
    if (glfwGetKey(h, GLFW_KEY_E) == GLFW_PRESS) camPos.y += speed;

    // 材质切换 (M)
    static bool mLast = false;
    bool mNow = glfwGetKey(h, GLFW_KEY_M) == GLFW_PRESS;
    if (mNow && !mLast) {
        materialSystem.nextPreset();
        std::cout << "Material preset: " << materialSystem.getPreset() << "\n";
    }
    mLast = mNow;

    // 玻璃 (G)
    static bool gLast = false;
    bool gNow = glfwGetKey(h, GLFW_KEY_G) == GLFW_PRESS;
    if (gNow && !gLast) {
        materialSystem.toggleGlass();
        std::cout << "Glass: " << (materialSystem.isGlassEnabled() ? "ON" : "OFF") << "\n";
    }
    gLast = gNow;

    // 自发光 (F)
    static bool eLast = false;
    bool eNow = glfwGetKey(h, GLFW_KEY_F) == GLFW_PRESS;
    if (eNow && !eLast) {
        materialSystem.toggleEmissive();
        std::cout << "Emissive: " << (materialSystem.isEmissiveEnabled() ? "ON" : "OFF") << "\n";
    }
    eLast = eNow;
}

// ============================================================================
// Main loop
// ============================================================================
void PBRApp::mainLoop() {
    while (!window.shouldClose()) {
        window.pollEvents();
        handleInput(0.016f);
        drawFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    vkDeviceWaitIdle(ctx.device);
}

// ============================================================================
// Frame rendering
// ============================================================================
void PBRApp::drawFrame() {
    syncManager.waitForFence(ctx.device);

    uint32_t imgIdx;
    VkResult r = vkAcquireNextImageKHR(ctx.device, swapchain.getSwapchain(), UINT64_MAX,
        syncManager.getImageAvailableSemaphore(), VK_NULL_HANDLE, &imgIdx);

    if (r == VK_ERROR_OUT_OF_DATE_KHR || window.isFramebufferResized()) {
        window.resetFramebufferResized();
        recreateSwapchain();
        return;
    }

    syncManager.markImageInFlight(imgIdx);
    uint32_t frameIdx = syncManager.getCurrentFrame();

    // 计算相机 / 投影矩阵
    float cx = std::cos(camPitch) * std::sin(camYaw);
    float cy = std::sin(camPitch);
    float cz = std::cos(camPitch) * std::cos(camYaw);
    Vec3 camDir{cx, cy, cz};
    Vec3 target = camPos + camDir;
    Mat4 view = Mat4::lookAt(camPos, target, {0, 1, 0});
    float aspect = (float)swapchain.getExtent().width / (float)swapchain.getExtent().height;
    Mat4 proj = Mat4::perspective(45.0f * static_cast<float>(M_PI) / 180.0f, aspect, 0.1f, 100.0f);
    Mat4 model = Mat4::identity();

    // Shadow UBO 必须先更新（内部会计算 lightView / lightProj）
    // 注意：shadow UBO 数量 = MAX_FRAMES_IN_FLIGHT，用 frameIdx 索引
    shadowSystem.updateShadowUBO(ctx.device, frameIdx, model);

    // 光源空间矩阵（用于主 pass 的 MVP UBO，以便 shader 做阴影坐标变换）
    Mat4 lightSpaceMatrix = shadowSystem.getLightProj() * shadowSystem.getLightView();

    // 更新主 pass UBOs
    meshManager.updateUniformBuffers(ctx.device, frameIdx,
        model, view, proj, lightSpaceMatrix, camPos,
        materialSystem.getPreset(),
        materialSystem.isGlassEnabled(),
        materialSystem.isEmissiveEnabled());

    // descriptor set 已经在 init 时绑定到每帧 UBO，无需每帧更新

    // 1) Shadow pass — 独立提交并等待完成（主 pass 需要采样 shadow map）
    recordShadowCommandBuffer();
    VkSubmitInfo shadowSubmit{};
    shadowSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    shadowSubmit.commandBufferCount = 1;
    VkCommandBuffer shadowCmd = cmdManager.getShadowCommandBuffer();
    shadowSubmit.pCommandBuffers = &shadowCmd;
    vkQueueSubmit(ctx.graphicsQueue, 1, &shadowSubmit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphicsQueue);

    // 2) 主 pass
    recordCommandBuffer(imgIdx);

    syncManager.resetFence(ctx.device);

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore wait[] = {syncManager.getImageAvailableSemaphore()};
    VkSemaphore sig[]  = {syncManager.getRenderFinishedSemaphore()};

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = wait;
    si.pWaitDstStageMask = waitStages;
    si.commandBufferCount = 1;
    VkCommandBuffer mainCmd = cmdManager.getBuffer(frameIdx);
    si.pCommandBuffers = &mainCmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = sig;

    if (vkQueueSubmit(ctx.graphicsQueue, 1, &si, syncManager.getInFlightFence()) != VK_SUCCESS)
        throw std::runtime_error("queue submit failed");

    VkSwapchainKHR sc = swapchain.getSwapchain();
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = sig;
    pi.swapchainCount = 1;
    pi.pSwapchains = &sc;
    pi.pImageIndices = &imgIdx;
    r = vkQueuePresentKHR(ctx.presentQueue, &pi);

    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || window.isFramebufferResized()) {
        window.resetFramebufferResized();
        recreateSwapchain();
    }

    syncManager.advanceFrame();
}

// ============================================================================
// Shadow command buffer
// ============================================================================
void PBRApp::recordShadowCommandBuffer() {
    VkCommandBuffer cmd = cmdManager.getShadowCommandBuffer();
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkClearValue clearValue{};
    clearValue.depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = shadowSystem.getRenderPass();
    rpi.framebuffer = shadowSystem.getFramebuffer();
    rpi.renderArea.offset = {0, 0};
    rpi.renderArea.extent = {2048, 2048};
    rpi.clearValueCount = 1;
    rpi.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowSystem.getPipeline());

    // 动态 viewport/scissor（shadow pipeline 已声明为 dynamic）
    VkViewport vp{0, 0, 2048.0f, 2048.0f, 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, {2048, 2048}};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    uint32_t frameIdx = syncManager.getCurrentFrame();
    VkDescriptorSet descSets[] = {shadowSystem.getShadowSet(frameIdx)};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        shadowSystem.getPipelineLayout(), 0, 1, descSets, 0, nullptr);

    // 球体
    VkBuffer vbos[] = {meshManager.getSphereVBO()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbos, offsets);
    vkCmdBindIndexBuffer(cmd, meshManager.getSphereIBO(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, meshManager.getSphereIndexCount(), 1, 0, 0, 0);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

// ============================================================================
// Main command buffer
// ============================================================================
void PBRApp::recordCommandBuffer(uint32_t imgIdx) {
    uint32_t frameIdx = syncManager.getCurrentFrame();
    VkCommandBuffer cmd = cmdManager.getBuffer(frameIdx);
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    VkClearValue clears[2];
    clears[0].color = {{0.02f, 0.02f, 0.05f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = renderPipeline.getRenderPass();
    rpi.framebuffer = renderPipeline.getFramebuffers()[imgIdx];
    rpi.renderArea.extent = swapchain.getExtent();
    rpi.clearValueCount = 2;
    rpi.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderPipeline.getPipeline());

    // set 0 = MVP, set 1 = Material, set 2 = Shadow Sampler
    VkDescriptorSet descSets[] = {
        descManager.getMVPSet(frameIdx),
        descManager.getMaterialSet(frameIdx),
        shadowSystem.getSamplerSet(imgIdx),
    };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        renderPipeline.getPipelineLayout(), 0, 3, descSets, 0, nullptr);

    VkExtent2D ext = swapchain.getExtent();
    VkViewport vp{0, 0, (float)ext.width, (float)ext.height, 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, ext};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // 球体
    VkBuffer vbos[] = {meshManager.getSphereVBO()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbos, offsets);
    vkCmdBindIndexBuffer(cmd, meshManager.getSphereIBO(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, meshManager.getSphereIndexCount(), 1, 0, 0, 0);

    // 地面（接收阴影，复用同一材质）
    VkBuffer planeVbos[] = {meshManager.getPlaneVBO()};
    vkCmdBindVertexBuffers(cmd, 0, 1, planeVbos, offsets);
    vkCmdBindIndexBuffer(cmd, meshManager.getPlaneIBO(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, meshManager.getPlaneIndexCount(), 1, 0, 0, 0);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

// ============================================================================
// Swapchain recreation
// ============================================================================
void PBRApp::recreateSwapchain() {
    vkDeviceWaitIdle(ctx.device);

    // 销毁依赖 swapchain extent 的资源
    renderPipeline.cleanup(ctx.device);
    swapchain.recreate(ctx.device, ctx.physicalDevice, ctx.surface, window.getHandle());

    // 重建依赖 swapchain 的资源
    renderPipeline.createRenderPass(ctx.device, swapchain.getFormat());
    renderPipeline.createPipelineLayout(ctx.device,
        descManager.getMVPLayout(),
        descManager.getMaterialLayout(),
        shadowSystem.getSamplerLayout());
    renderPipeline.createGraphicsPipeline(ctx.device, swapchain.getExtent(), shaderDir);
    renderPipeline.createFramebuffers(ctx.device, swapchain.getExtent(),
        swapchain.getImageViews(), swapchain.getDepthImageView());
}
