#include "descriptor_manager.h"
#include "types.h"

#include <stdexcept>
#include <array>

DescriptorManager::~DescriptorManager() {
    // Cleanup should be called explicitly
}

void DescriptorManager::createLayouts() {
    // MVP layout (set 0, vertex shader)
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1;
    li.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(device, &li, nullptr, &dslMVP) != VK_SUCCESS)
        throw std::runtime_error("MVP descriptor layout creation failed");

    // Material layout (set 1, fragment shader)
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    if (vkCreateDescriptorSetLayout(device, &li, nullptr, &dslMat) != VK_SUCCESS)
        throw std::runtime_error("Material descriptor layout creation failed");
}

void DescriptorManager::createPool(uint32_t imageCount) {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = imageCount * 2; // MVP + Material
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = imageCount;

    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = (uint32_t)poolSizes.size();
    pi.pPoolSizes = poolSizes.data();
    pi.maxSets = imageCount * 3;

    if (vkCreateDescriptorPool(device, &pi, nullptr, &pool) != VK_SUCCESS)
        throw std::runtime_error("descriptor pool creation failed");
}

void DescriptorManager::allocateSets(uint32_t imageCount) {
    setsMVP.resize(imageCount);
    setsMat.resize(imageCount);

    std::vector<VkDescriptorSetLayout> mvpLayouts(imageCount, dslMVP);
    std::vector<VkDescriptorSetLayout> matLayouts(imageCount, dslMat);

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool;
    ai.descriptorSetCount = imageCount;

    ai.pSetLayouts = mvpLayouts.data();
    if (vkAllocateDescriptorSets(device, &ai, setsMVP.data()) != VK_SUCCESS)
        throw std::runtime_error("MVP descriptor set allocation failed");

    ai.pSetLayouts = matLayouts.data();
    if (vkAllocateDescriptorSets(device, &ai, setsMat.data()) != VK_SUCCESS)
        throw std::runtime_error("Material descriptor set allocation failed");
}

void DescriptorManager::updateSets(uint32_t imageIndex, VkBuffer mvpBuffer, VkBuffer matBuffer) {
    VkDescriptorBufferInfo mvpInfo{};
    mvpInfo.buffer = mvpBuffer;
    mvpInfo.offset = 0;
    mvpInfo.range = sizeof(UBO_MVP);

    VkWriteDescriptorSet mvpWrite{};
    mvpWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    mvpWrite.dstSet = setsMVP[imageIndex];
    mvpWrite.dstBinding = 0;
    mvpWrite.descriptorCount = 1;
    mvpWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    mvpWrite.pBufferInfo = &mvpInfo;

    VkDescriptorBufferInfo matInfo{};
    matInfo.buffer = matBuffer;
    matInfo.offset = 0;
    matInfo.range = sizeof(UBO_Material);

    VkWriteDescriptorSet matWrite{};
    matWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matWrite.dstSet = setsMat[imageIndex];
    matWrite.dstBinding = 0;
    matWrite.descriptorCount = 1;
    matWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matWrite.pBufferInfo = &matInfo;

    std::array<VkWriteDescriptorSet, 2> writes = {mvpWrite, matWrite};
    vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
}

void DescriptorManager::cleanup() {
    if (pool) {
        vkDestroyDescriptorPool(device, pool, nullptr);
        pool = VK_NULL_HANDLE;
    }
    if (dslMat) {
        vkDestroyDescriptorSetLayout(device, dslMat, nullptr);
        dslMat = VK_NULL_HANDLE;
    }
    if (dslMVP) {
        vkDestroyDescriptorSetLayout(device, dslMVP, nullptr);
        dslMVP = VK_NULL_HANDLE;
    }
    setsMVP.clear();
    setsMat.clear();
}