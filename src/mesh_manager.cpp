#include "mesh_manager.h"
#include "vulkan_utils.h"
#include "mesh.h"

#include <cmath>
#include <stdexcept>
#include <cstring>

MeshManager::~MeshManager() {
    // Cleanup should be called explicitly
}

void MeshManager::createMeshes(VkDevice device, VkPhysicalDevice physicalDevice,
                               VkQueue graphicsQueue, VkCommandPool cmdPool) {
    createSphereMesh(device, physicalDevice, graphicsQueue, cmdPool);
    createPlaneMesh(device, physicalDevice, graphicsQueue, cmdPool);
}

void MeshManager::createUniformBuffers(VkDevice device, VkPhysicalDevice physicalDevice, size_t imageCount) {
    uboMVPBuf.resize(imageCount);
    uboMVPMem.resize(imageCount);
    uboMatBuf.resize(imageCount);
    uboMatMem.resize(imageCount);

    for (size_t i = 0; i < imageCount; ++i) {
        createBuffer(device, physicalDevice, sizeof(UBO_MVP),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    uboMVPBuf[i], uboMVPMem[i]);
        createBuffer(device, physicalDevice, sizeof(UBO_Material),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    uboMatBuf[i], uboMatMem[i]);
    }
}

void MeshManager::updateUniformBuffers(VkDevice device, uint32_t imageIndex, const Mat4& model, const Mat4& view, const Mat4& proj,
                                       const Mat4& lightSpaceMatrix, const Vec3& cameraPos,
                                       int materialPreset, bool glassEnabled, bool emissiveEnabled) {
    // Update MVP UBO
    {
        UBO_MVP ubo{};
        ubo.model = model;
        ubo.view = view;
        ubo.proj = proj;
        ubo.lightSpaceMatrix = lightSpaceMatrix;
        ubo.cameraPos = cameraPos;

        void* data;
        vkMapMemory(device, uboMVPMem[imageIndex], 0, sizeof(ubo), 0, &data);
        memcpy(data, &ubo, sizeof(ubo));
        vkUnmapMemory(device, uboMVPMem[imageIndex]);
    }

    // Update Material UBO
    {
        // Material presets
        struct MaterialPreset {
            Vec3 albedo;
            float metallic;
            float roughness;
            float ao;
            float ior;
            float opacity;
        };

        MaterialPreset presets[] = {
            {{0.8f, 0.2f, 0.2f}, 0.0f, 0.8f, 1.0f, 1.5f, 1.0f},  // Red plastic
            {{0.9f, 0.9f, 0.9f}, 1.0f, 0.1f, 1.0f, 1.5f, 1.0f},  // Silver mirror
            {{0.1f, 0.2f, 0.8f}, 0.0f, 0.3f, 1.0f, 1.5f, 1.0f},  // Blue rubber
            {{0.9f, 0.7f, 0.2f}, 1.0f, 0.3f, 1.0f, 1.5f, 1.0f},  // Gold
            {{0.5f, 0.5f, 0.5f}, 0.0f, 0.5f, 1.0f, 1.5f, 1.0f},  // Gray
            {{0.2f, 0.8f, 0.3f}, 0.0f, 0.6f, 1.0f, 1.5f, 1.0f},  // Green
            {{0.9f, 0.9f, 0.95f}, 0.0f, 0.0f, 1.0f, 1.5f, 1.0f}, // Glass
        };

        auto& pr = presets[materialPreset % 7];

        UBO_Material ubo{};
        ubo.albedo = pr.albedo;
        ubo.metallic = pr.metallic;
        ubo.roughness = pr.roughness;
        ubo.ao = pr.ao;
        ubo.ior = pr.ior;
        ubo.opacity = pr.opacity;
        ubo.cameraPos = cameraPos;

        if ((materialPreset % 7) == 6 && glassEnabled) {
            ubo.opacity = 0.3f;
            ubo.ior = 1.5f;
        }

        ubo.ambientLight = {0.1f, 0.1f, 0.1f};

        // Lights
        ubo.lights[0].position = {3, 3, 3};
        ubo.lights[0].color = {1, 1, 1};
        ubo.lights[0].intensity = 20;

        ubo.lights[1].position = {-3, 2, -3};
        ubo.lights[1].color = {0.8f, 0.8f, 1.0f};
        ubo.lights[1].intensity = 15;

        ubo.lights[2].position = {0, 5, 0};
        ubo.lights[2].color = {1, 0.9f, 0.8f};
        ubo.lights[2].intensity = 10;

        ubo.lights[3].position = {5, 1, -2};
        ubo.lights[3].color = {1, 0.5f, 0.5f};
        ubo.lights[3].intensity = 8;

        if (emissiveEnabled) {
            ubo.emissive = {1, 1, 1};
            ubo.emissiveStrength = 2.0f;
        } else {
            ubo.emissive = {0, 0, 0};
            ubo.emissiveStrength = 0;
        }

        void* data;
        vkMapMemory(device, uboMatMem[imageIndex], 0, sizeof(ubo), 0, &data);
        memcpy(data, &ubo, sizeof(ubo));
        vkUnmapMemory(device, uboMatMem[imageIndex]);
    }
}

