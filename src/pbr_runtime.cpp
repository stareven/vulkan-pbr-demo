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
#include <thread>

void PBRApp::updateUBOs() {
    float cx = std::cos(camPitch) * std::sin(camYaw);
    float cy = std::sin(camPitch);
    float cz = std::cos(camPitch) * std::cos(camYaw);
    Vec3 camDir{cx, cy, cz};
    Vec3 target = camPos + camDir;
    Vec3 up{0, 1, 0};

    Mat4 view = Mat4::lookAt(camPos, target, up);
    float aspect = (float)scExtent.width / scExtent.height;
    Mat4 proj = Mat4::perspective(45.0f * M_PI / 180.0f, aspect, 0.1f, 100.0f);
    Mat4 model = Mat4::identity();

    // 光源空间矩阵（主光源在 (10,10,10) 看向原点）
    Vec3 lightPos{10, 10, 10};
    lightView = Mat4::lookAt(lightPos, Vec3(0, 0, 0), Vec3(0, 1, 0));
    lightProj = Mat4::ortho(-8, 8, -8, 8, 0.1f, 50.0f);
    Mat4 lightSpaceMatrix = lightProj * lightView;

    // C++ 是 row-major，GLSL 是 column-major，需要转置
    UBO_MVP mvp{model.transposed(), view.transposed(), proj.transposed(),
                lightSpaceMatrix.transposed(), camPos, 0.0f};
    void* p;
    vkMapMemory(device, uboMVPMem[frameIdx], 0, sizeof(mvp), 0, &p);
    std::memcpy(p, &mvp, sizeof(mvp));
    vkUnmapMemory(device, uboMVPMem[frameIdx]);

    // 更新 Shadow UBO（光源空间矩阵，用于 shadow pass 顶点变换）
    UBO_Shadow shadowUBO{lightSpaceMatrix.transposed(), lightPos, 0.0f};
    vkMapMemory(device, uboShadowMem[frameIdx], 0, sizeof(shadowUBO), 0, &p);
    std::memcpy(p, &shadowUBO, sizeof(shadowUBO));
    vkUnmapMemory(device, uboShadowMem[frameIdx]);

    struct MatPreset {
        Vec3 albedo;
        float metallic;
        float roughness;
    };
    MatPreset presets[] = {
        {{0.95f, 0.35f, 0.10f}, 0.0f, 0.7f},   // 红色塑料
        {{0.95f, 0.93f, 0.88f}, 1.0f, 0.1f},   // 银
        {{1.00f, 0.77f, 0.33f}, 1.0f, 0.3f},   // 金
        {{0.97f, 0.96f, 0.91f}, 1.0f, 0.2f},   // 铝
        {{0.30f, 0.85f, 0.39f}, 0.0f, 0.4f},   // 绿色塑料
        {{0.50f, 0.50f, 0.50f}, 1.0f, 0.5f},   // 铁
        {{0.98f, 0.99f, 1.00f}, 0.0f, 0.05f},  // 玻璃
    };
    auto& pr = presets[matPreset % 7];

    UBO_Material mat{};
    mat.albedo = pr.albedo;
    mat.metallic = pr.metallic;
    mat.roughness = pr.roughness;
    mat.ao = 1.0f;
    mat.ior = 1.5f;
    mat.opacity = 1.0f;
    if ((matPreset % 7) == 6 && glassEnabled) {
        mat.ior = 1.52f;
        mat.opacity = 0.3f;
    }
    mat.cameraPos = camPos;
    mat.ambientLight = {0.03f, 0.03f, 0.03f};
    mat.lights[0] = {{10, 10, 10}, 0.0f, {300, 300, 300}, 1.0f};
    mat.lights[1] = {{-10, 10, 10}, 0.0f, {300, 100, 100}, 1.0f};
    mat.lights[2] = {{10, -10, -10}, 0.0f, {100, 300, 100}, 1.0f};
    mat.lights[3] = {{-10, -10, -10}, 0.0f, {100, 100, 300}, 1.0f};

    if (emissiveEnabled) {
        mat.emissive = {1.0f, 0.5f, 0.1f};
        mat.emissiveStrength = 2.0f;
    } else {
        mat.emissive = {0.0f, 0.0f, 0.0f};
        mat.emissiveStrength = 0.0f;
    }

    vkMapMemory(device, uboMatMem[frameIdx], 0, sizeof(mat), 0, &p);
    std::memcpy(p, &mat, sizeof(mat));
    vkUnmapMemory(device, uboMatMem[frameIdx]);
}

