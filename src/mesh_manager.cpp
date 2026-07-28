#include "mesh_manager.h"
#include "vulkan_utils.h"
#include "mesh.h"
using namespace vulkan;

#include <cmath>
#include <stdexcept>
#include <cstring>
MeshManager::~MeshManager() {
    // Cleanup should be called explicitly
}

void MeshManager::createMeshes(VkQueue graphicsQueue, VkCommandPool cmdPool) {
    // --- Sphere ---
    auto vertices = generateSphere(32, 64);
    auto indices = generateSphereIndices(32, 64);
    indexCount = (uint32_t)indices.size();

    VkDeviceSize vboSize = sizeof(vertices[0]) * vertices.size();
    VkDeviceSize iboSize = sizeof(indices[0]) * indices.size();

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

    void* data;
    vkMapMemory(device, stagingVboMem, 0, vboSize, 0, &data);
    memcpy(data, vertices.data(), vboSize);
    vkUnmapMemory(device, stagingVboMem);

    vkMapMemory(device, stagingIboMem, 0, iboSize, 0, &data);
    memcpy(data, indices.data(), iboSize);
    vkUnmapMemory(device, stagingIboMem);

    createBuffer(device, physicalDevice, vboSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                vbo, vboMem);
    createBuffer(device, physicalDevice, iboSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                ibo, iboMem);

    copyBuffer(device, graphicsQueue, cmdPool, stagingVbo, vbo, vboSize);
    copyBuffer(device, graphicsQueue, cmdPool, stagingIbo, ibo, iboSize);

    vkDestroyBuffer(device, stagingVbo, nullptr);
    vkFreeMemory(device, stagingVboMem, nullptr);
    vkDestroyBuffer(device, stagingIbo, nullptr);
    vkFreeMemory(device, stagingIboMem, nullptr);

    // --- Plane ---
    auto planeVerts = generatePlane(10.0f, -1.5f);
    auto planeIndices = generatePlaneIndices();
    planeIndexCount = (uint32_t)planeIndices.size();

    VkDeviceSize pvboSize = sizeof(planeVerts[0]) * planeVerts.size();
    VkDeviceSize piboSize = sizeof(planeIndices[0]) * planeIndices.size();

    VkBuffer pStagingVbo, pStagingIbo;
    VkDeviceMemory pStagingVboMem, pStagingIboMem;

    createBuffer(device, physicalDevice, pvboSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                pStagingVbo, pStagingVboMem);
    createBuffer(device, physicalDevice, piboSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                pStagingIbo, pStagingIboMem);

    vkMapMemory(device, pStagingVboMem, 0, pvboSize, 0, &data);
    memcpy(data, planeVerts.data(), pvboSize);
    vkUnmapMemory(device, pStagingVboMem);

    vkMapMemory(device, pStagingIboMem, 0, piboSize, 0, &data);
    memcpy(data, planeIndices.data(), piboSize);
    vkUnmapMemory(device, pStagingIboMem);

    createBuffer(device, physicalDevice, pvboSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                planeVbo, planeVboMem);
    createBuffer(device, physicalDevice, piboSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                planeIbo, planeIboMem);

    copyBuffer(device, graphicsQueue, cmdPool, pStagingVbo, planeVbo, pvboSize);
    copyBuffer(device, graphicsQueue, cmdPool, pStagingIbo, planeIbo, piboSize);

    vkDestroyBuffer(device, pStagingVbo, nullptr);
    vkFreeMemory(device, pStagingVboMem, nullptr);
    vkDestroyBuffer(device, pStagingIbo, nullptr);
    vkFreeMemory(device, pStagingIboMem, nullptr);
}

