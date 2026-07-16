// ============================================================================
// Vulkan PBR Demo — 基于物理的渲染 (球体 + 多光源)
// ============================================================================
// 基于上一个三角形 Demo 改造, 新增:
//   - Vertex Buffer (球体网格)
//   - Depth Buffer (深度测试)
//   - Uniform Buffer (MVP 矩阵 + 材质参数)
//   - Descriptor Set (Shader 资源绑定)
//   - PBR Shader (Cook-Torrance BRDF)
//   - 多光源渲染
//
// 编译:
//   glslc -fvertex shader.vert -o shader.vert.spv
//   glslc -ffragment shader.frag -o shader.frag.spv
//   g++ vulkan_pbr.cpp -o vulkan_pbr -std=c++17 -lvulkan -lglfw -lm
//
// 运行:
//   ./vulkan_pbr
//   → WASD 移动相机, QE 升降, 鼠标左键旋转
//   → 空格: 切换材质 (金属/非金属交替)
// ============================================================================

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

// ============================================================================
// 常量
// ============================================================================
const uint32_t WIDTH  = 1280;
const uint32_t HEIGHT = 720;
const char*    TITLE  = "Vulkan PBR Demo";

const bool          ENABLE_VALIDATION = true;
const uint32_t      MAX_FRAMES_IN_FLIGHT = 2;
const std::vector<const char*> VALIDATION_LAYERS = {"VK_LAYER_KHRONOS_validation"};
const std::vector<const char*> DEVICE_EXTENSIONS = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

// ============================================================================
// 数学工具 (简易矩阵/向量, 避免引入外部库)
// ============================================================================
struct Vec3 {
    float x, y, z;
    Vec3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
    Vec3 operator+(const Vec3& o)const{return{x+o.x,y+o.y,z+o.z};}
    Vec3 operator-(const Vec3& o)const{return{x-o.x,y-o.y,z-o.z};}
    Vec3 operator*(float s)const{return{x*s,y*s,z*s};}
    float dot(const Vec3& o)const{return x*o.x+y*o.y+z*o.z;}
    Vec3  cross(const Vec3& o)const{return{y*o.z-z*o.y,z*o.x-x*o.z,x*o.y-y*o.x};}
    float length()const{return std::sqrt(x*x+y*y+z*z);}
    Vec3  normalize()const{float l=length();return l>0?Vec3{x/l,y/l,z/l}:Vec3{};}
};

struct Mat4 {
    float m[4][4];
    static Mat4 identity() {
        Mat4 r{}; r.m[0][0]=r.m[1][1]=r.m[2][2]=r.m[3][3]=1.0f; return r;
    }
    static Mat4 perspective(float fov, float aspect, float near, float far) {
        Mat4 r{};
        float f = 1.0f / std::tan(fov * 0.5f);
        r.m[0][0] = f / aspect;
        r.m[1][1] = f;
        r.m[2][2] = far / (near - far);
        r.m[2][3] = -1.0f;
        r.m[3][2] = (far * near) / (near - far);
        return r;
    }
    static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
        Vec3 f = (center - eye).normalize();
        Vec3 r = f.cross(up).normalize();
        Vec3 u = r.cross(f);
        Mat4 res = identity();
        res.m[0][0]=r.x; res.m[0][1]=r.y; res.m[0][2]=r.z; res.m[0][3]=-r.dot(eye);
        res.m[1][0]=u.x; res.m[1][1]=u.y; res.m[1][2]=u.z; res.m[1][3]=-u.dot(eye);
        res.m[2][0]=-f.x;res.m[2][1]=-f.y;res.m[2][2]=-f.z;res.m[2][3]= f.dot(eye);
        return res;
    }
    static Mat4 translation(Vec3 t) {
        Mat4 r = identity();
        r.m[0][3] = t.x; r.m[1][3] = t.y; r.m[2][3] = t.z;
        return r;
    }
    static Mat4 scale(float s) {
        Mat4 r = identity();
        r.m[0][0] = r.m[1][1] = r.m[2][2] = s;
        return r;
    }
};

// ============================================================================
// Vertex / UBO 结构
// ============================================================================
struct Vertex {
    Vec3 pos, normal;
    Vec2 uv;
};

struct Vec2 { float x, y; };

struct UBO_MVP {
    Mat4 model, view, proj;
    Vec3 cameraPos;
};

struct UBOLight {
    Vec3 position;
    Vec3 color;
    float intensity;
};

struct UBO_Material {
    Vec3 albedo;
    float metallic;
    float roughness;
    float ao;
    float ior;        // 折射率 (玻璃 ~1.5)
    float opacity;    // 透明度 [0,1] (玻璃 < 1.0)
    int   _pad0;
    UBOLight lights[4];
    Vec3 ambientLight;
    Vec3 cameraPos;
};

