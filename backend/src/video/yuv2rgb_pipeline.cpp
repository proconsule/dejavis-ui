#include "yuv2rgb_pipeline.h"
#include "../logger.h"

#include <vector>
#include <cstring>
#include <libavutil/hwcontext_vulkan.h>
#include <shaderc/shaderc.hpp>

#include "vulkan_utils.h"

// =============================================================================
//  Shader sorgente — GLSL inline. Spostalo in un .h se preferisci.
//  Output: storage image RGBA8.
//  Input:  buffer Y, buffer U (o UV), buffer V.
//  Push constants: strideY, strideU, strideV, isNV12, rangeFull, width, height.
// =============================================================================
static const char* yuv2rgb_glsl = R"(#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0, rgba8) uniform writeonly image2D outImage;
layout(std430, binding = 1) readonly buffer YBuf  { uint dataY[]; };
layout(std430, binding = 2) readonly buffer UBuf  { uint dataU[]; };
layout(std430, binding = 3) readonly buffer VBuf  { uint dataV[]; };

layout(push_constant) uniform PC {
    int strideY;
    int strideU;
    int strideV;
    int isNV12;
    int rangeFull;
    int width;
    int height;
    int isUYVY;
} pc;

float fetchByte(uint v, int byteIdx) {
    return float((v >> (byteIdx * 8)) & 0xFFu);
}

// Legge un singolo byte da dataY a un offset arbitrario in byte.
// Gestisce qualsiasi allineamento di stride.
float readY8(int byteOfs) {
    uint w = dataY[byteOfs >> 2];
    return fetchByte(w, byteOfs & 3);
}

float sampleY(int x, int y) {
    int idx = y * pc.strideY + x;
    uint word = dataY[idx >> 2];
    return fetchByte(word, idx & 3);
}

vec2 sampleUV(int x, int y) {
    int cx = x >> 1;
    int cy = y >> 1;
    if (pc.isNV12 == 1) {
        int idx = cy * pc.strideU + cx * 2;
        uint w0 = dataU[idx >> 2];
        float u = fetchByte(w0, idx & 3);
        float v = fetchByte(w0, (idx + 1) & 3);
        if (((idx & 3) + 1) > 3) {
            uint w1 = dataU[(idx + 1) >> 2];
            v = fetchByte(w1, (idx + 1) & 3);
        }
        return vec2(u, v);
    } else {
        int uIdx = cy * pc.strideU + cx;
        int vIdx = cy * pc.strideV + cx;
        uint wU = dataU[uIdx >> 2];
        uint wV = dataV[vIdx >> 2];
        return vec2(fetchByte(wU, uIdx & 3), fetchByte(wV, vIdx & 3));
    }
}

// BT.601 limited/full -> RGB normalizzato
vec4 convertYUV(float Y, float U, float V) {
    float yN, uN, vN;
    if (pc.rangeFull == 1) {
        yN = Y / 255.0;
        uN = (U - 128.0) / 255.0;
        vN = (V - 128.0) / 255.0;
    } else {
        yN = (Y - 16.0)  / 219.0;
        uN = (U - 128.0) / 224.0;
        vN = (V - 128.0) / 224.0;
    }
    float r = yN + 1.402    * vN;
    float g = yN - 0.344136 * uN - 0.714136 * vN;
    float b = yN + 1.772    * uN;
    return vec4(clamp(vec3(r, g, b), 0.0, 1.0), 1.0);
}

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= pc.width || px.y >= pc.height) return;

    float Y, U, V;

    if (pc.isUYVY == 1) {
        // UYVY: 4 byte packed per ogni pair di pixel orizzontali.
        // Layout pair: [U0, Y0, V0, Y1]. strideY = bytes per riga (= width*2).
        int pairX = px.x & ~1;
        int base  = px.y * pc.strideY + pairX * 2;

        float u  = readY8(base + 0);
        float y0 = readY8(base + 1);
        float v  = readY8(base + 2);
        float y1 = readY8(base + 3);

        U = u;
        V = v;
        Y = ((px.x & 1) == 0) ? y0 : y1;
    } else {
        Y       = sampleY(px.x, px.y);
        vec2 uv = sampleUV(px.x, px.y);
        U = uv.x;
        V = uv.y;
    }

    imageStore(outImage, px, convertYUV(Y, U, V));
})";

