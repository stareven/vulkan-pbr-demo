# 面向对象设计分析与优化建议

## 当前问题

### 1. 上帝类 (God Class) 问题
PBRApp 承担了过多职责，违反了**单一职责原则 (SRP)**：
- 97 个成员变量
- 33 个成员函数
- 14 个不同的职责领域

### 2. 低内聚高耦合
- 所有功能都集中在一个类中
- 内部模块之间界限不清
- 难以理解各部分之间的关系

### 3. 缺乏封装
- 大量 Vulkan 资源直接作为成员变量
- 没有合理的抽象层次
- 状态管理分散

### 4. 可测试性差
- 无法单独测试某个子系统
- 依赖关系不清晰
- Mock 困难

## 优化方案

### 方案：使用组合和职责分离

```
PBRApp (协调者)
  ├─ Window (窗口管理)
  ├─ VulkanContext (Vulkan 核心上下文)
  ├─ SwapchainManager (Swapchain 管理)
  ├─ RenderPipeline (渲染管线)
  ├─ MeshManager (网格管理)
  ├─ DescriptorManager (描述符管理)
  ├─ CommandManager (命令缓冲管理)
  ├─ SyncManager (同步管理)
  ├─ ShadowSystem (阴影系统)
  ├─ Camera (相机控制)
  └─ MaterialSystem (材质系统)
```

## 具体重构建议

### 1. VulkanContext 类
封装 Vulkan 核心资源：
```cpp
class VulkanContext {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    
public:
    void initialize();
    void cleanup();
    // ...
};
```

### 2. SwapchainManager 类
管理 Swapchain 及其相关资源：
```cpp
class SwapchainManager {
    VkSwapchainKHR swapchain;
    VkFormat format;
    VkExtent2D extent;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    VkImage depthImage;
    VkDeviceMemory depthMemory;
    VkImageView depthImageView;
    
public:
    void create(VkDevice device, VkSurfaceKHR surface);
    void recreate();
    void cleanup();
};
```

### 3. RenderPipeline 类
管理渲染通道和管线：
```cpp
class RenderPipeline {
    VkRenderPass renderPass;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    std::vector<VkFramebuffer> framebuffers;
    
public:
    void create(VkDevice device, VkFormat swapchainFormat);
    void cleanup();
};
```

### 4. MeshManager 类
管理所有网格数据：
```cpp
class MeshManager {
    VkBuffer vbo, ibo;
    VkDeviceMemory vboMem, iboMem;
    uint32_t indexCount;
    
    VkBuffer planeVbo, planeIbo;
    VkDeviceMemory planeVboMem, planeIboMem;
    uint32_t planeIndexCount;
    
public:
    void createMeshes(VkDevice device);
    void cleanup();
};
```

### 5. DescriptorManager 类
管理描述符布局和集合：
```cpp
class DescriptorManager {
    VkDescriptorSetLayout dslMVP, dslMat;
    VkDescriptorPool pool;
    std::vector<VkDescriptorSet> setsMVP, setsMat;
    
public:
    void createLayouts(VkDevice device);
    void createPool(VkDevice device);
    void allocateSets(VkDevice device);
    void cleanup();
};
```

### 6. CommandManager 类
管理命令池和命令缓冲：
```cpp
class CommandManager {
    VkCommandPool pool;
    std::vector<VkCommandBuffer> buffers;
    
public:
    void createPool(VkDevice device);
    void allocateBuffers(VkDevice device);
    void recordCommandBuffer(uint32_t imageIndex);
    void cleanup();
};
```

### 7. SyncManager 类
管理同步对象：
```cpp
class SyncManager {
    std::vector<VkSemaphore> imageAvailable;
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkFence> fences;
    uint32_t currentFrame;
    
public:
    void create(VkDevice device, uint32_t maxFrames);
    void waitForFence(uint32_t frameIndex);
    void resetFence(uint32_t frameIndex);
    void cleanup();
};
```

