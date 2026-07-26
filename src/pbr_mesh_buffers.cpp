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

void PBRApp::createCommandPool() {
    auto q = findQueues(pd, surface);
    VkCommandPoolCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = q.gfx.value();
    if (vkCreateCommandPool(device, &cpi, nullptr, &cmdPool) != VK_SUCCESS)
        throw std::runtime_error("command pool failed");
}

// ============================================================================
// Mesh
// ============================================================================
void PBRApp::createMesh() {
    auto verts = generateSphere(32, 64);
    auto idxs = generateSphereIndices(32, 64);
    indexCount = (uint32_t)idxs.size();
    std::cout << "[MESH] vertices=" << verts.size() << " indices=" << idxs.size() << "\n" << std::flush;

    VkDeviceSize vboSz = verts.size() * sizeof(Vertex);
    VkDeviceSize iboSz = idxs.size() * sizeof(uint32_t);

    // 顶点 staging buffer
    VkBuffer stgV; VkDeviceMemory stgVM;
    createBuffer(device, pd, vboSz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, stgV, stgVM);
    void* mappedV;
    vkMapMemory(device, stgVM, 0, vboSz, 0, &mappedV);
    std::memcpy(mappedV, verts.data(), vboSz);
    vkUnmapMemory(device, stgVM);

    // 索引 staging buffer
    VkBuffer stgI; VkDeviceMemory stgIM;
    createBuffer(device, pd, iboSz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, stgI, stgIM);
    void* mappedI;
    vkMapMemory(device, stgIM, 0, iboSz, 0, &mappedI);
    std::memcpy(mappedI, idxs.data(), iboSz);
    vkUnmapMemory(device, stgIM);

    // Device-local buffers
    createBuffer(device, pd, vboSz,
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vbo, vboMem);
    createBuffer(device, pd, iboSz,
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ibo, iboMem);

    copyBuffer(device, gfxQueue, cmdPool, stgV, vbo, vboSz);
    copyBuffer(device, gfxQueue, cmdPool, stgI, ibo, iboSz);

    vkDestroyBuffer(device, stgV, nullptr);
    vkFreeMemory(device, stgVM, nullptr);
    vkDestroyBuffer(device, stgI, nullptr);
    vkFreeMemory(device, stgIM, nullptr);

    // 地面平面（用于接收阴影）
    auto planeVerts = generatePlane(10.0f, -1.5f);
    auto planeIdxs = generatePlaneIndices();
    planeIndexCount = (uint32_t)planeIdxs.size();

    VkDeviceSize pvboSz = planeVerts.size() * sizeof(Vertex);
    VkDeviceSize piboSz = planeIdxs.size() * sizeof(uint32_t);

    VkBuffer stgPV; VkDeviceMemory stgPVM;
    createBuffer(device, pd, pvboSz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, stgPV, stgPVM);
    void* mPV; vkMapMemory(device, stgPVM, 0, pvboSz, 0, &mPV);
    std::memcpy(mPV, planeVerts.data(), pvboSz); vkUnmapMemory(device, stgPVM);

    VkBuffer stgPI; VkDeviceMemory stgPIM;
    createBuffer(device, pd, piboSz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, stgPI, stgPIM);
    void* mPI; vkMapMemory(device, stgPIM, 0, piboSz, 0, &mPI);
    std::memcpy(mPI, planeIdxs.data(), piboSz); vkUnmapMemory(device, stgPIM);

    createBuffer(device, pd, pvboSz,
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, planeVbo, planeVboMem);
    createBuffer(device, pd, piboSz,
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, planeIbo, planeIboMem);

    copyBuffer(device, gfxQueue, cmdPool, stgPV, planeVbo, pvboSz);
    copyBuffer(device, gfxQueue, cmdPool, stgPI, planeIbo, piboSz);

    vkDestroyBuffer(device, stgPV, nullptr); vkFreeMemory(device, stgPVM, nullptr);
    vkDestroyBuffer(device, stgPI, nullptr); vkFreeMemory(device, stgPIM, nullptr);
}

// ============================================================================
// Uniform buffers
// ============================================================================
void PBRApp::createUniformBuffers() {
    uboMVPBuf.resize(MAX_FRAMES_IN_FLIGHT);
    uboMVPMem.resize(MAX_FRAMES_IN_FLIGHT);
    uboMatBuf.resize(MAX_FRAMES_IN_FLIGHT);
    uboMatMem.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        createBuffer(device, pd, sizeof(UBO_MVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, uboMVPBuf[i], uboMVPMem[i]);
        createBuffer(device, pd, sizeof(UBO_Material), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, uboMatBuf[i], uboMatMem[i]);
    }
}

// ============================================================================
// Descriptor pool & sets
// ============================================================================