// =============================================================================
//  Init / Shutdown
// =============================================================================
bool YUV2RGBPipeline::init(VulkanContext* ctx) {
    Logger::getInstance().setMinLevel(LogLevel::LevelDebug);
    if (m_initialized) return true;
    m_ctx = ctx;

    if (!createDescriptorLayout()) return false;
    if (!createPipeline()) {
        if (m_setLayout) vkDestroyDescriptorSetLayout(m_ctx->device, m_setLayout, nullptr);
        m_setLayout = VK_NULL_HANDLE;
        return false;
    }

    m_initialized = true;
    DEJAVISUI_LOG_INFO("[YUV2RGB] Pipeline initialized");
    return true;
}

void YUV2RGBPipeline::shutdown() {
    if (!m_initialized) return;
    vkDeviceWaitIdle(m_ctx->device);
    if (m_pipeline)       vkDestroyPipeline(m_ctx->device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_ctx->device, m_pipelineLayout, nullptr);
    if (m_setLayout)      vkDestroyDescriptorSetLayout(m_ctx->device, m_setLayout, nullptr);
    m_pipeline = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_setLayout = VK_NULL_HANDLE;
    m_initialized = false;
}

// =============================================================================
//  Layout / Pipeline
// =============================================================================
bool YUV2RGBPipeline::createDescriptorLayout() {
    VkDescriptorSetLayoutBinding b[4] = {};
    b[0].binding         = 0;
    b[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b[0].descriptorCount = 1;
    b[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    for (int i = 1; i < 4; ++i) {
        b[i].binding         = i;
        b[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[i].descriptorCount = 1;
        b[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    info.bindingCount = 4;
    info.pBindings    = b;
    if (vkCreateDescriptorSetLayout(m_ctx->device, &info, nullptr, &m_setLayout) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] descriptor set layout failed");
        return false;
    }
    return true;
}

VkShaderModule YUV2RGBPipeline::loadShader(const char* glslSource, const char* name) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);

    shaderc::SpvCompilationResult res = compiler.CompileGlslToSpv(
        glslSource, shaderc_glsl_compute_shader, name, options);
    if (res.GetCompilationStatus() != shaderc_compilation_status_success) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] %s compile error:\n%s",
                            name, res.GetErrorMessage().c_str());
        return VK_NULL_HANDLE;
    }
    std::vector<uint32_t> spv(res.cbegin(), res.cend());
    return createShaderModule(spv.data(), spv.size() * sizeof(uint32_t));
}

VkShaderModule YUV2RGBPipeline::createShaderModule(const uint32_t* code, size_t sizeInBytes) {
    VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    info.codeSize = sizeInBytes;
    info.pCode    = code;
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_ctx->device, &info, nullptr, &mod) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] vkCreateShaderModule failed");
        return VK_NULL_HANDLE;
    }
    return mod;
}

VkShaderModule YUV2RGBPipeline::createShader() {
    return loadShader(yuv2rgb_glsl, "yuv2rgb.comp");
}

bool YUV2RGBPipeline::createPipeline() {
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(YUV2RGBPushConstants);

    VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plInfo.setLayoutCount         = 1;
    plInfo.pSetLayouts            = &m_setLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges    = &pcr;
    if (vkCreatePipelineLayout(m_ctx->device, &plInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] pipeline layout failed");
        return false;
    }

    VkShaderModule s = createShader();
    if (!s) return false;

    VkComputePipelineCreateInfo info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    info.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.pName  = "main";
    info.stage.module = s;
    info.layout       = m_pipelineLayout;

    VkResult r = vkCreateComputePipelines(m_ctx->device, VK_NULL_HANDLE, 1, &info, nullptr, &m_pipeline);
    vkDestroyShaderModule(m_ctx->device, s, nullptr);

    if (r != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] pipeline create failed: %d", r);
        return false;
    }
    return true;
}

// =============================================================================
//  Helpers
// =============================================================================
uint32_t YUV2RGBPipeline::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_ctx->physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool YUV2RGBPipeline::createOutputTexture(YUV2RGBSlotResources& slot, uint32_t w, uint32_t h) {
    VkImageCreateInfo imgInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent        = { w, h, 1 };
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                          | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_ctx->device, &imgInfo, nullptr, &slot.rgbaTexture.VkTexture.image) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] vkCreateImage failed");
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_ctx->device, slot.rgbaTexture.VkTexture.image, &memReq);

    VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    alloc.allocationSize  = memReq.size;
    alloc.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (alloc.memoryTypeIndex == UINT32_MAX) return false;
    if (vkAllocateMemory(m_ctx->device, &alloc, nullptr, &slot.rgbaTexture.VkTexture.memory) != VK_SUCCESS) {
        return false;
    }
    vkBindImageMemory(m_ctx->device, slot.rgbaTexture.VkTexture.image,
                      slot.rgbaTexture.VkTexture.memory, 0);

    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image            = slot.rgbaTexture.VkTexture.image;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(m_ctx->device, &viewInfo, nullptr, &slot.rgbaTexture.VkTexture.view) != VK_SUCCESS) {
        return false;
    }

    VkSamplerCreateInfo s{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    s.magFilter = VK_FILTER_LINEAR;
    s.minFilter = VK_FILTER_LINEAR;
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(m_ctx->device, &s, nullptr, &slot.rgbaTexture.VkTexture.sampler) != VK_SUCCESS) {
        return false;
    }

    slot.rgbaTexture.VkTexture.width         = w;
    slot.rgbaTexture.VkTexture.height        = h;
    slot.rgbaTexture.VkTexture.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