### 8. ShadowSystem 类
管理阴影映射：
```cpp
class ShadowSystem {
    static constexpr uint32_t MAP_SIZE = 2048;
    VkImage shadowMap;
    VkDeviceMemory shadowMemory;
    VkImageView shadowView;
    VkFramebuffer framebuffer;
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkSampler sampler;
    // ... 其他阴影相关资源
    
public:
    void initialize(VkDevice device);
    void renderShadowMap();
    void cleanup();
};
```

### 9. Camera 类
管理相机状态和输入：
```cpp
class Camera {
    Vec3 position;
    float yaw, pitch;
    bool mouseDown;
    double lastMouseX, lastMouseY;
    
public:
    void update(const InputState& input);
    Mat4 getViewMatrix() const;
    Mat4 getProjectionMatrix(float aspectRatio) const;
};
```

### 10. MaterialSystem 类
管理材质状态：
```cpp
class MaterialSystem {
    int preset;
    bool glassEnabled;
    bool emissiveEnabled;
    
public:
    void nextPreset();
    void toggleGlass();
    void toggleEmissive();
    Material getCurrentMaterial() const;
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
    Camera camera;
    MaterialSystem materials;
    
public:
    void run() {
        initialize();
        mainLoop();
        cleanup();
    }
    
private:
    void initialize() {
        window.create();
        vulkan.initialize();
        swapchain.create(vulkan.device, vulkan.surface);
        pipeline.create(vulkan.device, swapchain.format);
        meshes.createMeshes(vulkan.device);
        descriptors.createLayouts(vulkan.device);
        descriptors.createPool(vulkan.device);
        sync.create(vulkan.device, MAX_FRAMES);
        shadow.initialize(vulkan.device);
        commands.createPool(vulkan.device);
        commands.allocateBuffers(vulkan.device);
    }
    
    void mainLoop() {
        while (!window.shouldClose()) {
            window.pollEvents();
            camera.update(window.input);
            renderFrame();
        }
    }
    
    void renderFrame() {
        sync.waitForFence(sync.currentFrame);
        // ... 渲染逻辑
    }
    
    void cleanup() {
        shadow.cleanup();
        sync.cleanup();
        commands.cleanup();
        descriptors.cleanup();
        meshes.cleanup();
        pipeline.cleanup();
        swapchain.cleanup();
        vulkan.cleanup();
        window.destroy();
    }
};
```

## 优化收益

### 1. 单一职责原则
- 每个类只负责一个明确的职责
- 代码更易理解和维护

### 2. 高内聚低耦合
- 相关功能集中在一个类中
- 类之间通过明确的接口交互

### 3. 更好的封装
- 内部实现细节对外隐藏
- 减少意外修改的风险

### 4. 可测试性提升
- 可以单独测试每个子系统
- 容易创建 Mock 对象

### 5. 可复用性增强
- 独立的类可以在其他项目中复用
- 例如：VulkanContext、Camera、SyncManager

### 6. 团队协作友好
- 不同开发者可以并行开发不同的类
- 减少代码冲突

## 实施建议

1. **渐进式重构**：不要一次性全部重构，逐步提取类
2. **保持接口稳定**：重构过程中保持外部接口不变
3. **充分测试**：每次重构后都要测试确保功能正常
4. **文档更新**：及时更新类图和接口文档

## 优先级建议

**高优先级**（最容易且收益最大）：
1. Camera 类 - 独立性强，容易提取
2. MaterialSystem 类 - 逻辑独立
3. SyncManager 类 - 职责明确

**中优先级**：
4. ShadowSystem 类 - 相对独立
5. MeshManager 类 - 数据管理清晰
6. CommandManager 类 - 逻辑独立

**低优先级**（需要更多设计）：
7. SwapchainManager 类
8. RenderPipeline 类
9. DescriptorManager 类
10. VulkanContext 类

