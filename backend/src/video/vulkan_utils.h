#ifndef DEJAVIS_UI_VULKAN_UTILS_H
#define DEJAVIS_UI_VULKAN_UTILS_H

#include "render_globals.h"

void TransitionImageLayout(VkCommandBuffer cmd,
                           VulkanTexture&  tex,
                           VkImageLayout   newLayout);

void TransitionImageLayout_RAW(VkCommandBuffer cmd,
                       VkImage         image,
                       VkImageLayout   oldLayout,
                       VkImageLayout   newLayout);
void TransitionImageLayout_SingleCMD(VulkanContext* _ctx,VulkanTexture&  tex,
                           VkImageLayout   newLayout);

VkDescriptorSet createTextureDescriptor(VulkanContext *_ctx,VkSampler _sampler,VkImageView imageView);

VkCommandBuffer BeginSingleTimeCommands(VulkanContext* _ctx, QueueType type = QueueType::Graphics);
void EndSingleTimeCommands(VulkanContext* _ctx, VkCommandBuffer commandBuffer, QueueType type = QueueType::Graphics);

uint32_t FindMemoryType(VulkanContext *_ctx,uint32_t typeFilter, VkMemoryPropertyFlags properties);

VkResult VulkanQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence, const char* debugName);
#endif //DEJAVIS_UI_VULKAN_UTILS_H