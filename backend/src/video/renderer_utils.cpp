#include "renderer.h"
#include <iostream>


const char* getDeviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:          return "Other";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "Discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "Virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU";
        default:                                     return "Unknown";
    }
}

void CRenderer::Print_GPU_List(){
    std::cout << "Detected GPU: " << gpu_list.size() << std::endl;
    for (const auto& device : gpu_list) {
        uint32_t major = VK_API_VERSION_MAJOR(device.dev_prop.apiVersion);
        uint32_t minor = VK_API_VERSION_MINOR(device.dev_prop.apiVersion);
        uint32_t patch = VK_API_VERSION_PATCH(device.dev_prop.apiVersion);

        std::cout << "Name:    " << device.dev_prop.deviceName << std::endl;
        std::cout << "Type:    " << getDeviceTypeName(device.dev_prop.deviceType) << std::endl;
        std::cout << "Vulkan:  " << major << "." << minor << "." << patch << std::endl;
        std::cout << "RAM:  " << device.vramGB << std::endl;
        std::cout << "------------------------------------------" << std::endl;

    }
}

/*
uint32_t CRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_ctx.physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}
*/

Json::Value CRenderer::getRendererStatusJson() {
    Json::Value status;

    // 1. Stato Generale
    status["running"] = gpu_active;
    status["gpu_active"] = gpu_active;
    status["glfw_active"] = glfw_active;

    status["frameCount"] = framecount;
    status["fps"] = std::round(dejatimer.get_fps() * 100.0f) / 100.0f; // Arrotonda a 2 decimali
    status["frameTimeMs"] = dejatimer.get_delta_time() * 1000.0f;
    status["window_w"] = window_w;
    status["window_h"] = window_h;
    status["core_w"] = core_w;
    status["core_h"] = core_h;




#ifdef _WIN32
    status["SPOUT2_SENDER_STATUS"] = spout2_sender_active.load();
    status["SPOUT2_SENDER_WIDTH"] = sender_SPOUT2.GetSenderWidth();
    status["SPOUT2_SENDER_HEIGHT"] = sender_SPOUT2.GetSenderHeight();
    status["SPOUT2_SENDER_NAME"] = sender_SPOUT2.GetSenderName();
    status["SPOUT2_SENDER_FORMAT"] = (uint32_t)sender_SPOUT2.GetCurrentD3DFormat();
    status["SPOUT2_RECEIVER_STATUS"] = "N/A";
#else
    status["SPOUT2_SENDER_STATUS"] = "N/A";
    status["SPOUT2_RECEIVER_STATUS"] = "N/A";

#endif
    if (m_ctx.physicalDevice != VK_NULL_HANDLE) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(m_ctx.physicalDevice, &props);
        status["activeGpu"]["name"] = props.deviceName;
        status["activeGpu"]["type"] = getDeviceTypeName(props.deviceType);
        status["activeGpu"]["vulkanVersion"] = std::to_string(VK_API_VERSION_MAJOR(props.apiVersion)) + "." +
                                               std::to_string(VK_API_VERSION_MINOR(props.apiVersion));
    } else {
        status["activeGpu"] = Json::nullValue;
    }

    Json::Value list(Json::arrayValue);
    int idx = 0;
    for (const auto& gpu : gpu_list) {
        Json::Value dev;
        dev["id"] = idx;
        dev["name"] = gpu.dev_prop.deviceName;
        dev["type"] = getDeviceTypeName(gpu.dev_prop.deviceType);
        dev["deviceId"] = gpu.dev_prop.deviceID;
        dev["vendorId"] = gpu.dev_prop.vendorID;
        dev["vramGB"] = gpu.vramGB;
        list.append(dev);
        idx++;
    }
    status["availableGpus"] = list;

    status["videomixer"] = GetVideoMixerJson();

    return status;
}


/*
void CRenderer::TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        // Aggiungi altri casi se necessario (es. per Spout)
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
*/


void CRenderer::LimitFrameRate() {
    if(!framelimiter)return;
    float mytargetms = fpstargetMS;

    while (dejatimer.get_elapsed_since_last_update_ms() < mytargetms - 2.0) {
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }

    while (dejatimer.get_elapsed_since_last_update_ms() < mytargetms - 0.5) {
        std::this_thread::yield();
    }

    while (dejatimer.get_elapsed_since_last_update_ms() < mytargetms) {

    }
}