// ============================================================================
// 生成球体网格
// ============================================================================
std::vector<Vertex> generateSphere(int rings = 32, int sectors = 64) {
    std::vector<Vertex> verts;
    const float R = 1.0f;
    for (int r = 0; r <= rings; ++r) {
        float phi = M_PI * float(r) / rings;
        for (int s = 0; s <= sectors; ++s) {
            float theta = 2.0f * M_PI * float(s) / sectors;
            float x = std::sin(phi) * std::cos(theta);
            float y = std::cos(phi);
            float z = std::sin(phi) * std::sin(theta);
            Vec3 p{x*R, y*R, z*R};
            Vec3 n = p.normalize();
            Vec2 uv{float(s)/sectors, float(r)/rings};
            verts.push_back({p, n, uv});
        }
    }
    return verts;
}

std::vector<uint32_t> generateSphereIndices(int rings, int sectors) {
    std::vector<uint32_t> idx;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < sectors; ++s) {
            uint32_t a = r * (sectors+1) + s;
            uint32_t b = a + sectors + 1;
            idx.push_back(a); idx.push_back(b); idx.push_back(a+1);
            idx.push_back(a+1); idx.push_back(b); idx.push_back(b+1);
        }
    }
    return idx;
}

// ============================================================================
// 工具函数
// ============================================================================
std::vector<char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::ate | std::ios::binary);
    if (!f) throw std::runtime_error("open failed: " + path);
    std::vector<char> buf(f.tellg());
    f.seekg(0); f.read(buf.data(), buf.size());
    return buf;
}

bool checkLayerSupport() {
    uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> layers(n);
    vkEnumerateInstanceLayerProperties(&n, layers.data());
    for (const char* l : VALIDATION_LAYERS) {
        bool found = false;
        for (auto& p : layers) if (std::strcmp(l, p.layerName) == 0) { found = true; break; }
        if (!found) return false;
    }
    return true;
}

VkShaderModule makeShaderModule(VkDevice dev, const std::vector<char>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m;
    if (vkCreateShaderModule(dev, &ci, nullptr, &m) != VK_SUCCESS)
        throw std::runtime_error("shader module creation failed");
    return m;
}

struct QueueFamilies {
    std::optional<uint32_t> gfx, present;
    bool complete() const { return gfx && present; }
};

QueueFamilies findQueues(VkPhysicalDevice pd, VkSurfaceKHR surf) {
    QueueFamilies q;
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, nullptr);
    std::vector<VkQueueFamilyProperties> props(n);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, props.data());
    for (uint32_t i = 0; i < n; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) q.gfx = i;
        VkBool32 sup = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surf, &sup);
        if (sup) q.present = i;
    }
    return q;
}

// ============================================================================
// Buffer 辅助: 创建 VkBuffer + VkDeviceMemory
// ============================================================================
VkDeviceSize alignUp(VkDeviceSize sz, VkDeviceSize align) {
    return (sz + align - 1) & ~(align - 1);
}

