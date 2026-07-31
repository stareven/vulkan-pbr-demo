#include "mesh_manager.h"
#include "vulkan_utils.h"
#include "mesh.h"
using namespace vulkan;

#include <cmath>
#include <stdexcept>
#include <cstring>

// ----------------------------------------------------------------------------
// 析构函数: cleanup 必须显式调用
// ----------------------------------------------------------------------------
MeshManager::~MeshManager() {
    // Cleanup should be called explicitly
}

// ----------------------------------------------------------------------------
// 创建网格 (球体 + 地面)
// ----------------------------------------------------------------------------
// 流程 (Vulkan 的标准模式):
//   1. 生成顶点/索引数据 (CPU 内存)
//   2. 创建 Staging Buffer (HOST_VISIBLE, CPU 可写)
//   3. CPU 写入数据到 Staging Buffer (vkMapMemory + memcpy)
//   4. 创建 Device Local Buffer (DEVICE_LOCAL, GPU 显存)
//   5. 用 vkCmdCopyBuffer 从 Staging 拷贝到 Device Local
//   6. 销毁 Staging Buffer (不再需要)
//
// 为什么要两步?
//   - Device Local 内存 GPU 访问最快, 但 CPU 不可见
//   - Host Visible 内存 CPU 可写, 但 GPU 访问慢
//   - 标准做法: CPU 写入 Host Visible, GPU 从 Device Local 读取
// ----------------------------------------------------------------------------
void MeshManager::createMeshes(VkQueue graphicsQueue, VkCommandPool cmdPool) {
    // --- 球体 ---
    // 生成球体顶点和索引 (32 纬度 x 64 经度细分)
    auto vertices = generateSphere(32, 64);
    auto indices = generateSphereIndices(32, 64);
    indexCount = (uint32_t)indices.size();

    // 计算缓冲大小 (字节)
    VkDeviceSize vboSize = sizeof(vertices[0]) * vertices.size();
    VkDeviceSize iboSize = sizeof(indices[0]) * indices.size();

    // 1. 创建 Staging Buffer (HOST_VISIBLE, CPU 可写)
    VkBuffer stagingVbo, stagingIbo;
    VkDeviceMemory stagingVboMem, stagingIboMem;

    createBuffer(device, physicalDevice, vboSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,  // 用作拷贝源
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingVbo, stagingVboMem);
    createBuffer(device, physicalDevice, iboSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingIbo, stagingIboMem);

    // 2. CPU 写入数据到 Staging Buffer
    // vkMapMemory: 把 GPU 内存映射到 CPU 地址空间
    // memcpy: 复制数据
    // vkUnmapMemory: 取消映射
    void* data;
    vkMapMemory(device, stagingVboMem, 0, vboSize, 0, &data);
    memcpy(data, vertices.data(), vboSize);
    vkUnmapMemory(device, stagingVboMem);

    vkMapMemory(device, stagingIboMem, 0, iboSize, 0, &data);
    memcpy(data, indices.data(), iboSize);
    vkUnmapMemory(device, stagingIboMem);

    // 3. 创建 Device Local Buffer (GPU 显存, 渲染时快速访问)
    createBuffer(device, physicalDevice, vboSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                // TRANSFER_DST: 接收从 Staging 拷贝的数据
                // VERTEX_BUFFER: 用作顶点缓冲
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,  // GPU 显存
                vbo, vboMem);
    createBuffer(device, physicalDevice, iboSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                ibo, iboMem);

    // 4. 从 Staging 拷贝到 Device Local
    copyBuffer(device, graphicsQueue, cmdPool, stagingVbo, vbo, vboSize);
    copyBuffer(device, graphicsQueue, cmdPool, stagingIbo, ibo, iboSize);

    // 5. 销毁 Staging Buffer (不再需要)
    vkDestroyBuffer(device, stagingVbo, nullptr);
    vkFreeMemory(device, stagingVboMem, nullptr);
    vkDestroyBuffer(device, stagingIbo, nullptr);
    vkFreeMemory(device, stagingIboMem, nullptr);

    // --- 地面 ---
    // 生成地面平面 (10x10, 高度 -1.5)
    auto planeVerts = generatePlane(10.0f, -1.5f);
    auto planeIndices = generatePlaneIndices();
    planeIndexCount = (uint32_t)planeIndices.size();

    VkDeviceSize pvboSize = sizeof(planeVerts[0]) * planeVerts.size();
    VkDeviceSize piboSize = sizeof(planeIndices[0]) * planeIndices.size();

    // 同样的 Staging -> Device Local 流程
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

// ----------------------------------------------------------------------------
// 创建 Uniform 缓冲
// ----------------------------------------------------------------------------
// 每帧独立一套 UBO (数量 = imageCount):
//   - UBO_MVP: 变换矩阵 (每帧更新)
//   - UBO_Material (球体): 材质参数 (每帧更新)
//   - UBO_Material (地面): 固定材质 (创建时初始化, 不再更新)
//
// 为什么用 HOST_VISIBLE 而不是 DEVICE_LOCAL?
//   - UBO 每帧都要更新 (MVP 矩阵变化, 材质参数切换)
//   - HOST_VISIBLE 允许 CPU 直接写入, 无需 Staging 拷贝
//   - 性能略低于 DEVICE_LOCAL, 但更新方便
// ----------------------------------------------------------------------------
void MeshManager::createUniformBuffers(size_t imageCount) {
    uboMVPBuf.resize(imageCount);
    uboMVPMem.resize(imageCount);
    uboMatBuf.resize(imageCount);
    uboMatMem.resize(imageCount);
    uboMatGroundBuf.resize(imageCount);
    uboMatGroundMem.resize(imageCount);

    // 每帧创建一个 UBO
    for (size_t i = 0; i < imageCount; ++i) {
        // MVP UBO
        createBuffer(device, physicalDevice, sizeof(UBO_MVP),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    uboMVPBuf[i], uboMVPMem[i]);
        // 球体材质 UBO
        createBuffer(device, physicalDevice, sizeof(UBO_Material),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    uboMatBuf[i], uboMatMem[i]);
        // 地面材质 UBO
        createBuffer(device, physicalDevice, sizeof(UBO_Material),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    uboMatGroundBuf[i], uboMatGroundMem[i]);
    }

    // 地面材质：固定中性混凝土，不受 M/G/F 影响
    // 创建时一次性初始化, 之后不再更新
    for (size_t i = 0; i < imageCount; ++i) {
        UBO_Material ground{};
        ground.albedo = {0.45f, 0.43f, 0.40f};  // 灰色混凝土
        ground.metallic = 0.0f;      // 非金属
        ground.roughness = 0.85f;    // 粗糙
        ground.ao = 1.0f;            // 无遮蔽
        ground.ior = 1.5f;           // 折射率
        ground.opacity = 1.0f;       // 不透明
        ground.cameraPos = {0, 3, 8}; // 占位，每帧会被 MVP UBO 覆盖
        ground.ambientLight = {0.03f, 0.03f, 0.03f};  // 微弱环境光
        // 4 个点光源 (不同颜色和位置)
        ground.lights[0] = {{10, 10, 10}, 0.0f, {300, 300, 300}, 1.0f};
        ground.lights[1] = {{-10, 10, 10}, 0.0f, {300, 100, 100}, 1.0f};
        ground.lights[2] = {{10, -10, -10}, 0.0f, {100, 300, 100}, 1.0f};
        ground.lights[3] = {{-10, -10, -10}, 0.0f, {100, 100, 300}, 1.0f};
        ground.emissive = {0, 0, 0};
        ground.emissiveStrength = 0.0f;

        // CPU 写入到 UBO
        void* data;
        vkMapMemory(device, uboMatGroundMem[i], 0, sizeof(ground), 0, &data);
        memcpy(data, &ground, sizeof(ground));
        vkUnmapMemory(device, uboMatGroundMem[i]);
    }
}

// ----------------------------------------------------------------------------
// 更新 Uniform 缓冲 (每帧调用)
// ----------------------------------------------------------------------------
// 更新 MVP UBO:
//   - model/view/proj: 变换矩阵 (C++ 是行主序, GLSL 是列主序, 需要转置)
//   - lightSpaceMatrix: 光源空间矩阵 (用于阴影)
//   - cameraPos: 相机位置
//
// 更新材质 UBO:
//   - 根据 materialPreset 选择预定义材质
//   - 根据 glassEnabled 调整透明度和折射率
//   - 根据 emissiveEnabled 调整自发光
// ----------------------------------------------------------------------------
void MeshManager::updateUniformBuffers(uint32_t imageIndex, const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj,
                                       const glm::mat4& lightSpaceMatrix, const glm::vec3& cameraPos,
                                       int materialPreset, bool glassEnabled, bool emissiveEnabled) {
    // 更新 MVP UBO
    {
        UBO_MVP ubo{};
        // GLM 的 mat4 默认列主序, 与 GLSL 的 mat4 布局一致
        // 可以直接 memcpy, 不需要转置
        ubo.model = model;
        ubo.view = view;
        ubo.proj = proj;
        ubo.lightSpaceMatrix = lightSpaceMatrix;
        ubo.cameraPos = cameraPos;

        // CPU 写入到 UBO
        void* data;
        vkMapMemory(device, uboMVPMem[imageIndex], 0, sizeof(ubo), 0, &data);
        memcpy(data, &ubo, sizeof(ubo));
        vkUnmapMemory(device, uboMVPMem[imageIndex]);
    }

    // 更新材质 UBO
    {
        // 7 种材质预设 (albedo/metallic/roughness)
        struct MaterialPreset {
            glm::vec3 albedo;
            float metallic;
            float roughness;
        };

        MaterialPreset presets[] = {
            {{0.95f, 0.35f, 0.10f}, 0.0f, 0.7f},   // 0: 橙色塑料
            {{0.95f, 0.93f, 0.88f}, 1.0f, 0.1f},   // 1: 银色金属
            {{1.00f, 0.77f, 0.33f}, 1.0f, 0.3f},   // 2: 金色金属
            {{0.97f, 0.96f, 0.91f}, 1.0f, 0.2f},   // 3: 白金金属
            {{0.30f, 0.85f, 0.39f}, 0.0f, 0.4f},   // 4: 绿色塑料
            {{0.50f, 0.50f, 0.50f}, 1.0f, 0.5f},   // 5: 灰色金属
            {{0.98f, 0.99f, 1.00f}, 0.0f, 0.05f},  // 6: 白色陶瓷
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

        // 玻璃模式: 透明 + 折射
        if (glassEnabled) {
            ubo.ior = 1.52f;       // 玻璃折射率
            ubo.opacity = 0.2f;    // 半透明
            ubo.roughness = 0.02f; // 光滑
        }

        // 4 个点光源
        ubo.lights[0] = {{10, 10, 10}, 0.0f, {300, 300, 300}, 1.0f};
        ubo.lights[1] = {{-10, 10, 10}, 0.0f, {300, 100, 100}, 1.0f};
        ubo.lights[2] = {{10, -10, -10}, 0.0f, {100, 300, 100}, 1.0f};
        ubo.lights[3] = {{-10, -10, -10}, 0.0f, {100, 100, 300}, 1.0f};

        // 自发光模式
        if (emissiveEnabled) {
            ubo.emissive = {2.0f, 0.3f, 0.05f};  // 橙红色自发光
            ubo.emissiveStrength = 5.0f;
        } else {
            ubo.emissive = {0.0f, 0.0f, 0.0f};
            ubo.emissiveStrength = 0.0f;
        }

        // CPU 写入到 UBO
        void* data;
        vkMapMemory(device, uboMatMem[imageIndex], 0, sizeof(ubo), 0, &data);
        memcpy(data, &ubo, sizeof(ubo));
        vkUnmapMemory(device, uboMatMem[imageIndex]);
    }
}

// ----------------------------------------------------------------------------
// 销毁所有缓冲
// ----------------------------------------------------------------------------
// 销毁顺序:
//   1. UBO (MVP/Material/MaterialGround)
//   2. 地面网格 (VBO/IBO)
//   3. 球体网格 (VBO/IBO)
// ----------------------------------------------------------------------------
void MeshManager::cleanup() {
    // 销毁 UBO
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

    // 销毁地面网格
    if (planeIbo) vkDestroyBuffer(device, planeIbo, nullptr);
    if (planeVbo) vkDestroyBuffer(device, planeVbo, nullptr);
    if (planeIboMem) vkFreeMemory(device, planeIboMem, nullptr);
    if (planeVboMem) vkFreeMemory(device, planeVboMem, nullptr);

    // 销毁球体网格
    if (ibo) vkDestroyBuffer(device, ibo, nullptr);
    if (vbo) vkDestroyBuffer(device, vbo, nullptr);
    if (iboMem) vkFreeMemory(device, iboMem, nullptr);
    if (vboMem) vkFreeMemory(device, vboMem, nullptr);
}