void CRenderer::SetFrameLimit(uint32_t _targetFPS){
    if(_targetFPS == 0){
        framelimiter = false;
        return;
    }
    framelimiter = true;
    fpstarget = _targetFPS;
    fpstargetMS = (1.0/fpstarget)*1000.f;
}

VkShaderModule CRenderer::CreateShaderModule(const uint32_t* code, size_t sizeInBytes) {
    VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    createInfo.pNext = nullptr;
    createInfo.flags = 0;
    createInfo.codeSize = sizeInBytes; // Deve essere la dimensione totale in byte
    createInfo.pCode = code;

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_ctx.device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("Fallita creazione Shader Module!");
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}
/*
void CRenderer::ImageBarrier(VkCommandBuffer cmd, VkImage image,
                             VkImageLayout oldLayout, VkImageLayout newLayout,
                             VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                             VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.image = image;

    if (image == m_yuvVideo.image) {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT |
                                              VK_IMAGE_ASPECT_PLANE_1_BIT |
                                              VK_IMAGE_ASPECT_PLANE_2_BIT;
    } else {
        // Per le immagini R8 del compute shader o il Master RGBA
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    vkCmdPipelineBarrier(
        cmd,
        srcStage,       // In quale fase della pipeline dobbiamo aspettare? (es. COMPUTE)
        dstStage,       // Quale fase deve attendere? (es. TRANSFER o VIDEO_ENCODE)
        0,              // Dependency flags
        0, nullptr,     // Memory barriers
        0, nullptr,     // Buffer barriers
        1, &barrier     // Image memory barrier
    );
}
*/
/*
VkCommandBuffer CRenderer::BeginSingleTimeCommands(VulkanContext* _ctx, QueueType type) {
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

void CRenderer::EndSingleTimeCommands(VulkanContext* _ctx, VkCommandBuffer cmd, QueueType type) {
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
*/
void CRenderer::Vulkan_CopyBufferToImage(VulkanContext* ctx, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands(ctx);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    EndSingleTimeCommands(ctx, commandBuffer);
}

void CRenderer::Vulkan_CreateBuffer(VulkanContext* ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(ctx->device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Fallita la creazione del buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(ctx->device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(&m_ctx,memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(ctx->device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Fallita l'allocazione della memoria del buffer!");
    }

    vkBindBufferMemory(ctx->device, buffer, bufferMemory, 0);
}

void CRenderer::CleanupTexture(VulkanTexture& tex) {

    vkDeviceWaitIdle(m_ctx.device);

    // 2. Ora procedi con la distruzione (ordine inverso)
    if (tex.descriptorSet != VK_NULL_HANDLE) {
        // Nota: Spesso i descriptor si resettano col pool,
        // ma se li hai allocati singolarmente:
        // vkFreeDescriptorSets(device, descriptorPool, 1, &tex.descriptorSet);
    }

    if (tex.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_ctx.device, tex.sampler, nullptr);
    }

    if (tex.view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_ctx.device, tex.view, nullptr);
    }

    if (tex.image != VK_NULL_HANDLE) {
        vkDestroyImage(m_ctx.device, tex.image, nullptr);
    }

    if (tex.memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_ctx.device, tex.memory, nullptr);
    }
    // 3. Opzionale: azzera la struct per evitare "double free"
    tex = VulkanTexture{};
}


/*

void CRenderer::TransitionImageLayout(VkCommandBuffer cmd,
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

void CRenderer::TransitionImageLayout_SingleCMD(VulkanTexture&  tex,
                           VkImageLayout   newLayout)
{
    if (tex.currentLayout == newLayout) return;  // niente da fare
    if (tex.image == VK_NULL_HANDLE)    return;  // safety

    VkCommandBuffer commandBuffer = BeginSingleTimeCommands(&m_ctx);

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

    EndSingleTimeCommands(&m_ctx,commandBuffer);
}

*/

/*
void CRenderer::TransitionImageLayout_RAW(VkCommandBuffer cmd,
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
*/

