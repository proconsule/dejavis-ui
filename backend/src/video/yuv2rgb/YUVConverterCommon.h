#ifndef DEJAVIS_APP_YUV_CONVERTER_COMMON_H
#define DEJAVIS_APP_YUV_CONVERTER_COMMON_H

#include <vulkan/vulkan.h>
#include <cstdint>

#include "../render_globals.h"

namespace yuvconv {

// Alloca un buffer storage host-visible e ne mappa la memoria.
// Ritorna false su qualsiasi errore Vulkan. In caso di errore i campi out
// possono essere parzialmente popolati: il chiamante deve fare cleanup.
bool createMappedStorageBuffer(VulkanContext* ctx,
                               VkDeviceSize    size,
                               VkBuffer&       outBuffer,
                               VkDeviceMemory& outMemory,
                               void**          outMapped,
                               VkDeviceSize&   outActualSize);

void destroyMappedBuffer(VulkanContext* ctx,
                         VkBuffer&       buffer,
                         VkDeviceMemory& memory,
                         void**          mapped);

    uint32_t findMemoryType(VulkanContext* ctx, uint32_t typeFilter,
                        VkMemoryPropertyFlags properties);

// Pipeline barrier su una singola immagine.
void imageBarrier(VkCommandBuffer cmd,
                  VkImage img,
                  VkImageLayout oldLayout,
                  VkImageLayout newLayout,
                  VkAccessFlags srcAccess,
                  VkAccessFlags dstAccess,
                  VkPipelineStageFlags srcStage,
                  VkPipelineStageFlags dstStage);

// Pipeline barrier su un singolo buffer (host write -> shader read e similari).
void bufferBarrier(VkCommandBuffer cmd,
                   VkBuffer buf,
                   VkAccessFlags srcAccess,
                   VkAccessFlags dstAccess,
                   VkPipelineStageFlags srcStage,
                   VkPipelineStageFlags dstStage);

// Carica e compila uno shader GLSL compute (richiede glslang/shaderc nel build).
// Ritorna VK_NULL_HANDLE su errore (e logga il messaggio del compiler).
VkShaderModule compileComputeShader(VulkanContext* ctx,
                                    const char* glslSource,
                                    const char* nameForDebug);

// Helper allineamento
inline uint32_t alignUp(uint32_t v, uint32_t a) {
    return (v + a - 1u) & ~(a - 1u);
}

constexpr uint32_t kRowAlign = 256;   // tipico per FFmpeg/VAAPI/Intel

} // namespace yuvconv

#endif // DEJAVIS_APP_YUV_CONVERTER_COMMON_H