bool YUV2RGBPipeline::createBuffers(YUV2RGBSlotResources& slot, uint32_t w, uint32_t h) {
    // Dimensiona i buffer assumendo uno stride allineato a 256 byte per riga.
    // FFmpeg/VAAPI/Intel tipicamente allineano a 256; codec NVDEC/x264 a 64. 256
    // copre tutti i casi pratici. Se in futuro un decoder usasse un allineamento
    // ancora maggiore, uploadSoftwareFrame fallirebbe il bound check e basta
    // alzare questa costante.
    constexpr uint32_t kRowAlign = 256;
    auto alignUp = [](uint32_t v, uint32_t a) { return (v + a - 1u) & ~(a - 1u); };

    const uint32_t alignedYU = alignUp(w * 2, kRowAlign);  // copre anche UYVY packed
    const uint32_t alignedUV = alignUp(w,     kRowAlign);
    const uint32_t alignedC  = alignUp(w / 2, kRowAlign);

    VkDeviceSize sizes[3] = {
        (VkDeviceSize)alignedYU * h,           // Y o UYVY packed
        (VkDeviceSize)alignedUV * (h / 2),     // UV (NV12) o U (YUV420P)
        (VkDeviceSize)alignedC  * (h / 2)      // V (YUV420P), dummy altrove
    };

    for (int i = 0; i < 3; i++) {
        VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bi.size        = sizes[i];
        bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_ctx->device, &bi, nullptr, &slot.buffers[i]) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[YUV2RGB] vkCreateBuffer[%d] failed", i);
            return false;
        }

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_ctx->device, slot.buffers[i], &req);

        VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX) return false;
        if (vkAllocateMemory(m_ctx->device, &ai, nullptr, &slot.bufferMemories[i]) != VK_SUCCESS) {
            return false;
        }
        vkBindBufferMemory(m_ctx->device, slot.buffers[i], slot.bufferMemories[i], 0);
        if (vkMapMemory(m_ctx->device, slot.bufferMemories[i], 0, req.size, 0,
                        &slot.mappedPtrs[i]) != VK_SUCCESS) {
            return false;
        }
        slot.bufferSizes[i] = req.size;
    }
    return true;
}

bool YUV2RGBPipeline::createSync(YUV2RGBSlotResources& slot) {
    VkSemaphoreCreateInfo sem{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    // Crea il ring di semafori. Tutti partono unsignaled.
    for (int i = 0; i < YUV2RGBSlotResources::kSemRing; ++i) {
        if (vkCreateSemaphore(m_ctx->device, &sem, nullptr, &slot.semRing[i]) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[YUV2RGB] semaphore[%d] create failed", i);
            return false;
        }
    }
    slot.semWriteIdx     = 0;
    slot.computeFinished = slot.semRing[0]; // alias per chi legge l'handle "corrente"

    VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(m_ctx->device, &fi, nullptr, &slot.fence) != VK_SUCCESS)
        return false;

    VkCommandBufferAllocateInfo cbAlloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbAlloc.commandPool        = m_ctx->computeCommandPool;
    cbAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_ctx->device, &cbAlloc, &slot.cmd) != VK_SUCCESS)
        return false;

    return true;
}

