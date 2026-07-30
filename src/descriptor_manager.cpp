#include "descriptor_manager.h"
#include "types.h"

#include <stdexcept>
#include <array>

// ----------------------------------------------------------------------------
// 析构函数: cleanup 必须显式调用
// ----------------------------------------------------------------------------
DescriptorManager::~DescriptorManager() {
    // Cleanup should be called explicitly
}

// ----------------------------------------------------------------------------
// 创建描述符集布局
// ----------------------------------------------------------------------------
// 描述符集布局定义 Shader 需要什么资源:
//   - binding: 资源槽位号 (Shader 中 layout(binding=0) uniform UBO ubo;)
//   - descriptorType: 资源类型 (UNIFORM_BUFFER/COMBINED_IMAGE_SAMPLER 等)
//   - descriptorCount: 资源数量 (通常是 1, 数组时 >1)
//   - stageFlags: Shader 阶段 (VERTEX/FRAGMENT)
//
// 本 Demo 创建三个布局:
//   1. dslMVP: vertex shader 的 MVP 矩阵 (binding 0, UNIFORM_BUFFER)
//   2. dslMat: fragment shader 的球体材质 (binding 0, UNIFORM_BUFFER)
//   3. dslMatGround: fragment shader 的地面材质 (binding 0, UNIFORM_BUFFER)
//
// 为什么球体和地面要分开?
//   - 它们的材质参数不同 (albedo/metallic/roughness)
//   - 独立的 set 可以在渲染时切换, 避免修改同一个 set
// ----------------------------------------------------------------------------
void DescriptorManager::createLayouts() {
    // MVP layout (set 0, vertex shader)
    //   - binding 0: UNIFORM_BUFFER, 1 个, vertex shader 使用
    //   - 绑定 MVP 矩阵的 uniform buffer
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;  // Shader 中的 binding 号
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;  // 统一缓冲
    binding.descriptorCount = 1;  // 1 个 buffer
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;  // vertex shader 使用

    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1;  // 1 个 binding
    li.pBindings = &binding;

    // 创建 MVP 布局
    if (vkCreateDescriptorSetLayout(device, &li, nullptr, &dslMVP) != VK_SUCCESS)
        throw std::runtime_error("MVP descriptor layout creation failed");

    // Material layout (set 1, fragment shader)
    //   - binding 0: UNIFORM_BUFFER, 1 个, fragment shader 使用
    //   - 绑定球体材质的 uniform buffer
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;  // fragment shader 使用
    if (vkCreateDescriptorSetLayout(device, &li, nullptr, &dslMat) != VK_SUCCESS)
        throw std::runtime_error("Material descriptor layout creation failed");

    // 地面材质布局（结构同球体，独立 set）
    //   - 同样的 binding/type/count, 但绑定到不同的 uniform buffer
    //   - 独立的 set 便于渲染时切换
    if (vkCreateDescriptorSetLayout(device, &li, nullptr, &dslMatGround) != VK_SUCCESS)
        throw std::runtime_error("Ground material descriptor layout creation failed");
}

// ----------------------------------------------------------------------------
// 创建描述符池
// ----------------------------------------------------------------------------
// 描述符池是描述符集的"内存池":
//   - 预先分配一定数量的各类型描述符
//   - 从中分配描述符集, 避免频繁申请/释放
//
// 池大小计算:
//   - UNIFORM_BUFFER: imageCount * 3 (MVP + Mat + MatGround, 每帧各一个)
//   - COMBINED_IMAGE_SAMPLER: imageCount (预留, 当前未用纹理)
//   - maxSets: imageCount * 4 (MVP + Mat + MatGround + shadow, 预留阴影)
//
// 为什么按 imageCount 分配?
//   - 每帧独立一套描述符集, 避免 CPU 更新时 GPU 还在读 (数据竞争)
//   - 第 0 帧用 sets[0], 第 1 帧用 sets[1], 循环使用
// ----------------------------------------------------------------------------
void DescriptorManager::createPool(uint32_t imageCount) {
    // 池大小: 各类型描述符的数量
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    // UNIFORM_BUFFER: 每帧 3 个 (MVP + Mat + MatGround)
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = imageCount * 3;
    // COMBINED_IMAGE_SAMPLER: 每帧 1 个 (预留, 当前未用)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = imageCount;

    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = (uint32_t)poolSizes.size();
    pi.pPoolSizes = poolSizes.data();
    // 最大描述符集数: 每帧 4 个 (MVP + Mat + MatGround + shadow)
    pi.maxSets = imageCount * 4;

    if (vkCreateDescriptorPool(device, &pi, nullptr, &pool) != VK_SUCCESS)
        throw std::runtime_error("descriptor pool creation failed");
}

