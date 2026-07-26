#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include "types.h"

// ============================================================================
// 网格管理 - 顶点缓冲、索引缓冲、Uniform 缓冲
// ============================================================================
class MeshManager {
private:
    // 主网格（球体）
    VkBuffer vbo = VK_NULL_HANDLE;
    VkBuffer ibo = VK_NULL_HANDLE;
    VkDeviceMemory vboMem = VK_NULL_HANDLE;
    VkDeviceMemory iboMem = VK_NULL_HANDLE;
    uint32_t indexCount = 0;

    // 地面网格
    VkBuffer planeVbo = VK_NULL_HANDLE;
    VkBuffer planeIbo = VK_NULL_HANDLE;
    VkDeviceMemory planeVboMem = VK_NULL_HANDLE;
    VkDeviceMemory planeIboMem = VK_NULL_HANDLE;
    uint32_t planeIndexCount = 0;

    // Uniform 缓冲
    std::vector<VkBuffer> uboMVPBuf;
    std::vector<VkDeviceMemory> uboMVPMem;
    std::vector<VkBuffer> uboMatBuf;
    std::vector<VkDeviceMemory> uboMatMem;

public:
    MeshManager() = default;
    ~MeshManager();

    void createMeshes(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue, VkCommandPool cmdPool);
    void createUniformBuffers(VkDevice device, VkPhysicalDevice physicalDevice, size_t imageCount);
    void updateUniformBuffers(VkDevice device, uint32_t imageIndex, const Mat4& model, const Mat4& view, const Mat4& proj,
                             const Mat4& lightSpaceMatrix, const Vec3& cameraPos,
                             int materialPreset, bool glassEnabled, bool emissiveEnabled);
    void cleanup(VkDevice device);

    // Getters
    VkBuffer getSphereVBO() const { return vbo; }
    VkBuffer getSphereIBO() const { return ibo; }
    uint32_t getSphereIndexCount() const { return indexCount; }

    VkBuffer getPlaneVBO() const { return planeVbo; }
    VkBuffer getPlaneIBO() const { return planeIbo; }
    uint32_t getPlaneIndexCount() const { return planeIndexCount; }

    VkBuffer getMVPBuffer(uint32_t index) const { return uboMVPBuf[index]; }
    VkBuffer getMaterialBuffer(uint32_t index) const { return uboMatBuf[index]; }

private:
    void createSphereMesh(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue, VkCommandPool cmdPool);
    void createPlaneMesh(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue, VkCommandPool cmdPool);
};
