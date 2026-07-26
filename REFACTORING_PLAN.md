# PBRApp 面向对象重构方案

## 重构目标

将上帝类 PBRApp (97个成员变量, 33个函数) 拆分为 10 个职责单一的类，遵循单一职责原则。

## 核心设计原则

1. **组合优于继承** - 使用组合模式，PBRApp 作为协调者
2. **依赖注入** - 通过构造函数或初始化方法传递依赖
3. **最小知识原则** - 每个类只访问自己需要的资源
4. **渐进式重构** - 分阶段实施，每阶段保持可运行状态

## 类设计

### 1. VulkanContext (核心上下文)
**职责**: 管理 Vulkan 核心资源和 Surface
```cpp
class VulkanContext {
public:
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkSurfaceKHR surface;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    uint32_t graphicsFamily;
    uint32_t presentFamily;
    
    void initialize(GLFWwindow* window);
    void cleanup();
};
```

### 2. Window (窗口管理)
**职责**: 窗口创建、事件处理、输入管理
```cpp
class Window {
private:
    GLFWwindow* handle;
    int width, height;
    std::string title;
    
    // 输入状态
    Vec3 cameraPos;
    float cameraYaw, cameraPitch;
    bool mouseDown;
    double lastMouseX, lastMouseY;
    
public:
    void create(int width, int height, const std::string& title);
    void pollEvents();
    bool shouldClose() const;
    VkSurfaceKHR createSurface(VkInstance instance);
    
    // 相机控制
    void updateCamera();
    Vec3 getCameraPosition() const;
    float getCameraYaw() const;
    float getCameraPitch() const;
    
    void destroy();
};
```

### 3. SwapchainManager (Swapchain 管理)
**职责**: Swapchain、图像视图、深度缓冲
```cpp
class SwapchainManager {
private:
    VkSwapchainKHR swapchain;
    VkFormat format;
    VkExtent2D extent;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    
    // 深度缓冲
    VkImage depthImage;
    VkDeviceMemory depthMemory;
    VkImageView depthImageView;
    
public:
    void create(VkDevice device, VkPhysicalDevice physicalDevice, 
                VkSurfaceKHR surface, GLFWwindow* window);
    void recreate(VkDevice device, VkPhysicalDevice physicalDevice, 
                  VkSurfaceKHR surface, GLFWwindow* window);
    VkFormat getFormat() const;
    VkExtent2D getExtent() const;
    const std::vector<VkImageView>& getImageViews() const;
    VkImageView getDepthImageView() const;
    void cleanup(VkDevice device);
};
```

### 4. RenderPipeline (渲染管线)
**职责**: 渲染通道、管线布局、图形管线、帧缓冲
```cpp
class RenderPipeline {
private:
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    std::vector<VkFramebuffer> framebuffers;
    
public:
    void create(VkDevice device, VkFormat swapchainFormat, 
                VkImageView depthView, VkExtent2D extent);
    VkRenderPass getRenderPass() const;
    VkPipeline getPipeline() const;
    VkPipelineLayout getPipelineLayout() const;
    const std::vector<VkFramebuffer>& getFramebuffers() const;
    void recreateFramebuffers(VkDevice device, VkExtent2D extent,
                             const std::vector<VkImageView>& swapchainViews,
                             VkImageView depthView);
    void cleanup(VkDevice device);
};
```

### 5. MeshManager (网格管理)
**职责**: 顶点缓冲、索引缓冲、Uniform 缓冲
```cpp
class MeshManager {
private:
    // 主网格（球体）
    VkBuffer vbo, ibo;
    VkDeviceMemory vboMem, iboMem;
    uint32_t indexCount;
    
    // 地面网格
    VkBuffer planeVbo, planeIbo;
    VkDeviceMemory planeVboMem, planeIboMem;
    uint32_t planeIndexCount;
    
    // Uniform 缓冲
    std::vector<VkBuffer> uboMVPBuf;
    std::vector<VkDeviceMemory> uboMVPMem;
    std::vector<VkBuffer> uboMatBuf;
    std::vector<VkDeviceMemory> uboMatMem;
    
public:
    void createMeshes(VkDevice device, VkPhysicalDevice physicalDevice);
    void createUniformBuffers(VkDevice device, VkPhysicalDevice physicalDevice, 
                             size_t imageCount);
    void updateUniformBuffers(uint32_t imageIndex, const Mat4& model, 
                             const Mat4& view, const Mat4& proj,
                             int materialPreset, bool glassEnabled, bool emissiveEnabled);
    
    // 绑定方法
    void bindSphereMesh(VkCommandBuffer cmd);
    void bindPlaneMesh(VkCommandBuffer cmd);
    void bindMVPDescriptor(VkDescriptorSet set, uint32_t imageIndex);
    void bindMaterialDescriptor(VkDescriptorSet set, uint32_t imageIndex);
    
    uint32_t getSphereIndexCount() const;
    uint32_t getPlaneIndexCount() const;
    
    void cleanup(VkDevice device);
};
```