bool YUV2RGBPipeline::createDescriptors(YUV2RGBSlotResources& slot,
                                        VkDescriptorSetLayout mixerSamplerLayout) {
    VkDescriptorPoolSize sizes[3] = {};
    sizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    sizes[0].descriptorCount = 1;
    sizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sizes[1].descriptorCount = 3;
    sizes[2].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[2].descriptorCount = 1;

    VkDescriptorPoolCreateInfo pi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pi.maxSets       = 2;
    pi.poolSizeCount = 3;
    pi.pPoolSizes    = sizes;
    if (vkCreateDescriptorPool(m_ctx->device, &pi, nullptr, &slot.descriptorPool) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] descriptor pool failed");
        return false;
    }

    // Compute set
    {
        VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        ai.descriptorPool     = slot.descriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &m_setLayout;
        if (vkAllocateDescriptorSets(m_ctx->device, &ai, &slot.computeDescriptorSet) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[YUV2RGB] compute set alloc failed");
            return false;
        }
    }
    // Mixer set (combined image sampler della rgbaTexture)
    {
        VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        ai.descriptorPool     = slot.descriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &mixerSamplerLayout;
        if (vkAllocateDescriptorSets(m_ctx->device, &ai, &slot.mixerDescriptorSet) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[YUV2RGB] mixer set alloc failed");
            return false;
        }
        // questo lo esponiamo anche come VkTexture.descriptorSet per
        // compatibilita' col getter del decoder che a volte lo riusa
        slot.rgbaTexture.VkTexture.descriptorSet = slot.mixerDescriptorSet;
    }

    writeDescriptors(slot);
    return true;
}

void YUV2RGBPipeline::writeDescriptors(YUV2RGBSlotResources& slot) {
    VkDescriptorImageInfo outImg{};
    outImg.imageView   = slot.rgbaTexture.VkTexture.view;
    outImg.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo bufs[3];
    for (int i = 0; i < 3; ++i) {
        bufs[i].buffer = slot.buffers[i];
        bufs[i].offset = 0;
        bufs[i].range  = VK_WHOLE_SIZE;
    }

    VkDescriptorImageInfo mixerImg{};
    mixerImg.sampler     = slot.rgbaTexture.VkTexture.sampler;
    mixerImg.imageView   = slot.rgbaTexture.VkTexture.view;
    mixerImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet w[5] = {};
    w[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet          = slot.computeDescriptorSet;
    w[0].dstBinding      = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[0].pImageInfo      = &outImg;

    for (int i = 0; i < 3; ++i) {
        w[i + 1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i + 1].dstSet          = slot.computeDescriptorSet;
        w[i + 1].dstBinding      = i + 1;
        w[i + 1].descriptorCount = 1;
        w[i + 1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[i + 1].pBufferInfo     = &bufs[i];
    }

    w[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[4].dstSet          = slot.mixerDescriptorSet;
    w[4].dstBinding      = 0;
    w[4].descriptorCount = 1;
    w[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[4].pImageInfo      = &mixerImg;

    vkUpdateDescriptorSets(m_ctx->device, 5, w, 0, nullptr);
}

// =============================================================================
//  createSlot / destroySlot
// =============================================================================
bool YUV2RGBPipeline::createSlot(YUV2RGBSlotResources& slot,
                                 uint32_t width, uint32_t height,
                                 VkDescriptorSetLayout mixerSamplerLayout)
{
    if (!m_initialized) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] createSlot before init");
        return false;
    }
    slot.width  = width;
    slot.height = height;

    if (!createOutputTexture(slot, width, height)) return false;
    if (!createBuffers(slot, width, height))       return false;
    if (!createSync(slot))                         return false;
    if (!createDescriptors(slot, mixerSamplerLayout)) return false;

    slot.valid = true;
    DEJAVISUI_LOG_DEBUG("[YUV2RGB] slot ready: %ux%u", width, height);
    return true;
}

void YUV2RGBPipeline::destroySlot(YUV2RGBSlotResources& slot) {
    if (!slot.valid) return;
    vkDeviceWaitIdle(m_ctx->device);

    if (slot.fence)           vkDestroyFence(m_ctx->device, slot.fence, nullptr);
    for (int i = 0; i < YUV2RGBSlotResources::kSemRing; ++i) {
        if (slot.semRing[i]) {
            vkDestroySemaphore(m_ctx->device, slot.semRing[i], nullptr);
            slot.semRing[i] = VK_NULL_HANDLE;
        }
    }
    {
        std::lock_guard<std::mutex> lk(slot.pendingSemMutex);
        slot.pendingSemaphores.clear();
    }
    if (slot.cmd)             vkFreeCommandBuffers(m_ctx->device, m_ctx->computeCommandPool, 1, &slot.cmd);

    if (slot.descriptorPool)  vkDestroyDescriptorPool(m_ctx->device, slot.descriptorPool, nullptr);

    for (int i = 0; i < 3; ++i) {
        if (slot.mappedPtrs[i])     vkUnmapMemory(m_ctx->device, slot.bufferMemories[i]);
        if (slot.buffers[i])        vkDestroyBuffer(m_ctx->device, slot.buffers[i], nullptr);
        if (slot.bufferMemories[i]) vkFreeMemory(m_ctx->device, slot.bufferMemories[i], nullptr);
    }

    if (slot.rgbaTexture.VkTexture.sampler) vkDestroySampler(m_ctx->device, slot.rgbaTexture.VkTexture.sampler, nullptr);
    if (slot.rgbaTexture.VkTexture.view)    vkDestroyImageView(m_ctx->device, slot.rgbaTexture.VkTexture.view, nullptr);
    if (slot.rgbaTexture.VkTexture.image)   vkDestroyImage(m_ctx->device, slot.rgbaTexture.VkTexture.image, nullptr);
    if (slot.rgbaTexture.VkTexture.memory)  vkFreeMemory(m_ctx->device, slot.rgbaTexture.VkTexture.memory, nullptr);

    // reset, ma il mutex/atomics non si possono assegnare via slot = {};
    slot.fence = VK_NULL_HANDLE;
    slot.computeFinished = VK_NULL_HANDLE;
    slot.semWriteIdx = 0;
    slot.cmd = VK_NULL_HANDLE;
    slot.descriptorPool = VK_NULL_HANDLE;
    slot.computeDescriptorSet = VK_NULL_HANDLE;
    slot.mixerDescriptorSet = VK_NULL_HANDLE;
    for (int i = 0; i < 3; ++i) {
        slot.buffers[i] = VK_NULL_HANDLE;
        slot.bufferMemories[i] = VK_NULL_HANDLE;
        slot.mappedPtrs[i] = nullptr;
        slot.bufferSizes[i] = 0;
        slot.strides[i] = 0;
    }
    slot.rgbaTexture = {};
    slot.width = slot.height = 0;
    slot.isNV12 = false;
    slot.isUYVY = false;
    slot.rangeFull = 0;
    slot.pts = 0.0;
    slot.submitted.store(false);
    slot.semaphoreSignaled.store(false);
    slot.valid = false;
    DEJAVISUI_LOG_DEBUG("[YUV2RGB] SLOT Destroyed");
}

// =============================================================================
//  resizeSlot — drain + destroy + create alle nuove dimensioni
// =============================================================================
bool YUV2RGBPipeline::resizeSlot(YUV2RGBSlotResources& slot,
                                 uint32_t width, uint32_t height,
                                 VkDescriptorSetLayout mixerSamplerLayout)
{
    if (!m_initialized) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] resizeSlot before init");
        return false;
    }

    // Slot non ancora creato -> equivalente a createSlot
    if (!slot.valid) {
        return createSlot(slot, width, height, mixerSamplerLayout);
    }

    // No-op se le dimensioni coincidono
    if (slot.width == width && slot.height == height) {
        return true;
    }

    DEJAVISUI_LOG_DEBUG("[YUV2RGB] resize slot: %ux%u -> %ux%u",
                        slot.width, slot.height, width, height);

    // Drain di tutto il device prima di toccare le risorse.
    // Operazione non frequente (cambio file / cambio rendition), accettabile.
    vkDeviceWaitIdle(m_ctx->device);

    destroySlot(slot);
    return createSlot(slot, width, height, mixerSamplerLayout);
}