void createBuffer(VkDevice dev, VkPhysicalDevice pd,
                  VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags memProp,
                  VkBuffer& buf, VkDeviceMemory& mem) {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &bi, nullptr, &buf) != VK_SUCCESS)
        throw std::runtime_error("buffer creation failed");

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, buf, &mr);

    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);

    uint32_t typeIdx = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((mr.memoryTypeBits & (1 << i)) &&
            (mp.memoryTypes[i].propertyFlags & memProp) == memProp) {
            typeIdx = i; break;
        }
    }
    if (typeIdx == UINT32_MAX) throw std::runtime_error("memory type not found");

    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = typeIdx;
    if (vkAllocateMemory(dev, &ai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("memory allocation failed");
    vkBindBufferMemory(dev, buf, mem, 0);
}

void copyBuffer(VkDevice dev, VkQueue queue, VkCommandPool pool,
                VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(dev, &ai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(dev, pool, 1, &cmd);
}

// ============================================================================
// 主应用
// ============================================================================
class PBRApp {
public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow* window = nullptr;

    // Vulkan 核心
    VkInstance       instance = VK_NULL_HANDLE;
    VkSurfaceKHR     surface  = VK_NULL_HANDLE;
    VkPhysicalDevice pd        = VK_NULL_HANDLE;
    VkDevice         device    = VK_NULL_HANDLE;
    VkQueue          gfxQueue, presQueue;

    // Swapchain
    VkSwapchainKHR                swapchain = VK_NULL_HANDLE;
    VkFormat                      scFormat;
    VkExtent2D                    scExtent;
    std::vector<VkImage>          scImages;
    std::vector<VkImageView>      scImageViews;

    // Depth buffer
    VkImage          depthImage;
    VkDeviceMemory   depthMemory;
    VkImageView      depthImageView;

    // Render Pass
    VkRenderPass     renderPass;

    // Pipeline
    VkPipelineLayout pipelineLayout;
    VkPipeline       pipeline;

    // Framebuffers
    std::vector<VkFramebuffer> framebuffers;

    // Command pool & buffers
    VkCommandPool                cmdPool;
    std::vector<VkCommandBuffer> cmdBuffers;

    // Sync
    std::vector<VkSemaphore> semImgAvail, semRendDone;
    std::vector<VkFence>     fences;
    uint32_t frameIdx = 0;

    // Mesh (球体)
    VkBuffer       vbo, ibo;
    VkDeviceMemory vboMem, iboMem;
    uint32_t       indexCount = 0;

    // Uniform buffers
    std::vector<VkBuffer>       uboMVPBuf;
    std::vector<VkDeviceMemory> uboMVPMem;
    std::vector<VkBuffer>       uboMatBuf;
    std::vector<VkDeviceMemory> uboMatMem;

    // Descriptor
    VkDescriptorSetLayout        dslMVP, dslMat;
    VkDescriptorPool             descPool;
    std::vector<VkDescriptorSet> descSetsMVP, descSetsMat;

    // Camera
    Vec3 camPos{0, 0, 4};
    float camYaw = 0, camPitch = 0;
    bool  leftDown = false;
    double lastMX = 0, lastMY = 0;

    // Material preset toggle
    int matPreset = 0;
    // 玻璃效果开关
    bool glassEnabled = false;

    bool fbResized = false;

    // ------------------------------------------------------------------
    // Window
    // ------------------------------------------------------------------
    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        window = glfwCreateWindow(WIDTH, HEIGHT, TITLE, nullptr, nullptr);

        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int){
            reinterpret_cast<PBRApp*>(glfwGetWindowUserPointer(w))->fbResized = true;
        });
        glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y){
            auto s = reinterpret_cast<PBRApp*>(glfwGetWindowUserPointer(w));
            if (s->leftDown) {
                double dx = x - s->lastMX, dy = y - s->lastMY;
                s->camYaw   += dx * 0.005f;
                s->camPitch -= dy * 0.005f;
                s->camPitch = std::clamp(s->camPitch, -1.5f, 1.5f);
            }
            s->lastMX = x; s->lastMY = y;
        });
        glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int btn, int act, int){
            auto s = reinterpret_cast<PBRApp*>(glfwGetWindowUserPointer(w));
            if (btn == GLFW_MOUSE_BUTTON_LEFT) {
                s->leftDown = (act == GLFW_PRESS);
                glfwGetCursorPos(w, &s->lastMX, &s->lastMY);
            }
        });
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    // ------------------------------------------------------------------
    // Vulkan init
    // ------------------------------------------------------------------
    void initVulkan() {
        if (ENABLE_VALIDATION && !checkLayerSupport())
            throw std::runtime_error("validation layer unavailable");

        // Instance
        VkApplicationInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName = "PBR Demo";
        ai.apiVersion = VK_API_VERSION_1_1;

        uint32_t extCount = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
        std::vector<const char*> exts(glfwExts, glfwExts + extCount);

        VkInstanceCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ici.pApplicationInfo = &ai;
        ici.enabledExtensionCount = (uint32_t)exts.size();
        ici.ppEnabledExtensionNames = exts.data();
        if (ENABLE_VALIDATION) {
            ici.enabledLayerCount = (uint32_t)VALIDATION_LAYERS.size();
            ici.ppEnabledLayerNames = VALIDATION_LAYERS.data();
        }
        if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS)
            throw std::runtime_error("instance creation failed");

        // Surface
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
            throw std::runtime_error("surface creation failed");

        // Physical device
        {
            uint32_t n = 0;
            vkEnumeratePhysicalDevices(instance, &n, nullptr);
            std::vector<VkPhysicalDevice> devs(n);
            vkEnumeratePhysicalDevices(instance, &n, devs.data());
            for (auto d : devs) {
                auto q = findQueues(d, surface);
                if (!q.complete()) continue;
                uint32_t eN = 0;
                vkEnumerateDeviceExtensionProperties(d, nullptr, &eN, nullptr);
                std::vector<VkExtensionProperties> eP(eN);
                vkEnumerateDeviceExtensionProperties(d, nullptr, &eN, eP.data());
                std::set<std::string> req(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());
                for (auto& e : eP) req.erase(e.extensionName);
                if (req.empty()) { pd = d; break; }
            }
            if (!pd) throw std::runtime_error("no suitable GPU");
        }

        // Logical device
        {
            auto q = findQueues(pd, surface);
            std::set<uint32_t> uq{q.gfx.value(), q.present.value()};
            std::vector<VkDeviceQueueCreateInfo> qci;
            float pri = 1.0f;
            for (uint32_t i : uq) {
                VkDeviceQueueCreateInfo c{};
                c.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                c.queueFamilyIndex = i;
                c.queueCount = 1;
                c.pQueuePriorities = &pri;
                qci.push_back(c);
            }
            VkPhysicalDeviceFeatures feat{};
            VkDeviceCreateInfo dci{};
            dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            dci.queueCreateInfoCount = (uint32_t)qci.size();
            dci.pQueueCreateInfos = qci.data();
            dci.pEnabledFeatures = &feat;
            dci.enabledExtensionCount = (uint32_t)DEVICE_EXTENSIONS.size();
            dci.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();
            if (vkCreateDevice(pd, &dci, nullptr, &device) != VK_SUCCESS)
                throw std::runtime_error("logical device creation failed");
            vkGetDeviceQueue(device, q.gfx.value(), 0, &gfxQueue);
            vkGetDeviceQueue(device, q.present.value(), 0, &presQueue);
        }

        createSwapchain();
        createImageViews();
        createDepthBuffer();
        createRenderPass();
        createDescriptorLayouts();
        createGraphicsPipeline();
        createFramebuffers();
        createCommandPool();
        createMesh();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();
    }

    // ------------------------------------------------------------------
    // Swapchain
    // ------------------------------------------------------------------
    void createSwapchain() {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surface, &caps);

        uint32_t fmtN;
        vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &fmtN, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(fmtN);
        vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &fmtN, fmts.data());

        uint32_t pmN;
        vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &pmN, nullptr);
        std::vector<VkPresentModeKHR> pms(pmN);
        vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &pmN, pms.data());

        VkSurfaceFormatKHR sf = fmts[0];
        for (auto& f : fmts)
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { sf = f; break; }

        VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
        for (auto m : pms) if (m == VK_PRESENT_MODE_MAILBOX_KHR) { pm = m; break; }

        VkExtent2D ext = caps.currentExtent;
        if (ext.width == UINT32_MAX) {
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            ext.width = std::clamp((uint32_t)w, caps.minImageExtent.width, caps.maxImageExtent.width);
            ext.height = std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height);
        }

        uint32_t imgN = caps.minImageCount + 1;
        if (caps.maxImageCount && imgN > caps.maxImageCount) imgN = caps.maxImageCount;

        auto q = findQueues(pd, surface);
        uint32_t families[] = {q.gfx.value(), q.present.value()};

        VkSwapchainCreateInfoKHR sci{};
        sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        sci.surface = surface;
        sci.minImageCount = imgN;
        sci.imageFormat = sf.format;
        sci.imageColorSpace = sf.colorSpace;
        sci.imageExtent = ext;
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        sci.imageSharingMode = (q.gfx != q.present) ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
        if (sci.imageSharingMode == VK_SHARING_MODE_CONCURRENT) {
            sci.queueFamilyIndexCount = 2;
            sci.pQueueFamilyIndices = families;
        }
        sci.preTransform = caps.currentTransform;
        sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sci.presentMode = pm;
        sci.clipped = VK_TRUE;
        sci.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain) != VK_SUCCESS)
            throw std::runtime_error("swapchain creation failed");

        vkGetSwapchainImagesKHR(device, swapchain, &imgN, nullptr);
        scImages.resize(imgN);
        vkGetSwapchainImagesKHR(device, swapchain, &imgN, scImages.data());
        scFormat = sf.format;
        scExtent = ext;
    }

    void createImageViews() {
        scImageViews.resize(scImages.size());
        for (size_t i = 0; i < scImages.size(); ++i) {
            VkImageViewCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ci.image = scImages[i];
            ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ci.format = scFormat;
            ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if (vkCreateImageView(device, &ci, nullptr, &scImageViews[i]) != VK_SUCCESS)
                throw std::runtime_error("image view creation failed");
        }
    }

    // ------------------------------------------------------------------
    // Depth buffer (VK_FORMAT_D32_SFLOAT)
    // ------------------------------------------------------------------
    void createDepthBuffer() {
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = VK_FORMAT_D32_SFLOAT;
        ii.extent = {scExtent.width, scExtent.height, 1};
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &ii, nullptr, &depthImage) != VK_SUCCESS)
            throw std::runtime_error("depth image creation failed");

        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(device, depthImage, &mr);
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(pd, &mp);
        uint32_t ti = UINT32_MAX;
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            if ((mr.memoryTypeBits & (1 << i)) &&
                (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                ti = i; break;
            }
        }
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = ti;
        if (vkAllocateMemory(device, &ai, nullptr, &depthMemory) != VK_SUCCESS ||
            vkBindImageMemory(device, depthImage, depthMemory, 0) != VK_SUCCESS)
            throw std::runtime_error("depth memory setup failed");

        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = depthImage;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_D32_SFLOAT;
        vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device, &vi, nullptr, &depthImageView) != VK_SUCCESS)
            throw std::runtime_error("depth image view failed");
    }

    // ------------------------------------------------------------------
    // Render pass: color + depth
    // ------------------------------------------------------------------
    void createRenderPass() {
        std::array<VkAttachmentDescription, 2> att{};
        // 颜色
        att[0].format = scFormat;
        att[0].samples = VK_SAMPLE_COUNT_1_BIT;
        att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        // 深度
        att[1].format = VK_FORMAT_D32_SFLOAT;
        att[1].samples = VK_SAMPLE_COUNT_1_BIT;
        att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &colorRef;
        sub.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpi{};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpi.attachmentCount = (uint32_t)att.size();
        rpi.pAttachments = att.data();
        rpi.subpassCount = 1;
        rpi.pSubpasses = &sub;
        rpi.dependencyCount = 1;
        rpi.pDependencies = &dep;

        if (vkCreateRenderPass(device, &rpi, nullptr, &renderPass) != VK_SUCCESS)
            throw std::runtime_error("render pass creation failed");
    }

    // ------------------------------------------------------------------
    // Descriptor set layouts
    // ------------------------------------------------------------------
    // set 0 = MVP (per frame)
    // set 1 = Material (per frame)
    void createDescriptorLayouts() {
        // MVP
        VkDescriptorSetLayoutBinding b0{};
        b0.binding = 0;
        b0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b0.descriptorCount = 1;
        b0.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 1;
        li.pBindings = &b0;
        if (vkCreateDescriptorSetLayout(device, &li, nullptr, &dslMVP) != VK_SUCCESS)
            throw std::runtime_error("dsl mvp failed");

        // Material
        VkDescriptorSetLayoutBinding b1{};
        b1.binding = 0;
        b1.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b1.descriptorCount = 1;
        b1.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        li.pBindings = &b1;
        if (vkCreateDescriptorSetLayout(device, &li, nullptr, &dslMat) != VK_SUCCESS)
            throw std::runtime_error("dsl mat failed");
    }

    // ------------------------------------------------------------------
    // Graphics pipeline
    // ------------------------------------------------------------------
    void createGraphicsPipeline() {
        auto vs = readFile("shader.vert.spv");
        auto fs = readFile("shader.frag.spv");
        auto vsm = makeShaderModule(device, vs);
        auto fsm = makeShaderModule(device, fs);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vsm;
        stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fsm;
        stages[1].pName = "main";

        // Vertex input
        VkVertexInputBindingDescription bib = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription aib[3] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex, uv)},
        };
        VkPipelineVertexInputStateCreateInfo vici{};
        vici.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vici.vertexBindingDescriptionCount = 1;
        vici.pVertexBindingDescriptions = &bib;
        vici.vertexAttributeDescriptionCount = 3;
        vici.pVertexAttributeDescriptions = aib;

        VkPipelineInputAssemblyStateCreateInfo iaci{};
        iaci.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        iaci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vsci{};
        vsci.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vsci.viewportCount = vsci.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rsci{};
        rsci.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rsci.polygonMode = VK_POLYGON_MODE_FILL;
        rsci.lineWidth = 1.0f;
        rsci.cullMode = VK_CULL_MODE_BACK_BIT;
        rsci.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo msci{};
        msci.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        msci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo dsci{};
        dsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        dsci.depthTestEnable = dsci.depthWriteEnable = VK_TRUE;
        dsci.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        // 启用 Alpha 混合 (支持透明材质如玻璃)
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cbci{};
        cbci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbci.attachmentCount = 1;
        cbci.pAttachments = &cba;

        std::vector<VkDynamicState> ds = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dsci2{};
        dsci2.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dsci2.dynamicStateCount = (uint32_t)ds.size();
        dsci2.pDynamicStates = ds.data();

        VkDescriptorSetLayout layouts[] = {dslMVP, dslMat};
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 2;
        pli.pSetLayouts = layouts;
        if (vkCreatePipelineLayout(device, &pli, nullptr, &pipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("pipeline layout failed");

        VkGraphicsPipelineCreateInfo gpi{};
        gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpi.stageCount = 2;
        gpi.pStages = stages;
        gpi.pVertexInputState   = &vici;
        gpi.pInputAssemblyState = &iaci;
        gpi.pViewportState      = &vsci;
        gpi.pRasterizationState = &rsci;
        gpi.pMultisampleState   = &msci;
        gpi.pDepthStencilState  = &dsci;
        gpi.pColorBlendState    = &cbci;
        gpi.pDynamicState       = &dsci2;
        gpi.layout = pipelineLayout;
        gpi.renderPass = renderPass;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpi, nullptr, &pipeline) != VK_SUCCESS)
            throw std::runtime_error("graphics pipeline failed");

        vkDestroyShaderModule(device, vsm, nullptr);
        vkDestroyShaderModule(device, fsm, nullptr);
    }

    void createFramebuffers() {
        framebuffers.resize(scImageViews.size());
        for (size_t i = 0; i < scImageViews.size(); ++i) {
            VkImageView att[] = {scImageViews[i], depthImageView};
            VkFramebufferCreateInfo fbi{};
            fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbi.renderPass = renderPass;
            fbi.attachmentCount = 2;
            fbi.pAttachments = att;
            fbi.width = scExtent.width;
            fbi.height = scExtent.height;
            fbi.layers = 1;
            if (vkCreateFramebuffer(device, &fbi, nullptr, &framebuffers[i]) != VK_SUCCESS)
                throw std::runtime_error("framebuffer failed");
        }
    }

    void createCommandPool() {
        auto q = findQueues(pd, surface);
        VkCommandPoolCreateInfo cpi{};
        cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpi.queueFamilyIndex = q.gfx.value();
        if (vkCreateCommandPool(device, &cpi, nullptr, &cmdPool) != VK_SUCCESS)
            throw std::runtime_error("command pool failed");
    }

    // ------------------------------------------------------------------
    // Mesh: 球体 VBO + IBO (with staging buffer)
    // ------------------------------------------------------------------
    void createMesh() {
        auto verts = generateSphere(32, 64);
        auto idxs  = generateSphereIndices(32, 64);
        indexCount = (uint32_t)idxs.size();

        VkDeviceSize vboSz = verts.size() * sizeof(Vertex);
        VkDeviceSize iboSz = idxs.size()  * sizeof(uint32_t);

        // Staging buffer (host visible)
        VkBuffer stg;
        VkDeviceMemory stgMem;
        createBuffer(device, pd, vboSz + iboSz,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                     stg, stgMem);
        void* mapped;
        vkMapMemory(device, stgMem, 0, vboSz + iboSz, 0, &mapped);
        std::memcpy(mapped, verts.data(), vboSz);
        std::memcpy((char*)mapped + vboSz, idxs.data(), iboSz);
        vkUnmapMemory(device, stgMem);

        // Device-local VBO
        createBuffer(device, pd, vboSz,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     vbo, vboMem);
        // Device-local IBO
        createBuffer(device, pd, iboSz,
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     ibo, iboMem);

        copyBuffer(device, gfxQueue, cmdPool, stg, vbo, vboSz);
        copyBuffer(device, gfxQueue, cmdPool, stg, ibo, iboSz);

        vkDestroyBuffer(device, stg, nullptr);
        vkFreeMemory(device, stgMem, nullptr);
    }

    // ------------------------------------------------------------------
    // Uniform buffers (per-frame)
    // ------------------------------------------------------------------
    void createUniformBuffers() {
        uboMVPBuf.resize(MAX_FRAMES_IN_FLIGHT);
        uboMVPMem.resize(MAX_FRAMES_IN_FLIGHT);
        uboMatBuf.resize(MAX_FRAMES_IN_FLIGHT);
        uboMatMem.resize(MAX_FRAMES_IN_FLIGHT);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            createBuffer(device, pd, sizeof(UBO_MVP),
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                         uboMVPBuf[i], uboMVPMem[i]);
            createBuffer(device, pd, sizeof(UBO_Material),
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                         uboMatBuf[i], uboMatMem[i]);
        }
    }

    // ------------------------------------------------------------------
    // Descriptor pool & sets
    // ------------------------------------------------------------------
    void createDescriptorPool() {
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)(MAX_FRAMES_IN_FLIGHT * 2)}
        };
        VkDescriptorPoolCreateInfo dpi{};
        dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpi.maxSets = MAX_FRAMES_IN_FLIGHT * 2;
        dpi.poolSizeCount = 1;
        dpi.pPoolSizes = sizes;
        if (vkCreateDescriptorPool(device, &dpi, nullptr, &descPool) != VK_SUCCESS)
            throw std::runtime_error("descriptor pool failed");
    }

    void createDescriptorSets() {
        descSetsMVP.resize(MAX_FRAMES_IN_FLIGHT);
        descSetsMat.resize(MAX_FRAMES_IN_FLIGHT);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            VkDescriptorSetAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = descPool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &dslMVP;
            if (vkAllocateDescriptorSets(device, &ai, &descSetsMVP[i]) != VK_SUCCESS)
                throw std::runtime_error("alloc mvp desc set failed");

            VkDescriptorBufferInfo bi{};
            bi.buffer = uboMVPBuf[i];
            bi.range  = sizeof(UBO_MVP);
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = descSetsMVP[i];
            w.dstBinding = 0;
            w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w.descriptorCount = 1;
            w.pBufferInfo = &bi;
            vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);

            ai.pSetLayouts = &dslMat;
            if (vkAllocateDescriptorSets(device, &ai, &descSetsMat[i]) != VK_SUCCESS)
                throw std::runtime_error("alloc mat desc set failed");

            bi.buffer = uboMatBuf[i];
            bi.range  = sizeof(UBO_Material);
            w.dstSet = descSetsMat[i];
            w.pBufferInfo = &bi;
            vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        }
    }

    // ------------------------------------------------------------------
    // Command buffers
    // ------------------------------------------------------------------
    void createCommandBuffers() {
        cmdBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = cmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = (uint32_t)cmdBuffers.size();
        if (vkAllocateCommandBuffers(device, &ai, cmdBuffers.data()) != VK_SUCCESS)
            throw std::runtime_error("command buffer alloc failed");
    }

    void recordCommandBuffer(uint32_t imgIdx) {
        VkCommandBuffer cmd = cmdBuffers[frameIdx];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &bi);

        VkClearValue clears[2];
        clears[0].color = {{0.02f, 0.02f, 0.03f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rpi{};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpi.renderPass = renderPass;
        rpi.framebuffer = framebuffers[imgIdx];
        rpi.renderArea.extent = scExtent;
        rpi.clearValueCount = 2;
        rpi.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkViewport vp{0, 0, (float)scExtent.width, (float)scExtent.height, 0, 1};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{{0,0}, scExtent};
        vkCmdSetScissor(cmd, 0, 1, &sc);

        VkBuffer vBufs[] = {vbo};
        VkDeviceSize offs[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vBufs, offs);
        vkCmdBindIndexBuffer(cmd, ibo, 0, VK_INDEX_TYPE_UINT32);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                0, 1, &descSetsMVP[frameIdx], 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                1, 1, &descSetsMat[frameIdx], 0, nullptr);

        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

    // ------------------------------------------------------------------
    // Sync objects
    // ------------------------------------------------------------------
    void createSyncObjects() {
        semImgAvail.resize(MAX_FRAMES_IN_FLIGHT);
        semRendDone.resize(MAX_FRAMES_IN_FLIGHT);
        fences.resize(MAX_FRAMES_IN_FLIGHT);
        VkSemaphoreCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (vkCreateSemaphore(device, &sci, nullptr, &semImgAvail[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device, &sci, nullptr, &semRendDone[i]) != VK_SUCCESS ||
                vkCreateFence(device, &fci, nullptr, &fences[i]) != VK_SUCCESS)
                throw std::runtime_error("sync object creation failed");
        }
    }

    // ------------------------------------------------------------------
    // Update UBOs each frame
    // ------------------------------------------------------------------
    void updateUBOs() {
        // Camera
        float cx = std::cos(camPitch) * std::sin(camYaw);
        float cy = std::sin(camPitch);
        float cz = std::cos(camPitch) * std::cos(camYaw);
        Vec3 camDir{cx, cy, cz};
        Vec3 target = camPos + camDir;
        Vec3 up{0, 1, 0};

        Mat4 view = Mat4::lookAt(camPos, target, up);
        float aspect = (float)scExtent.width / scExtent.height;
        Mat4 proj = Mat4::perspective(45.0f * M_PI / 180.0f, aspect, 0.1f, 100.0f);

        // 多个球体 (这里只渲染一个在 (0,0,0))
        Mat4 model = Mat4::identity();

        UBO_MVP mvp{model, view, proj, camPos};
        void* p;
        vkMapMemory(device, uboMVPMem[frameIdx], 0, sizeof(mvp), 0, &p);
        std::memcpy(p, &mvp, sizeof(mvp));
        vkUnmapMemory(device, uboMVPMem[frameIdx]);

        // Material: 根据预设切换
        struct MatPreset { Vec3 albedo; float metallic; float roughness; };
        MatPreset presets[] = {
            {{0.95f, 0.35f, 0.10f}, 0.0f, 0.7f},   // 红色塑料
            {{0.95f, 0.93f, 0.88f}, 1.0f, 0.1f},   // 银
            {{1.00f, 0.77f, 0.33f}, 1.0f, 0.3f},   // 金
            {{0.97f, 0.96f, 0.91f}, 1.0f, 0.2f},   // 铝
            {{0.30f, 0.85f, 0.39f}, 0.0f, 0.4f},   // 绿色塑料
            {{0.50f, 0.50f, 0.50f}, 1.0f, 0.5f},   // 铁
            {{0.98f, 0.99f, 1.00f}, 0.0f, 0.05f},  // 玻璃 (配合 ior/opacity)
        };
        auto& pr = presets[matPreset % 7];

        UBO_Material mat{};
        mat.albedo = pr.albedo;
        mat.metallic = pr.metallic;
        mat.roughness = pr.roughness;
        mat.ao = 1.0f;
        mat.ior = 1.5f;
        mat.opacity = 1.0f;
        // 玻璃预设 (index 6)
        if ((matPreset % 7) == 6 && glassEnabled) {
            mat.ior = 1.52f;
            mat.opacity = 0.3f;
        }
        mat.cameraPos = camPos;
        mat.ambientLight = {0.03f, 0.03f, 0.03f};
        mat.lights[0] = {{10,  10,  10}, {300, 300, 300}, 1.0f};
        mat.lights[1] = {{-10, 10,  10}, {300, 100, 100}, 1.0f};
        mat.lights[2] = {{10, -10, -10}, {100, 300, 100}, 1.0f};
        mat.lights[3] = {{-10,-10, -10}, {100, 100, 300}, 1.0f};

        vkMapMemory(device, uboMatMem[frameIdx], 0, sizeof(mat), 0, &p);
        std::memcpy(p, &mat, sizeof(mat));
        vkUnmapMemory(device, uboMatMem[frameIdx]);
    }

    // ------------------------------------------------------------------
    // Main loop
    // ------------------------------------------------------------------
    void mainLoop() {
        auto t0 = std::chrono::high_resolution_clock::now();
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            // 键盘控制
            float dt = 0.016f;
            float speed = 5.0f * dt;
            Vec3 fwd = (Vec3{std::cos(camPitch)*std::sin(camYaw),
                             std::sin(camPitch),
                             std::cos(camPitch)*std::cos(camYaw)}).normalize();
            Vec3 right = fwd.cross({0,1,0}).normalize();
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camPos = camPos + fwd * speed;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camPos = camPos - fwd * speed;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camPos = camPos - right * speed;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camPos = camPos + right * speed;
            if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) camPos.y -= speed;
            if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) camPos.y += speed;

            // 切换玻璃效果 (G 键)
            static bool gLast = false;
            bool gNow = glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS;
            if (gNow && !gLast) {
                glassEnabled = !glassEnabled;
                std::cout << "Glass: " << (glassEnabled ? "ON" : "OFF") << "\n";
            }
            gLast = gNow;

            drawFrame();
        }
        vkDeviceWaitIdle(device);
    }

    void drawFrame() {
        vkWaitForFences(device, 1, &fences[frameIdx], VK_TRUE, UINT64_MAX);

        uint32_t imgIdx;
        VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                           semImgAvail[frameIdx], VK_NULL_HANDLE, &imgIdx);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || fbResized) {
            fbResized = false;
            recreateSwapchain();
            return;
        }

        updateUBOs();
        recordCommandBuffer(imgIdx);

        vkResetFences(device, 1, &fences[frameIdx]);

        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphore wait[] = {semImgAvail[frameIdx]};
        VkSemaphore sig[] = {semRendDone[frameIdx]};

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = wait;
        si.pWaitDstStageMask = waitStages;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmdBuffers[frameIdx];
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = sig;

        if (vkQueueSubmit(gfxQueue, 1, &si, fences[frameIdx]) != VK_SUCCESS)
            throw std::runtime_error("queue submit failed");

        VkSwapchainKHR sw[] = {swapchain};
        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = sig;
        pi.swapchainCount = 1;
        pi.pSwapchains = sw;
        pi.pImageIndices = &imgIdx;
        r = vkQueuePresentKHR(presQueue, &pi);

        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || fbResized) {
            fbResized = false;
            recreateSwapchain();
        }

        frameIdx = (frameIdx + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void recreateSwapchain() {
        vkDeviceWaitIdle(device);
        // cleanup swapchain resources
        for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
        for (auto iv : scImageViews) vkDestroyImageView(device, iv, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroyImageView(device, depthImageView, nullptr);
        vkDestroyImage(device, depthImage, nullptr);
        vkFreeMemory(device, depthMemory, nullptr);

        createSwapchain();
        createImageViews();
        createDepthBuffer();
        createFramebuffers();
    }

    // ------------------------------------------------------------------
    // Cleanup
    // ------------------------------------------------------------------
    void cleanup() {
        vkDeviceWaitIdle(device);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vkDestroySemaphore(device, semImgAvail[i], nullptr);
            vkDestroySemaphore(device, semRendDone[i], nullptr);
            vkDestroyFence(device, fences[i], nullptr);
            vkDestroyBuffer(device, uboMVPBuf[i], nullptr);
            vkDestroyBuffer(device, uboMatBuf[i], nullptr);
            vkFreeMemory(device, uboMVPMem[i], nullptr);
            vkFreeMemory(device, uboMatMem[i], nullptr);
        }
        vkDestroyDescriptorPool(device, descPool, nullptr);
        vkDestroyDescriptorSetLayout(device, dslMVP, nullptr);
        vkDestroyDescriptorSetLayout(device, dslMat, nullptr);
        vkDestroyBuffer(device, vbo, nullptr);
        vkDestroyBuffer(device, ibo, nullptr);
        vkFreeMemory(device, vboMem, nullptr);
        vkFreeMemory(device, iboMem, nullptr);
        vkDestroyCommandPool(device, cmdPool, nullptr);
        for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);
        vkDestroyImageView(device, depthImageView, nullptr);
        vkDestroyImage(device, depthImage, nullptr);
        vkFreeMemory(device, depthMemory, nullptr);
        for (auto iv : scImageViews) vkDestroyImageView(device, iv, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
    }
};

// ============================================================================
// 入口
// ============================================================================
int main() {
    PBRApp app;
    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