void MeshManager::cleanup(VkDevice device) {
    for (auto buf : uboMVPBuf) {
        if (buf) vkDestroyBuffer(device, buf, nullptr);
    }
    for (auto mem : uboMVPMem) {
        if (mem) vkFreeMemory(device, mem, nullptr);
    }
    for (auto buf : uboMatBuf) {
        if (buf) vkDestroyBuffer(device, buf, nullptr);
    }
    for (auto mem : uboMatMem) {
        if (mem) vkFreeMemory(device, mem, nullptr);
    }

    if (planeIbo) vkDestroyBuffer(device, planeIbo, nullptr);
    if (planeVbo) vkDestroyBuffer(device, planeVbo, nullptr);
    if (planeIboMem) vkFreeMemory(device, planeIboMem, nullptr);
    if (planeVboMem) vkFreeMemory(device, planeVboMem, nullptr);

    if (ibo) vkDestroyBuffer(device, ibo, nullptr);
    if (vbo) vkDestroyBuffer(device, vbo, nullptr);
    if (iboMem) vkFreeMemory(device, iboMem, nullptr);
    if (vboMem) vkFreeMemory(device, vboMem, nullptr);
}

void MeshManager::createSphereMesh(VkDevice device, VkPhysicalDevice physicalDevice,
                                   VkQueue graphicsQueue, VkCommandPool cmdPool) {
    auto vertices = generateSphere(32, 64);
    auto indices = generateSphereIndices(32, 64);
    indexCount = (uint32_t)indices.size();

    VkDeviceSize vboSize = sizeof(vertices[0]) * vertices.size();
    VkDeviceSize iboSize = sizeof(indices[0]) * indices.size();

    // Create staging buffers
    VkBuffer stagingVbo, stagingIbo;
    VkDeviceMemory stagingVboMem, stagingIboMem;

    createBuffer(device, physicalDevice, vboSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingVbo, stagingVboMem);
    createBuffer(device, physicalDevice, iboSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingIbo, stagingIboMem);

    // Copy data to staging buffers
    void* data;
    vkMapMemory(device, stagingVboMem, 0, vboSize, 0, &data);
    memcpy(data, vertices.data(), vboSize);
    vkUnmapMemory(device, stagingVboMem);

    vkMapMemory(device, stagingIboMem, 0, iboSize, 0, &data);
    memcpy(data, indices.data(), iboSize);
    vkUnmapMemory(device, stagingIboMem);

    // Create device-local buffers
    createBuffer(device, physicalDevice, vboSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                vbo, vboMem);
    createBuffer(device, physicalDevice, iboSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                ibo, iboMem);

    // Copy to device-local buffers
    copyBuffer(device, graphicsQueue, cmdPool, stagingVbo, vbo, vboSize);
    copyBuffer(device, graphicsQueue, cmdPool, stagingIbo, ibo, iboSize);

    // Cleanup staging buffers
    vkDestroyBuffer(device, stagingVbo, nullptr);
    vkFreeMemory(device, stagingVboMem, nullptr);
    vkDestroyBuffer(device, stagingIbo, nullptr);
    vkFreeMemory(device, stagingIboMem, nullptr);
}

void MeshManager::createPlaneMesh(VkDevice device, VkPhysicalDevice physicalDevice,
                                  VkQueue graphicsQueue, VkCommandPool cmdPool) {
    auto vertices = generatePlane(10, 0);
    auto indices = generatePlaneIndices();
    planeIndexCount = (uint32_t)indices.size();

    VkDeviceSize vboSize = sizeof(vertices[0]) * vertices.size();
    VkDeviceSize iboSize = sizeof(indices[0]) * indices.size();

    // Create staging buffers
    VkBuffer stagingVbo, stagingIbo;
    VkDeviceMemory stagingVboMem, stagingIboMem;

    createBuffer(device, physicalDevice, vboSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingVbo, stagingVboMem);
    createBuffer(device, physicalDevice, iboSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingIbo, stagingIboMem);

    // Copy data
    void* data;
    vkMapMemory(device, stagingVboMem, 0, vboSize, 0, &data);
    memcpy(data, vertices.data(), vboSize);
    vkUnmapMemory(device, stagingVboMem);

    vkMapMemory(device, stagingIboMem, 0, iboSize, 0, &data);
    memcpy(data, indices.data(), iboSize);
    vkUnmapMemory(device, stagingIboMem);

    // Create device-local buffers
    createBuffer(device, physicalDevice, vboSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                planeVbo, planeVboMem);
    createBuffer(device, physicalDevice, iboSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                planeIbo, planeIboMem);

    // Copy
    copyBuffer(device, graphicsQueue, cmdPool, stagingVbo, planeVbo, vboSize);
    copyBuffer(device, graphicsQueue, cmdPool, stagingIbo, planeIbo, iboSize);

    // Cleanup
    vkDestroyBuffer(device, stagingVbo, nullptr);
    vkFreeMemory(device, stagingVboMem, nullptr);
    vkDestroyBuffer(device, stagingIbo, nullptr);
    vkFreeMemory(device, stagingIboMem, nullptr);
}