bool YUV2RGBPipeline::uploadNDIFrame(YUV2RGBSlotResources& slot, const NDIlib_video_frame_v2_t& v) {
    if (!slot.valid) return false;

    const uint32_t h = (uint32_t)v.yres;
    const uint32_t w = (uint32_t)v.xres;

    // Aggiorniamo i metadati dello slot per lo shader
    slot.width  = w;
    slot.height = h;

    switch (v.FourCC) {
        case NDIlib_FourCC_type_NV12: {
            const uint8_t* yPlane  = v.p_data;
            const int      yStride = v.line_stride_in_bytes;
            const uint8_t* uvPlane = yPlane + (size_t)yStride * h;
            const int      uvStride = v.line_stride_in_bytes;

            slot.strides[0] = yStride;
            slot.strides[1] = uvStride;
            slot.strides[2] = 0;
            slot.isNV12 = true;
            slot.isUYVY = false;
            slot.rangeFull = 0; // NDI standard is Limited Range

            if (slot.mappedPtrs[0]) std::memcpy(slot.mappedPtrs[0], yPlane,  (size_t)yStride  * h);
            if (slot.mappedPtrs[1]) std::memcpy(slot.mappedPtrs[1], uvPlane, (size_t)uvStride * (h / 2));
            break;
        }
        case NDIlib_FourCC_type_UYVY: {
            const uint8_t* data = v.p_data;
            const int      stride = v.line_stride_in_bytes;

            slot.strides[0] = stride;
            slot.isNV12 = false;
            slot.isUYVY = true;

            if (slot.mappedPtrs[0]) {
                std::memcpy(slot.mappedPtrs[0], data, (size_t)stride * h);
            }
            break;
        }
        default:
            DEJAVISUI_LOG_WARN("[YUV2RGB] FourCC NDI non supportato: 0x%08x", v.FourCC);
            return false;
    }
    return true;
}

