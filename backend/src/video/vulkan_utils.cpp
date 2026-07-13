#include "vulkan_utils.h"

#include <shaderc/shaderc.hpp>
#include <cstring>

void TransitionImageLayout(VkCommandBuffer cmd,
                           VulkanTexture&  tex,
                           VkImageLayout   newLayout)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = tex.currentLayout; // Assumendo che la tua struct tenga traccia del layout
    barrier.newLayout = newLayout;
    barrier.image = tex.image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    // Automatizzazione totale
    barrier.srcAccessMask = accessMaskFor(barrier.oldLayout);
    barrier.dstAccessMask = accessMaskFor(newLayout);

    VkPipelineStageFlags srcStage = stageMaskFor(barrier.oldLayout);
    VkPipelineStageFlags dstStage = stageMaskFor(newLayout);

    // Correzione specifica per la Presentazione (Access deve essere 0)
    if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.dstAccessMask = 0;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    tex.currentLayout = newLayout; // Aggiorna lo stato interno
}

void TransitionImageLayout_RAW(VkCommandBuffer cmd,
                           VkImage         image,
                           VkImageLayout   oldLayout,
                           VkImageLayout   newLayout)
{
    if (oldLayout == newLayout) return;

    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout           = oldLayout;
    b.newLayout           = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.srcAccessMask       = accessMaskFor(oldLayout);
    b.dstAccessMask       = accessMaskFor(newLayout);

    VkPipelineStageFlags srcStage = stageMaskFor(b.oldLayout);
    VkPipelineStageFlags dstStage = stageMaskFor(newLayout);

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                         0, nullptr, 0, nullptr,
                         1, &b);
}

VkCommandBuffer BeginSingleTimeCommands(VulkanContext* _ctx, QueueType type) {
    VkCommandPool pool;
    switch (type) {
        case QueueType::Compute:  pool = _ctx->computeCommandPool;  break;
        case QueueType::Transfer: pool = _ctx->transferCommandPool; break;
        default:                  pool = _ctx->graphicsCommandPool; break;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(_ctx->device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    return cmd;
}

void EndSingleTimeCommands(VulkanContext* _ctx, VkCommandBuffer cmd, QueueType type) {
    vkEndCommandBuffer(cmd);

    VkQueue queue;
    VkCommandPool pool;
    switch (type) {
        case QueueType::Compute:
            queue = _ctx->computeQueue;
            pool  = _ctx->computeCommandPool;
            break;
        case QueueType::Transfer:
            queue = _ctx->transferQueue;
            pool  = _ctx->transferCommandPool;
            break;
        default:
            queue = _ctx->graphicsQueue;
            pool  = _ctx->graphicsCommandPool;
            break;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(_ctx->device, pool, 1, &cmd);
}

void TransitionImageLayout_SingleCMD(VulkanContext* _ctx,VulkanTexture&  tex,
                           VkImageLayout   newLayout)
{
    if (tex.currentLayout == newLayout) return;  // niente da fare
    if (tex.image == VK_NULL_HANDLE)    return;  // safety

    VkCommandBuffer commandBuffer = BeginSingleTimeCommands(_ctx);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = tex.currentLayout; // Assumendo che la tua struct tenga traccia del layout
    barrier.newLayout = newLayout;
    barrier.image = tex.image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    // Automatizzazione totale
    barrier.srcAccessMask = accessMaskFor(barrier.oldLayout);
    barrier.dstAccessMask = accessMaskFor(newLayout);

    VkPipelineStageFlags srcStage = stageMaskFor(barrier.oldLayout);
    VkPipelineStageFlags dstStage = stageMaskFor(newLayout);

    // Correzione specifica per la Presentazione (Access deve essere 0)
    if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.dstAccessMask = 0;
    }

    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    tex.currentLayout = newLayout;

    EndSingleTimeCommands(_ctx,commandBuffer);
}

uint32_t FindMemoryType(VulkanContext *_ctx,uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(_ctx->physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

VkDescriptorSet createTextureDescriptor(VulkanContext *_ctx,VkSampler _sampler,VkImageView imageView) {
    if (imageView == VK_NULL_HANDLE || _sampler == VK_NULL_HANDLE) {
        DEJAVISUI_LOG_ERROR("Impossibile creare descrittore: View o Sampler nulli");
        return VK_NULL_HANDLE;
    }

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.descriptorPool = _ctx->descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &_ctx->m_mixerDescriptorLayout;

    if (vkAllocateDescriptorSets(_ctx->device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = _sampler;

    VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(_ctx->device, 1, &descriptorWrite, 0, nullptr);

    return descriptorSet;
}

VkResult VulkanQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence, const char* debugName) {
    //DEJAVISUI_LOG_DEBUG("[Vulkan Submit] : %s (Fence: %p)\n", debugName, (void*)fence);
    VkResult result = vkQueueSubmit(queue, submitCount, pSubmits, fence);

    if (result != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[Vulkan Error] Errore durante il submit di '%s'. Result: %d", debugName, result);
    }

    return result;
}

void Vulkan_CreateBuffer(VulkanContext *_ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(_ctx->device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Fallita la creazione del buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(_ctx->device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(_ctx, memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(_ctx->device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Fallita l'allocazione della memoria del buffer!");
    }

    vkBindBufferMemory(_ctx->device, buffer, bufferMemory, 0);
}


std::vector<uint32_t> Vulkan_GLSL2SPIRV(const char* source, shaderc_shader_kind kind, const char* name) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    options.SetOptimizationLevel(shaderc_optimization_level_performance);

    shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
        source, strlen(source), kind, name, options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << "Shader compilation failed: " << result.GetErrorMessage() << std::endl;
        return {};
    }

    return {result.cbegin(), result.cend()};
}


VkShaderModule Vulkan_CreateShader(VkDevice device,std::vector<uint32_t> spirv){

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        printf("Failed to create VkShaderModule\n");
        return VK_NULL_HANDLE;
    }

    return shaderModule;

}

void Vulkan_imageBarrier(VkCommandBuffer cmd, VkImage img,
                  VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                  VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout           = oldLayout;
    b.newLayout           = newLayout;
    b.srcAccessMask       = srcAccess;
    b.dstAccessMask       = dstAccess;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = img;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}