// ----------------------------------------------------------------------------
// 分配描述符集
// ----------------------------------------------------------------------------
// 从池中分配描述符集:
//   - 每帧分配一个 MVP set / Material set / MaterialGround set
//   - 分配时指定布局 (dslMVP/dslMat/dslMatGround)
//   - 分配后 set 是"空的", 需要通过 updateSets 绑定具体 buffer
//
// 流程:
//   1. 准备布局数组 (每帧用同一个布局)
//   2. 设置 VkDescriptorSetAllocateInfo (池 + 布局)
//   3. 调用 vkAllocateDescriptorSets 分配
// ----------------------------------------------------------------------------
void DescriptorManager::allocateSets(uint32_t imageCount) {
    setsMVP.resize(imageCount);
    setsMat.resize(imageCount);
    setsMatGround.resize(imageCount);

    // 每帧用同一个布局, 复制 imageCount 份
    std::vector<VkDescriptorSetLayout> mvpLayouts(imageCount, dslMVP);
    std::vector<VkDescriptorSetLayout> matLayouts(imageCount, dslMat);
    std::vector<VkDescriptorSetLayout> matGroundLayouts(imageCount, dslMatGround);

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool;  // 从池中分配
    ai.descriptorSetCount = imageCount;  // 分配 imageCount 个 set

    // 分配 MVP sets
    ai.pSetLayouts = mvpLayouts.data();
    if (vkAllocateDescriptorSets(device, &ai, setsMVP.data()) != VK_SUCCESS)
        throw std::runtime_error("MVP descriptor set allocation failed");

    // 分配 Material sets
    ai.pSetLayouts = matLayouts.data();
    if (vkAllocateDescriptorSets(device, &ai, setsMat.data()) != VK_SUCCESS)
        throw std::runtime_error("Material descriptor set allocation failed");

    // 分配 MaterialGround sets
    ai.pSetLayouts = matGroundLayouts.data();
    if (vkAllocateDescriptorSets(device, &ai, setsMatGround.data()) != VK_SUCCESS)
        throw std::runtime_error("Ground material descriptor set allocation failed");
}

// ----------------------------------------------------------------------------
// 更新球体的描述符集
// ----------------------------------------------------------------------------
// 把 uniform buffer 绑到描述符集:
//   - imageIndex: 当前帧索引
//   - mvpBuffer: MVP 矩阵的 uniform buffer
//   - matBuffer: 球体材质的 uniform buffer
//
// 流程:
//   1. 构造 VkDescriptorBufferInfo (buffer + offset + range)
//   2. 构造 VkWriteDescriptorSet (目标 set + binding + buffer info)
//   3. 调用 vkUpdateDescriptorSets 更新
//
// 为什么每帧独立更新?
//   - 每帧的 uniform buffer 不同 (MVP 矩阵每帧变化)
//   - 独立的 set 避免 CPU 更新时 GPU 还在读 (数据竞争)
// ----------------------------------------------------------------------------
void DescriptorManager::updateSets(uint32_t imageIndex, VkBuffer mvpBuffer, VkBuffer matBuffer) {
    // MVP buffer info
    VkDescriptorBufferInfo mvpInfo{};
    mvpInfo.buffer = mvpBuffer;
    mvpInfo.offset = 0;
    mvpInfo.range = sizeof(UBO_MVP);  // 绑定整个 UBO

    // MVP write descriptor
    VkWriteDescriptorSet mvpWrite{};
    mvpWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    mvpWrite.dstSet = setsMVP[imageIndex];  // 目标 set
    mvpWrite.dstBinding = 0;                // binding 0
    mvpWrite.descriptorCount = 1;
    mvpWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    mvpWrite.pBufferInfo = &mvpInfo;

    // Material buffer info
    VkDescriptorBufferInfo matInfo{};
    matInfo.buffer = matBuffer;
    matInfo.offset = 0;
    matInfo.range = sizeof(UBO_Material);

    // Material write descriptor
    VkWriteDescriptorSet matWrite{};
    matWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matWrite.dstSet = setsMat[imageIndex];
    matWrite.dstBinding = 0;
    matWrite.descriptorCount = 1;
    matWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matWrite.pBufferInfo = &matInfo;

    // 批量更新 (2 个 write)
    std::array<VkWriteDescriptorSet, 2> writes = {mvpWrite, matWrite};
    vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
}

// ----------------------------------------------------------------------------
// 更新地面的描述符集
// ----------------------------------------------------------------------------
// 把地面材质的 uniform buffer 绑到描述符集:
//   - imageIndex: 当前帧索引
//   - matGroundBuffer: 地面材质的 uniform buffer
// ----------------------------------------------------------------------------
void DescriptorManager::updateGroundSets(uint32_t imageIndex, VkBuffer matGroundBuffer) {
    VkDescriptorBufferInfo matInfo{};
    matInfo.buffer = matGroundBuffer;
    matInfo.offset = 0;
    matInfo.range = sizeof(UBO_Material);

    VkWriteDescriptorSet matWrite{};
    matWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matWrite.dstSet = setsMatGround[imageIndex];
    matWrite.dstBinding = 0;
    matWrite.descriptorCount = 1;
    matWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matWrite.pBufferInfo = &matInfo;

    vkUpdateDescriptorSets(device, 1, &matWrite, 0, nullptr);
}

// ----------------------------------------------------------------------------
// 销毁所有描述符对象
// ----------------------------------------------------------------------------
// 销毁顺序:
//   1. 销毁描述符池 (自动释放池中所有描述符集, 无需单独销毁)
//   2. 销毁描述符集布局
//   3. 清空 set 数组 (只是指针, 不销毁)
// ----------------------------------------------------------------------------
void DescriptorManager::cleanup() {
    // 销毁池 (自动释放所有 set)
    if (pool) {
        vkDestroyDescriptorPool(device, pool, nullptr);
        pool = VK_NULL_HANDLE;
    }
    // 销毁布局
    if (dslMatGround) {
        vkDestroyDescriptorSetLayout(device, dslMatGround, nullptr);
        dslMatGround = VK_NULL_HANDLE;
    }
    if (dslMat) {
        vkDestroyDescriptorSetLayout(device, dslMat, nullptr);
        dslMat = VK_NULL_HANDLE;
    }
    if (dslMVP) {
        vkDestroyDescriptorSetLayout(device, dslMVP, nullptr);
        dslMVP = VK_NULL_HANDLE;
    }
    // 清空 set 数组 (池已销毁, set 自动释放)
    setsMVP.clear();
    setsMat.clear();
    setsMatGround.clear();
}