bool YUV2RGBPipeline::uploadSoftwareFrame(YUV2RGBSlotResources& slot, AVFrame* f) {
    if (!slot.valid || !f) return false;

    const uint32_t h = (uint32_t)f->height;
    slot.strides[0] = f->linesize[0];
    slot.strides[1] = f->linesize[1];
    slot.strides[2] = f->linesize[2];
    slot.pts        = (double)f->pts;

    const bool isNV = (f->format == AV_PIX_FMT_NV12 ||
                       f->format == AV_PIX_FMT_NV21 ||
                       f->format == AV_PIX_FMT_P010LE);
    const bool isUYVY = (f->format == AV_PIX_FMT_UYVY422);

    slot.isNV12 = isNV;
    slot.isUYVY = isUYVY;

    // ---------- UYVY: single-plane packed ----------
    if (isUYVY) {
        const size_t bytes = (size_t)f->linesize[0] * h;
        if (bytes > slot.bufferSizes[0]) {
            DEJAVISUI_LOG_ERROR("[YUV2RGB] UYVY plane troppo grande (%zu > %llu)",
                                bytes, (unsigned long long)slot.bufferSizes[0]);
            return false;
        }
        memcpy(slot.mappedPtrs[0], f->data[0], bytes);
        // Buffer U/V non vengono letti dallo shader (isUYVY=1), nessuna copia.
        return true;
    }

    // ---------- Y plane (planar / semi-planar) ----------
    {
        size_t bytes = (size_t)f->linesize[0] * h;
        if (bytes > slot.bufferSizes[0]) {
            DEJAVISUI_LOG_ERROR("[YUV2RGB] Y plane troppo grande (%zu > %llu)",
                                bytes, (unsigned long long)slot.bufferSizes[0]);
            return false;
        }
        memcpy(slot.mappedPtrs[0], f->data[0], bytes);
    }

    if (f->format == AV_PIX_FMT_YUV420P) {
        size_t bU = (size_t)f->linesize[1] * (h / 2);
        size_t bV = (size_t)f->linesize[2] * (h / 2);
        if (bU > slot.bufferSizes[1] || bV > slot.bufferSizes[2]) {
            DEJAVISUI_LOG_ERROR("[YUV2RGB] U/V plane oversize");
            return false;
        }
        memcpy(slot.mappedPtrs[1], f->data[1], bU);
        memcpy(slot.mappedPtrs[2], f->data[2], bV);
    }
    else if (isNV) {
        size_t bUV = (size_t)f->linesize[1] * (h / 2);
        if (bUV > slot.bufferSizes[1]) {
            DEJAVISUI_LOG_ERROR("[YUV2RGB] UV plane oversize");
            return false;
        }
        memcpy(slot.mappedPtrs[1], f->data[1], bUV);
    }
    else {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] formato non supportato: %d", f->format);
        return false;
    }
    return true;
}