### 6. DescriptorManager (描述符管理)
**职责**: 描述符布局、描述符池、描述符集
```cpp
class DescriptorManager {
private:
    VkDescriptorSetLayout dslMVP;
    VkDescriptorSetLayout dslMat;
    VkDescriptorPool pool;
    std::vector<VkDescriptorSet> setsMVP;
    std::vector<VkDescriptorSet> setsMat;
    
public:
    void createLayouts(VkDevice device);
    void createPool(VkDevice device, uint32_t imageCount);
    void allocateSets(VkDevice device, uint32_t imageCount);
    void updateSets(uint32_t imageIndex, VkBuffer mvpBuffer, VkBuffer matBuffer);
    
    VkDescriptorSetLayout getMVPLayout() const;
    VkDescriptorSetLayout getMaterialLayout() const;
    VkDescriptorSet getMVPSet(uint32_t index) const;
    VkDescriptorSet getMaterialSet(uint32_t index) const;
    
    void cleanup(VkDevice device);
};
```

### 7. CommandManager (命令管理)
**职责**: 命令池、命令缓冲分配和录制
```cpp
class CommandManager {
private:
    VkCommandPool pool;
    std::vector<VkCommandBuffer> buffers;
    
public:
    void createPool(VkDevice device, uint32_t graphicsFamily);
    void allocateBuffers(VkDevice device, uint32_t count);
    VkCommandBuffer getBuffer(uint32_t index);
    
    // 录制辅助方法
    void beginFrame(VkCommandBuffer cmd);
    void endFrame(VkCommandBuffer cmd);
    
    void cleanup(VkDevice device);
};
```

### 8. SyncManager (同步管理)
**职责**: 信号量、栅栏、帧同步
```cpp
class SyncManager {
private:
    std::vector<VkSemaphore> imageAvailable;
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;
    uint32_t currentFrame;
    uint32_t maxFramesInFlight;
    
public:
    void create(VkDevice device, uint32_t imageCount, uint32_t maxFrames);
    void waitForFence(VkDevice device, uint32_t imageIndex);
    void resetFence(VkDevice device, uint32_t frameIndex);
    
    VkSemaphore getImageAvailableSemaphore(uint32_t frameIndex);
    VkSemaphore getRenderFinishedSemaphore(uint32_t frameIndex);
    VkFence getInFlightFence(uint32_t frameIndex);
    
    uint32_t getCurrentFrame() const;
    void advanceFrame();
    
    void cleanup(VkDevice device);
};
```

### 9. ShadowSystem (阴影系统)
**职责**: 阴影贴图、阴影渲染通道、阴影管线
```cpp
class ShadowSystem {
private:
    static constexpr uint32_t MAP_SIZE = 2048;
    
    VkImage shadowMapImage;
    VkDeviceMemory shadowMapMemory;
    VkImageView shadowMapImageView;
    VkSampler shadowSampler;
    
    VkFramebuffer framebuffer;
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    
    VkDescriptorSetLayout dslShadow;
    VkDescriptorSetLayout dslShadowSampler;
    std::vector<VkBuffer> uboShadowBuf;
    std::vector<VkDeviceMemory> uboShadowMem;
    std::vector<VkDescriptorSet> descSetsShadow;
    std::vector<VkDescriptorSet> descSetsShadowSampler;
    
    VkCommandBuffer commandBuffer;
    
    Mat4 lightView;
    Mat4 lightProj;
    
public:
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                   VkDescriptorSetLayout parentLayout);
    void updateShadowUBO(uint32_t imageIndex, const Mat4& model);
    void recordCommandBuffer(const MeshManager& meshes);
    
    // 获取资源
    VkRenderPass getRenderPass() const;
    VkPipeline getPipeline() const;
    VkPipelineLayout getPipelineLayout() const;
    VkFramebuffer getFramebuffer() const;
    VkImageView getShadowMapView() const;
    VkSampler getSampler() const;
    VkCommandBuffer getCommandBuffer() const;
    VkDescriptorSet getShadowDescriptorSet(uint32_t index) const;
    VkDescriptorSet getSamplerDescriptorSet(uint32_t index) const;
    const Mat4& getLightView() const;
    const Mat4& getLightProj() const;
    
    void cleanup(VkDevice device);
};
```