void MeshManager::createUniformBuffers(size_t imageCount) {
    uboMVPBuf.resize(imageCount);
    uboMVPMem.resize(imageCount);
    uboMatBuf.resize(imageCount);
    uboMatMem.resize(imageCount);
    uboMatGroundBuf.resize(imageCount);
    uboMatGroundMem.resize(imageCount);

    for (size_t i = 0; i < imageCount; ++i) {
        createBuffer(device, physicalDevice, sizeof(UBO_MVP),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    uboMVPBuf[i], uboMVPMem[i]);
        createBuffer(device, physicalDevice, sizeof(UBO_Material),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    uboMatBuf[i], uboMatMem[i]);
        createBuffer(device, physicalDevice, sizeof(UBO_Material),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    uboMatGroundBuf[i], uboMatGroundMem[i]);
    }

    // 地面材质：固定中性混凝土，不受 M/G/F 影响
    for (size_t i = 0; i < imageCount; ++i) {
        UBO_Material ground{};
        ground.albedo = {0.45f, 0.43f, 0.40f};
        ground.metallic = 0.0f;
        ground.roughness = 0.85f;
        ground.ao = 1.0f;
        ground.ior = 1.5f;
        ground.opacity = 1.0f;
        ground.cameraPos = {0, 3, 8}; // 占位，每帧会被 MVP UBO 覆盖
        ground.ambientLight = {0.03f, 0.03f, 0.03f};
        ground.lights[0] = {{10, 10, 10}, 0.0f, {300, 300, 300}, 1.0f};
        ground.lights[1] = {{-10, 10, 10}, 0.0f, {300, 100, 100}, 1.0f};
        ground.lights[2] = {{10, -10, -10}, 0.0f, {100, 300, 100}, 1.0f};
        ground.lights[3] = {{-10, -10, -10}, 0.0f, {100, 100, 300}, 1.0f};
        ground.emissive = {0, 0, 0};
        ground.emissiveStrength = 0.0f;

        void* data;
        vkMapMemory(device, uboMatGroundMem[i], 0, sizeof(ground), 0, &data);
        memcpy(data, &ground, sizeof(ground));
        vkUnmapMemory(device, uboMatGroundMem[i]);
    }
}

void MeshManager::updateUniformBuffers(uint32_t imageIndex, const Mat4& model, const Mat4& view, const Mat4& proj,
                                       const Mat4& lightSpaceMatrix, const Vec3& cameraPos,
                                       int materialPreset, bool glassEnabled, bool emissiveEnabled) {
    // Update MVP UBO (C++ is row-major, GLSL is column-major — transpose)
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
        struct MaterialPreset {
            Vec3 albedo;
            float metallic;
            float roughness;
        };

        MaterialPreset presets[] = {
            {{0.95f, 0.35f, 0.10f}, 0.0f, 0.7f},
            {{0.95f, 0.93f, 0.88f}, 1.0f, 0.1f},
            {{1.00f, 0.77f, 0.33f}, 1.0f, 0.3f},
            {{0.97f, 0.96f, 0.91f}, 1.0f, 0.2f},
            {{0.30f, 0.85f, 0.39f}, 0.0f, 0.4f},
            {{0.50f, 0.50f, 0.50f}, 1.0f, 0.5f},
            {{0.98f, 0.99f, 1.00f}, 0.0f, 0.05f},
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

        if (glassEnabled) {
            ubo.ior = 1.52f;
            ubo.opacity = 0.2f;
            ubo.roughness = 0.02f;
        }

        ubo.lights[0] = {{10, 10, 10}, 0.0f, {300, 300, 300}, 1.0f};
        ubo.lights[1] = {{-10, 10, 10}, 0.0f, {300, 100, 100}, 1.0f};
        ubo.lights[2] = {{10, -10, -10}, 0.0f, {100, 300, 100}, 1.0f};
        ubo.lights[3] = {{-10, -10, -10}, 0.0f, {100, 100, 300}, 1.0f};

        if (emissiveEnabled) {
            ubo.emissive = {2.0f, 0.3f, 0.05f};
            ubo.emissiveStrength = 5.0f;
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

void MeshManager::cleanup() {
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
    for (auto buf : uboMatGroundBuf) {
        if (buf) vkDestroyBuffer(device, buf, nullptr);
    }
    for (auto mem : uboMatGroundMem) {
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