// ============================================================================
// Main loop / draw
// ============================================================================
void PBRApp::mainLoop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        float dt = 0.016f;
        float speed = 5.0f * dt;
        Vec3 fwd = (Vec3{std::cos(camPitch) * std::sin(camYaw), std::sin(camPitch),
                          std::cos(camPitch) * std::cos(camYaw)})
                        .normalize();
        Vec3 right = fwd.cross({0, 1, 0}).normalize();
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camPos = camPos + fwd * speed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camPos = camPos - fwd * speed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camPos = camPos - right * speed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camPos = camPos + right * speed;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) camPos.y -= speed;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) camPos.y += speed;

        // 材质切换 (M)
        static bool mLast = false;
        bool mNow = glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS;
        if (mNow && !mLast) {
            matPreset = (matPreset + 1) % 7;
            std::cout << "Material preset: " << matPreset << "\n";
        }
        mLast = mNow;

        // 玻璃 (G)
        static bool gLast = false;
        bool gNow = glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS;
        if (gNow && !gLast) {
            glassEnabled = !glassEnabled;
            std::cout << "Glass: " << (glassEnabled ? "ON" : "OFF") << "\n";
        }
        gLast = gNow;

        // 自发光 (F)
        static bool eLast = false;
        bool eNow = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        if (eNow && !eLast) {
            emissiveEnabled = !emissiveEnabled;
            std::cout << "Emissive: " << (emissiveEnabled ? "ON" : "OFF") << "\n";
        }
        eLast = eNow;

        drawFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    vkDeviceWaitIdle(device);
}

void PBRApp::drawFrame() {
    vkWaitForFences(device, 1, &fences[frameIdx], VK_TRUE, UINT64_MAX);

    uint32_t imgIdx;
    VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                       semImgAvail[frameIdx], VK_NULL_HANDLE, &imgIdx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || fbResized) {
        fbResized = false;
        recreateSwapchain();
        return;
    }

    updateUBOs();

    // 先渲染 Shadow Map（独立提交并等待完成）
    recordShadowCommandBuffer();
    VkSubmitInfo shadowSubmitInfo{};
    shadowSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    shadowSubmitInfo.commandBufferCount = 1;
    shadowSubmitInfo.pCommandBuffers = &shadowCmdBuffer;
    vkQueueSubmit(gfxQueue, 1, &shadowSubmitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(gfxQueue);

    // 渲染主场景
    recordCommandBuffer(imgIdx);

    vkResetFences(device, 1, &fences[frameIdx]);

    // semaphore 用 imgIdx 索引，避免 presentation 重用冲突
    uint32_t semIdx = imgIdx % imageCount;
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore wait[] = {semImgAvail[frameIdx]};
    VkSemaphore sig[] = {semRendDone[semIdx]};

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = wait;
    si.pWaitDstStageMask = waitStages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmdBuffers[frameIdx];
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = sig;

    if (vkQueueSubmit(gfxQueue, 1, &si, fences[frameIdx]) != VK_SUCCESS)
        throw std::runtime_error("queue submit failed");

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = sig;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &imgIdx;
    r = vkQueuePresentKHR(presQueue, &pi);

    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || fbResized) {
        fbResized = false;
        recreateSwapchain();
    }

    frameIdx = (frameIdx + 1) % MAX_FRAMES_IN_FLIGHT;
}

void PBRApp::recreateSwapchain() {
    vkDeviceWaitIdle(device);
    for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    for (auto iv : scImageViews) vkDestroyImageView(device, iv, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyImageView(device, depthImageView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthMemory, nullptr);

    createSwapchain();
    createImageViews();
    createDepthBuffer();
    createFramebuffers();
}

// ============================================================================
// Cleanup
// ============================================================================
