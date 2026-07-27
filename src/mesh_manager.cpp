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
    // Update MVP UBO (C++ 是 row-major，GLSL 是 column-major，需要转置)
    {
        UBO_MVP ubo{};
        ubo.model = model.transposed();
        ubo.view = view.transposed();
        ubo.proj = proj.transposed();
        ubo.lightSpaceMatrix = lightSpaceMatrix.transposed();
        ubo.cameraPos = cameraPos;

        void* data;
        vkMapMemory(device, uboMVPMem[imageIndex], 0, sizeof(ubo), 0, &data);
        memcpy(data, &ubo, sizeof(ubo));
        vkUnmapMemory(device, uboMVPMem[imageIndex]);
    }

    // Update Material UBO
    {
        // Material presets（对齐 pbr_runtime.cpp 的预设值）
        struct MaterialPreset {
            Vec3 albedo;
            float metallic;
            float roughness;
        };

        MaterialPreset presets[] = {
            {{0.95f, 0.35f, 0.10f}, 0.0f, 0.7f},   // 红色塑料
            {{0.95f, 0.93f, 0.88f}, 1.0f, 0.1f},   // 银
            {{1.00f, 0.77f, 0.33f}, 1.0f, 0.3f},   // 金
            {{0.97f, 0.96f, 0.91f}, 1.0f, 0.2f},   // 铝
            {{0.30f, 0.85f, 0.39f}, 0.0f, 0.4f},   // 绿色塑料
            {{0.50f, 0.50f, 0.50f}, 1.0f, 0.5f},   // 铁
            {{0.98f, 0.99f, 1.00f}, 0.0f, 0.05f},  // 玻璃
        };

        auto& pr = presets[materialPreset % 7];

        UBO_Material ubo{};
        ubo.albedo = pr.albedo;
        ubo.metallic = pr.metallic;
        ubo.roughness = pr.roughness;
        ubo.ao = 1.0f;
        ubo.ior = 1.5f;
        ubo.opacity = 1.0f;
        ubo.cameraPos = cameraPos;
        ubo.ambientLight = {0.03f, 0.03f, 0.03f};

        if ((materialPreset % 7) == 6 && glassEnabled) {
            ubo.ior = 1.52f;
            ubo.opacity = 0.3f;
        }

        // Lights (对齐 pbr_runtime.cpp 的光源配置)
        ubo.lights[0] = {{10, 10, 10}, 0.0f, {300, 300, 300}, 1.0f};
        ubo.lights[1] = {{-10, 10, 10}, 0.0f, {300, 100, 100}, 1.0f};
        ubo.lights[2] = {{10, -10, -10}, 0.0f, {100, 300, 100}, 1.0f};
        ubo.lights[3] = {{-10, -10, -10}, 0.0f, {100, 100, 300}, 1.0f};

        if (emissiveEnabled) {
            ubo.emissive = {1.0f, 0.5f, 0.1f};
            ubo.emissiveStrength = 2.0f;
        } else {
            ubo.emissive = {0.0f, 0.0f, 0.0f};
            ubo.emissiveStrength = 0.0f;
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
    auto vertices = generatePlane(10.0f, -1.5f);
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
