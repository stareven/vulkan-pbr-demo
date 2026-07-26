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

void PBRApp::createSyncObjects() {
    // 为每个 swapchain 图像创建 semaphore（避免重用冲突）
    imageCount = (uint32_t)scImages.size();
    semImgAvail.resize(imageCount);
    semRendDone.resize(imageCount);
    // fence 仍然按 MAX_FRAMES_IN_FLIGHT 创建（用于帧 pacing）
    fences.resize(MAX_FRAMES_IN_FLIGHT);
    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < imageCount; ++i) {
        if (vkCreateSemaphore(device, &sci, nullptr, &semImgAvail[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &sci, nullptr, &semRendDone[i]) != VK_SUCCESS)
            throw std::runtime_error("sync object creation failed");
    }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateFence(device, &fci, nullptr, &fences[i]) != VK_SUCCESS)
            throw std::runtime_error("fence creation failed");
    }
}

// ============================================================================
// Shadow Map
// ============================================================================
