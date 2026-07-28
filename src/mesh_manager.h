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
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

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
    std::vector<VkBuffer> uboMatBuf;       // 球体材质
    std::vector<VkDeviceMemory> uboMatMem;
    std::vector<VkBuffer> uboMatGroundBuf; // 地面材质（固定，不受 M/G/F 影响）
    std::vector<VkDeviceMemory> uboMatGroundMem;

public:
    MeshManager() = default;
    ~MeshManager();

    void init(VkDevice dev, VkPhysicalDevice pd) { device = dev; physicalDevice = pd; }
    void createMeshes(VkQueue graphicsQueue, VkCommandPool cmdPool);
    void createUniformBuffers(size_t imageCount);
    void updateUniformBuffers(uint32_t imageIndex, const Mat4& model, const Mat4& view, const Mat4& proj,
                             const Mat4& lightSpaceMatrix, const Vec3& cameraPos,
                             int materialPreset, bool glassEnabled, bool emissiveEnabled);
    void cleanup();

    VkBuffer getSphereVBO() const { return vbo; }
    VkBuffer getSphereIBO() const { return ibo; }
    uint32_t getSphereIndexCount() const { return indexCount; }

    VkBuffer getPlaneVBO() const { return planeVbo; }
    VkBuffer getPlaneIBO() const { return planeIbo; }
    uint32_t getPlaneIndexCount() const { return planeIndexCount; }

    VkBuffer getMVPBuffer(uint32_t index) const { return uboMVPBuf[index]; }
    VkBuffer getMaterialBuffer(uint32_t index) const { return uboMatBuf[index]; }
    VkBuffer getMaterialGroundBuffer(uint32_t index) const { return uboMatGroundBuf[index]; }
};