bool YUV2RGBPipeline::uploadHardwareFrame(YUV2RGBSlotResources& slot, AVFrame* frame) {
    if (!slot.valid || !frame) return false;

    // Se il frame è VAAPI, lo mappiamo in Vulkan (operazione GPU-only)
    AVFrame* vk_frame = nullptr;
    bool is_mapped = false;
    if (frame->format == AV_PIX_FMT_VAAPI) {
        vk_frame = av_frame_alloc();
        // av_hwframe_map crea una vista Vulkan (VkImage) del frame VAAPI esistente
        DEJAVISUI_LOG_DEBUG("HW MAP");
        if (av_hwframe_map(vk_frame, frame, 0) < 0) {
            DEJAVISUI_LOG_ERROR("HW MAP FAILED");
            av_frame_free(&vk_frame);
            return false;
        }
        is_mapped = true;
    } else {
        vk_frame = frame;
    }

    if (vk_frame->format != AV_PIX_FMT_VULKAN) {
        if (is_mapped) av_frame_free(&vk_frame);
        return false;
    }

    DEJAVISUI_LOG_DEBUG("VULKAN EXTRACT");
    // Estraiamo gli handle Vulkan forniti da FFmpeg
    AVVkFrame* vk_info = (AVVkFrame*)vk_frame->data[0];

    // Prepariamo un command buffer rapido per la copia interna alla GPU
    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = m_ctx->transferCommandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_ctx->device, &ai, &cmd);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    // NV12 ha 2 piani: Y (0) e UV (1)
    // Copiamo direttamente dalle immagini Vulkan di FFmpeg ai tuoi buffer di slot
    for (int i = 0; i < 2; i++) {
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent.width  = (i == 0) ? vk_frame->width : vk_frame->width / 2;
        region.imageExtent.height = (i == 0) ? vk_frame->height : vk_frame->height / 2;
        region.imageExtent.depth  = 1;

        // Se l'immagine è NV12, il secondo piano (UV) ha larghezza dimezzata ma
        // 2 byte per pixel, FFmpeg gestisce questo nei descrittori dell'immagine.
        vkCmdCopyImageToBuffer(cmd, vk_info->img[i],
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               slot.buffers[i], 1, &region);
    }

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    {
        std::lock_guard<std::mutex> lock(m_ctx->computeQueueMutex);
        vkQueueSubmit(m_ctx->computeQueue, 1, &si, VK_NULL_HANDLE);
        // Usiamo waitIdle per semplicità, ma potresti usare un fence
        // per renderlo davvero asincrono
        vkQueueWaitIdle(m_ctx->computeQueue);
    }

    vkFreeCommandBuffers(m_ctx->device, m_ctx->transferCommandPool, 1, &cmd);

    if (is_mapped) av_frame_free(&vk_frame);
    return true;
}

