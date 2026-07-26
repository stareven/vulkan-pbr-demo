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

void PBRApp::cleanup() {
    vkDeviceWaitIdle(device);
    for (uint32_t i = 0; i < imageCount; ++i) {
        vkDestroySemaphore(device, semImgAvail[i], nullptr);
        vkDestroySemaphore(device, semRendDone[i], nullptr);
    }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroyFence(device, fences[i], nullptr);
        vkDestroyBuffer(device, uboMVPBuf[i], nullptr);
        vkDestroyBuffer(device, uboMatBuf[i], nullptr);
        vkFreeMemory(device, uboMVPMem[i], nullptr);
        vkFreeMemory(device, uboMatMem[i], nullptr);
    }
    vkDestroyDescriptorPool(device, descPool, nullptr);
    vkDestroyDescriptorSetLayout(device, dslMVP, nullptr);
    vkDestroyDescriptorSetLayout(device, dslMat, nullptr);
    vkDestroyBuffer(device, vbo, nullptr);
    vkDestroyBuffer(device, ibo, nullptr);
    vkFreeMemory(device, vboMem, nullptr);
    vkFreeMemory(device, iboMem, nullptr);
    vkDestroyBuffer(device, planeVbo, nullptr);
    vkDestroyBuffer(device, planeIbo, nullptr);
    vkFreeMemory(device, planeVboMem, nullptr);
    vkFreeMemory(device, planeIboMem, nullptr);
    vkDestroyCommandPool(device, cmdPool, nullptr);
    for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    vkDestroyImageView(device, depthImageView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthMemory, nullptr);
    for (auto iv : scImageViews) vkDestroyImageView(device, iv, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
}

// ============================================================================
// run()
// ============================================================================
void PBRApp::run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
}