### 10. MaterialSystem (材质系统)
**职责**: 材质预设、材质状态管理
```cpp
class MaterialSystem {
private:
    int currentPreset;
    bool glassEnabled;
    bool emissiveEnabled;
    
public:
    MaterialSystem();
    
    void nextPreset();
    void toggleGlass();
    void toggleEmissive();
    
    int getPreset() const;
    bool isGlassEnabled() const;
    bool isEmissiveEnabled() const;
};
```

## 重构后的 PBRApp

```cpp
class PBRApp {
private:
    Window window;
    VulkanContext vulkan;
    SwapchainManager swapchain;
    RenderPipeline pipeline;
    MeshManager meshes;
    DescriptorManager descriptors;
    CommandManager commands;
    SyncManager sync;
    ShadowSystem shadow;
    MaterialSystem materials;
    
    bool framebufferResized = false;
    
public:
    void run();
    
private:
    void initialize();
    void mainLoop();
    void drawFrame();
    void recreateSwapchain();
    void cleanup();
};
```

## 实施计划

### 阶段 1: 基础类提取（高优先级，低风险）
1. 创建 `MaterialSystem` 类 - 完全独立，无依赖
2. 创建 `Window` 类 - 包含窗口和相机控制
3. 测试：确保程序能正常启动

### 阶段 2: 核心系统提取（中优先级）
4. 创建 `VulkanContext` 类 - 核心 Vulkan 资源
5. 创建 `SyncManager` 类 - 同步对象管理
6. 测试：确保 Vulkan 初始化正常

### 阶段 3: 渲染系统提取（中优先级）
7. 创建 `SwapchainManager` 类 - Swapchain 管理
8. 创建 `RenderPipeline` 类 - 渲染管线
9. 测试：确保渲染正常

### 阶段 4: 资源管理提取（中优先级）
10. 创建 `MeshManager` 类 - 网格和 Uniform 缓冲
11. 创建 `DescriptorManager` 类 - 描述符管理
12. 测试：确保资源创建和绑定正常

### 阶段 5: 阴影系统提取（高优先级）
13. 创建 `ShadowSystem` 类 - 完整阴影系统
14. 测试：确保阴影渲染正常

### 阶段 6: 命令系统提取（低优先级）
15. 创建 `CommandManager` 类 - 命令缓冲管理
16. 测试：确保命令录制正常

### 阶段 7: 整合和优化
17. 重构 PBRApp 使用组合模式
18. 移除旧的成员变量和函数
19. 全面测试
20. 更新文档

## 文件组织

```
src/
  main.cpp
  pbr_app.h
  pbr_app.cpp
  window.h / window.cpp
  vulkan_context.h / vulkan_context.cpp
  swapchain_manager.h / swapchain_manager.cpp
  render_pipeline.h / render_pipeline.cpp
  mesh_manager.h / mesh_manager.cpp
  descriptor_manager.h / descriptor_manager.cpp
  command_manager.h / command_manager.cpp
  sync_manager.h / sync_manager.cpp
  shadow_system.h / shadow_system.cpp
  material_system.h / material_system.cpp
  pbr_init.cpp
  pbr_swapchain.cpp
  pbr_render.cpp
  pbr_mesh_buffers.cpp
  pbr_descriptors.cpp
  pbr_commands.cpp
  pbr_sync.cpp
  pbr_shadow.cpp
  pbr_runtime.cpp
  pbr_cleanup.cpp
  mesh.cpp
  vulkan_utils.cpp
```

## 风险和缓解措施

### 风险 1: 依赖循环
- **缓解**: 使用依赖注入，避免循环依赖
- **策略**: VulkanContext 作为共享上下文传递

### 风险 2: 性能问题
- **缓解**: 保持现有调用顺序，不改变渲染流程
- **策略**: 使用内联函数和引用传递

### 风险 3: 编译错误
- **缓解**: 每阶段都进行测试
- **策略**: 渐进式重构，保持可运行状态

### 风险 4: 接口设计不当
- **缓解**: 先设计接口，再实现
- **策略**: 参考现有代码的调用模式

## 成功标准

1. ✅ 所有功能正常工作
2. ✅ 代码更易理解和维护
3. ✅ 每个类职责单一
4. ✅ 编译无警告
5. ✅ 性能无明显下降
6. ✅ 代码行数减少或持平