// =============================================================================
//  recordDispatch
// =============================================================================
static void imageBarrier(VkCommandBuffer cmd, VkImage img,
                         VkImageLayout oldL, VkImageLayout newL,
                         VkAccessFlags srcAcc, VkAccessFlags dstAcc,
                         VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.srcAccessMask       = srcAcc;
    b.dstAccessMask       = dstAcc;
    b.oldLayout           = oldL;
    b.newLayout           = newL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = img;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

void YUV2RGBPipeline::recordDispatch(VkCommandBuffer cmd, YUV2RGBSlotResources& slot) {
    if (!slot.valid || m_pipeline == VK_NULL_HANDLE) return;

    auto& tex = slot.rgbaTexture.VkTexture;

    // 1. Buffer barrier: HOST_WRITE -> SHADER_READ (Y/U/V)
    VkBufferMemoryBarrier bufBars[3]{};
    for (int i = 0; i < 3; ++i) {
        bufBars[i].sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bufBars[i].srcAccessMask       = VK_ACCESS_HOST_WRITE_BIT;
        bufBars[i].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        bufBars[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufBars[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufBars[i].buffer              = slot.buffers[i];
        bufBars[i].offset              = 0;
        bufBars[i].size                = VK_WHOLE_SIZE;
    }
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 3, bufBars, 0, nullptr);

    // 2. Image barrier: layout corrente -> GENERAL per imageStore
    imageBarrier(cmd, tex.image,
                 tex.currentLayout, VK_IMAGE_LAYOUT_GENERAL,
                 0, VK_ACCESS_SHADER_WRITE_BIT,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    tex.currentLayout = VK_IMAGE_LAYOUT_GENERAL;

    // 3. Bind & push constants
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);

    YUV2RGBPushConstants pc{};
    pc.strideY   = slot.strides[0];
    pc.strideU   = slot.strides[1];
    pc.strideV   = slot.strides[2];
    pc.isNV12    = slot.isNV12 ? 1 : 0;
    pc.rangeFull = slot.rangeFull;
    pc.width     = (int)tex.width;
    pc.height    = (int)tex.height;
    pc.isUYVY    = slot.isUYVY ? 1 : 0;

    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_pipelineLayout, 0, 1, &slot.computeDescriptorSet, 0, nullptr);

    uint32_t gx = ((uint32_t)pc.width  + 15) / 16;
    uint32_t gy = ((uint32_t)pc.height + 15) / 16;
    vkCmdDispatch(cmd, gx, gy, 1);

    // 4. Barrier finale: GENERAL -> SHADER_READ_ONLY_OPTIMAL.
    // dstStage = COMPUTE_SHADER_BIT compatibile con coda compute pura.
    // Va bene sia per FX successiva (compute) sia per render (acquire grafico
    // dopo wait sul semaforo = TOP_OF_PIPE -> FRAGMENT_SHADER nel cmdbuf graphics).
    imageBarrier(cmd, tex.image,
                 VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    tex.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

// =============================================================================
//  submitAsync
// =============================================================================
void YUV2RGBPipeline::submitAsync(YUV2RGBSlotResources& slot,
                                  VideoFXSlotResources* fxSlot) {
    if (!slot.valid) return;
    if (slot.fence == VK_NULL_HANDLE || slot.cmd == VK_NULL_HANDLE ||
        slot.semRing[0] == VK_NULL_HANDLE) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] sync non inizializzata");
        return;
    }

    // Lock per istanza: protegge wait+reset+record+submit+ring write.
    std::lock_guard<std::mutex> instanceLock(slot.submitMutex);

    // Sceglie il prossimo semaforo del ring. NB: deve essere unsignaled.
    // In regime, il renderer drena i pendingSemaphores ad ogni frame, quindi
    // un ring di kSemRing >= MAX_FRAMES_IN_FLIGHT e' sufficiente.
    // Se il consumer e' in ritardo (renderer paused?) e tutti i kSemRing
    // sarebbero ancora pendenti, e' meglio fare skip: rifirmare un semaforo
    // ancora signaled e' UB.
    {
        std::lock_guard<std::mutex> lk(slot.pendingSemMutex);
        if ((int)slot.pendingSemaphores.size() >= YUV2RGBSlotResources::kSemRing) {
            DEJAVISUI_LOG_DEBUG("[YUV2RGB] ring saturo, skip frame (renderer in ritardo?)");
            return;
        }
    }

    VkResult waitRes = vkWaitForFences(m_ctx->device, 1, &slot.fence, VK_TRUE, 1'000'000'000);
    if (waitRes != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] vkWaitForFences: %d", waitRes);
        return;
    }
    vkResetFences(m_ctx->device, 1, &slot.fence);

    vkResetCommandBuffer(slot.cmd, 0);
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(slot.cmd, &bi);

    // 1. YUV -> RGBA. Lascia la rgbaTexture in SHADER_READ_ONLY_OPTIMAL,
    //    che e' la precondizione richiesta dalla FX.
    recordDispatch(slot.cmd, slot);

    // 2. (Opzionale) FX. Registra i suoi dispatch nello stesso command buffer
    //    cosi' la sync lato GPU e' implicita: il semaforo del ring si firma
    //    quando ANCHE la FX e' finita. La finalImage della FX risulta in
    //    SHADER_READ_ONLY_OPTIMAL al wait lato graphics.
    if (fxSlot && fxSlot->valid) {
        VideoFXPipeline::instance().recordDispatch(slot.cmd, *fxSlot);
    }

    vkEndCommandBuffer(slot.cmd);

    // Sceglie il semaforo da firmare in questo submit.
    VkSemaphore signalSem = slot.semRing[slot.semWriteIdx];
    slot.semWriteIdx = (slot.semWriteIdx + 1) % YUV2RGBSlotResources::kSemRing;
    // Aggiorna l'alias "corrente" — chi legge getComputeFinishedSemaphore()
    // ottiene cosi' un handle che e' coerente con l'ultimo submit.
    slot.computeFinished = signalSem;

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &slot.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &signalSem;

    {
        std::lock_guard<std::mutex> qlock(m_ctx->computeQueueMutex);
        VkResult r = vkQueueSubmit(m_ctx->computeQueue, 1, &si, slot.fence);
        if (r != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[YUV2RGB] vkQueueSubmit: %d", r);
            vkResetFences(m_ctx->device, 1, &slot.fence);
            return;
        }
    }

    // Pubblica il semaforo per il renderer.
    {
        std::lock_guard<std::mutex> lk(slot.pendingSemMutex);
        slot.pendingSemaphores.push_back(signalSem);
    }

    // Compatibilita': lasciamo i flag a true cosi' chi li legge come "c'e'
    // lavoro pronto" continua a funzionare. Non sono piu' usati per la sync.
    slot.submitted.store(true);
    slot.semaphoreSignaled.store(true);
}

// =============================================================================
//  drainPendingSemaphores
// =============================================================================
void YUV2RGBPipeline::drainPendingSemaphores(YUV2RGBSlotResources& slot,
                                             std::vector<VkSemaphore>& out) {
    if (!slot.valid) return;
    std::lock_guard<std::mutex> lk(slot.pendingSemMutex);
    out.insert(out.end(),
               slot.pendingSemaphores.begin(),
               slot.pendingSemaphores.end());
    slot.pendingSemaphores.clear();
    // Reset flag di "lavoro pendente" — non strettamente necessari ma coerenti
    // con il drain.
    slot.submitted.store(false);
    slot.semaphoreSignaled.store(false);
}