#include "rgb2yuv_pipeline.h"
#include "../logger.h"

#include <vector>
#include <cstring>
#include <shaderc/shaderc.hpp>

#include "vulkan_utils.h"


static const char* rgb2yuv_nv12_glsl = R"(#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, rgba8) uniform readonly image2D inImage;
layout(std430, binding = 1) writeonly buffer YBuf  { uint dataY[];  };
layout(std430, binding = 2) writeonly buffer UVBuf { uint dataUV[]; };

layout(push_constant) uniform PC {
    int strideY;
    int strideU;
    int strideV;
    int rangeFull;
    int width;
    int height;
} pc;

vec3 sampleRGB(int x, int y) {
    return imageLoad(inImage, ivec2(clamp(x, 0, pc.width - 1), clamp(y, 0, pc.height - 1))).rgb;
}

uint rgbToY(vec3 rgb) {
    float y = 0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
    return (pc.rangeFull == 1) ? uint(clamp(y, 0.0, 1.0) * 255.0 + 0.5)
                               : uint(16.0 + clamp(y, 0.0, 1.0) * 219.0 + 0.5);
}

vec2 rgbToUV(vec3 rgb) {
    float u = -0.168736 * rgb.r - 0.331264 * rgb.g + 0.5 * rgb.b;
    float v = 0.5 * rgb.r - 0.418688 * rgb.g - 0.081312 * rgb.b;
    if (pc.rangeFull == 1) {
        return vec2(clamp(u + 0.5, 0.0, 1.0) * 255.0, clamp(v + 0.5, 0.0, 1.0) * 255.0);
    } else {
        return vec2(128.0 + clamp(u, -0.5, 0.5) * 224.0, 128.0 + clamp(v, -0.5, 0.5) * 224.0);
    }
}

void main() {
    int gX = int(gl_GlobalInvocationID.x) * 4;
    int gY = int(gl_GlobalInvocationID.y) * 2;

    if (gX >= pc.width || gY >= pc.height) return;

    vec3 c0 = sampleRGB(gX,     gY);
    vec3 c1 = sampleRGB(gX + 1, gY);
    vec3 c2 = sampleRGB(gX + 2, gY);
    vec3 c3 = sampleRGB(gX + 3, gY);

    vec3 b0 = sampleRGB(gX,     gY + 1);
    vec3 b1 = sampleRGB(gX + 1, gY + 1);
    vec3 b2 = sampleRGB(gX + 2, gY + 1);
    vec3 b3 = sampleRGB(gX + 3, gY + 1);

    uint packedY0 = rgbToY(c0) | (rgbToY(c1) << 8) | (rgbToY(c2) << 16) | (rgbToY(c3) << 24);
    uint packedY1 = rgbToY(b0) | (rgbToY(b1) << 8) | (rgbToY(b2) << 16) | (rgbToY(b3) << 24);

    dataY[(gY * pc.strideY + gX) / 4] = packedY0;
    dataY[((gY + 1) * pc.strideY + gX) / 4] = packedY1;

    vec2 uv01 = (rgbToUV(c0) + rgbToUV(c1) + rgbToUV(b0) + rgbToUV(b1)) * 0.25;
    vec2 uv23 = (rgbToUV(c2) + rgbToUV(c3) + rgbToUV(b2) + rgbToUV(b3)) * 0.25;

    uint u0 = uint(uv01.x + 0.5); uint v0 = uint(uv01.y + 0.5);
    uint u1 = uint(uv23.x + 0.5); uint v1 = uint(uv23.y + 0.5);

    uint packedUV = u0 | (v0 << 8) | (u1 << 16) | (v1 << 24);

    // --- CORREZIONE QUI: gX senza il * 2 ---
    dataUV[((gY / 2) * pc.strideU + gX) / 4] = packedUV;
}
)";

// =============================================================================
//  Shader YUV420P (Definitivo - Allineato a 8x2 Pixel per Thread)
// =============================================================================
static const char* rgb2yuv_yuv420p_glsl = R"(#version 450

// Per il formato planare puro, modifichiamo il local_size a 4x8 in modo che ciascun thread
// possa processare ben 8 pixel in larghezza. Questo consente a OGNI thread di produrre
// autonomamente 4 campioni di Chroma (U e V), scrivendo una uint completa (4 byte)
// su ciascun piano senza mai sovrapporsi o usare atomici!
layout(local_size_x = 4, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, rgba8) uniform readonly image2D inImage;
layout(std430, binding = 1) writeonly buffer YBuf { uint dataY[]; };
layout(std430, binding = 2) writeonly buffer UBuf { uint dataU[]; };
layout(std430, binding = 3) writeonly buffer VBuf { uint dataV[]; };

layout(push_constant) uniform PC {
    int strideY;
    int strideU;
    int strideV;
    int rangeFull;
    int width;
    int height;
} pc;

vec3 sampleRGB(int x, int y) {
    return imageLoad(inImage, ivec2(clamp(x, 0, pc.width - 1), clamp(y, 0, pc.height - 1))).rgb;
}

uint rgbToY(vec3 rgb) {
    float y = 0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
    return (pc.rangeFull == 1) ? uint(clamp(y, 0.0, 1.0) * 255.0 + 0.5)
                               : uint(16.0 + clamp(y, 0.0, 1.0) * 219.0 + 0.5);
}

vec2 rgbToUV(vec3 rgb) {
    float u = -0.168736 * rgb.r - 0.331264 * rgb.g + 0.5 * rgb.b;
    float v = 0.5 * rgb.r - 0.418688 * rgb.g - 0.081312 * rgb.b;
    if (pc.rangeFull == 1) {
        return vec2(clamp(u + 0.5, 0.0, 1.0) * 255.0, clamp(v + 0.5, 0.0, 1.0) * 255.0);
    } else {
        return vec2(128.0 + clamp(u, -0.5, 0.5) * 224.0, 128.0 + clamp(v, -0.5, 0.5) * 224.0);
    }
}

void main() {
    int gX = int(gl_GlobalInvocationID.x) * 8; // 8 pixel in larghezza
    int gY = int(gl_GlobalInvocationID.y) * 2; // 2 pixel in altezza

    if (gX >= pc.width || gY >= pc.height) return;

    // Carichiamo gli 8 pixel della riga superiore (gY) e gli 8 della riga inferiore (gY+1)
    vec3 c[8]; vec3 b[8];
    for(int i = 0; i < 8; i++) {
        c[i] = sampleRGB(gX + i, gY);
        b[i] = sampleRGB(gX + i, gY + 1);
    }

    // 1. Scrittura del Piano Y (2 uint da 4 byte per ciascuna riga)
    uint y0_part1 = rgbToY(c[0]) | (rgbToY(c[1]) << 8) | (rgbToY(c[2]) << 16) | (rgbToY(c[3]) << 24);
    uint y0_part2 = rgbToY(c[4]) | (rgbToY(c[5]) << 8) | (rgbToY(c[6]) << 16) | (rgbToY(c[7]) << 24);

    uint y1_part1 = rgbToY(b[0]) | (rgbToY(b[1]) << 8) | (rgbToY(b[2]) << 16) | (rgbToY(b[3]) << 24);
    uint y1_part2 = rgbToY(b[4]) | (rgbToY(b[5]) << 8) | (rgbToY(b[6]) << 16) | (rgbToY(b[7]) << 24);

    int baseIdxY0 = (gY * pc.strideY + gX) / 4;
    dataY[baseIdxY0]     = y0_part1;
    dataY[baseIdxY0 + 1] = y0_part2;

    int baseIdxY1 = ((gY + 1) * pc.strideY + gX) / 4;
    dataY[baseIdxY1]     = y1_part1;
    dataY[baseIdxY1 + 1] = y1_part2;

    // 2. Calcolo dei 4 campioni Chroma mediati (per le 4 coppie 2x2 contenute negli 8 pixel)
    uint u[4]; uint v[4];
    for(int i = 0; i < 4; i++) {
        vec2 uv = (rgbToUV(c[i*2]) + rgbToUV(c[i*2+1]) + rgbToUV(b[i*2]) + rgbToUV(b[i*2+1])) * 0.25;
        u[i] = uint(uv.x + 0.5);
        v[i] = uint(uv.y + 0.5);
    }

    // Impacchettamento dei 4 campioni in singole uint da 32 bit
    uint packedU = u[0] | (u[1] << 8) | (u[2] << 16) | (u[3] << 24);
    uint packedV = v[0] | (v[1] << 8) | (v[2] << 16) | (v[3] << 24);

    // Scrittura diretta e atomica-free sui piani U e V separati
    int chromaX = gX / 2;
    int chromaY = gY / 2;
    dataU[(chromaY * pc.strideU + chromaX) / 4] = packedU;
    dataV[(chromaY * pc.strideV + chromaX) / 4] = packedV;
}
)";

// =============================================================================
//  Init / Shutdown
// =============================================================================
bool RGB2YUVPipeline::init(VulkanContext* ctx) {
    if (m_initialized) return true;
    m_ctx = ctx;

    if (!createDescriptorLayout()) return false;
    if (!createPipelines()) {
        if (m_setLayout) vkDestroyDescriptorSetLayout(m_ctx->device, m_setLayout, nullptr);
        m_setLayout = VK_NULL_HANDLE;
        return false;
    }

    m_initialized = true;
    DEJAVISUI_LOG_INFO("[RGB2YUV] Pipeline initialized");
    return true;
}

void RGB2YUVPipeline::shutdown() {
    if (!m_initialized) return;
    vkDeviceWaitIdle(m_ctx->device);

    if (m_pipelineNV12)    vkDestroyPipeline(m_ctx->device, m_pipelineNV12, nullptr);
    if (m_pipelineYUV420P) vkDestroyPipeline(m_ctx->device, m_pipelineYUV420P, nullptr);
    if (m_pipelineLayout)  vkDestroyPipelineLayout(m_ctx->device, m_pipelineLayout, nullptr);
    if (m_setLayout)       vkDestroyDescriptorSetLayout(m_ctx->device, m_setLayout, nullptr);

    m_pipelineNV12    = VK_NULL_HANDLE;
    m_pipelineYUV420P = VK_NULL_HANDLE;
    m_pipelineLayout  = VK_NULL_HANDLE;
    m_setLayout       = VK_NULL_HANDLE;
    m_initialized = false;
}

// =============================================================================
//  Layout / Pipelines
// =============================================================================
bool RGB2YUVPipeline::createDescriptorLayout() {
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
        DEJAVISUI_LOG_ERROR("[RGB2YUV] descriptor set layout failed");
        return false;
    }
    return true;
}

VkShaderModule RGB2YUVPipeline::loadShader(const char* glslSource, const char* name) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);

    shaderc::SpvCompilationResult res = compiler.CompileGlslToSpv(
        glslSource, shaderc_glsl_compute_shader, name, options);
    if (res.GetCompilationStatus() != shaderc_compilation_status_success) {
        DEJAVISUI_LOG_ERROR("[RGB2YUV] %s compile error:\n%s",
                            name, res.GetErrorMessage().c_str());
        return VK_NULL_HANDLE;
    }
    std::vector<uint32_t> spv(res.cbegin(), res.cend());
    return createShaderModule(spv.data(), spv.size() * sizeof(uint32_t));
}

VkShaderModule RGB2YUVPipeline::createShaderModule(const uint32_t* code, size_t sizeInBytes) {
    VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    info.codeSize = sizeInBytes;
    info.pCode    = code;
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_ctx->device, &info, nullptr, &mod) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[RGB2YUV] vkCreateShaderModule failed");
        return VK_NULL_HANDLE;
    }
    return mod;
}

VkShaderModule RGB2YUVPipeline::createNV12Shader()    { return loadShader(rgb2yuv_nv12_glsl,    "rgb2yuv_nv12.comp"); }
VkShaderModule RGB2YUVPipeline::createYUV420PShader() { return loadShader(rgb2yuv_yuv420p_glsl, "rgb2yuv_yuv420p.comp"); }

bool RGB2YUVPipeline::createPipelines() {
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(RGB2YUVPushConstants);

    VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plInfo.setLayoutCount         = 1;
    plInfo.pSetLayouts            = &m_setLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges    = &pcr;
    if (vkCreatePipelineLayout(m_ctx->device, &plInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[RGB2YUV] pipeline layout failed");
        return false;
    }

    auto buildPipeline = [&](VkShaderModule shader, VkPipeline& outPipeline) -> bool {
        if (!shader) return false;
        VkComputePipelineCreateInfo info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        info.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.pName  = "main";
        info.stage.module = shader;
        info.layout       = m_pipelineLayout;
        VkResult r = vkCreateComputePipelines(m_ctx->device, VK_NULL_HANDLE, 1, &info, nullptr, &outPipeline);
        vkDestroyShaderModule(m_ctx->device, shader, nullptr);
        return r == VK_SUCCESS;
    };

    if (!buildPipeline(createNV12Shader(), m_pipelineNV12)) {
        DEJAVISUI_LOG_ERROR("[RGB2YUV] NV12 pipeline create failed");
        return false;
    }
    if (!buildPipeline(createYUV420PShader(), m_pipelineYUV420P)) {
        DEJAVISUI_LOG_ERROR("[RGB2YUV] YUV420P pipeline create failed");
        return false;
    }
    return true;
}

// =============================================================================
//  Helpers
// =============================================================================
uint32_t RGB2YUVPipeline::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
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

bool RGB2YUVPipeline::createBuffers(RGB2YUVSlotResources& slot) {
    constexpr uint32_t kRowAlign = 256;
    auto alignUp = [](uint32_t v, uint32_t a) { return (v + a - 1u) & ~(a - 1u); };

    const uint32_t alignedY = alignUp(slot.width,     kRowAlign);
    const uint32_t alignedC = alignUp(slot.width / 2, kRowAlign);

    // Stride scelti dalla pipeline (encoder li legge da slot.strides).
    if (slot.format == RGB2YUVFormat::NV12) {
        slot.strides[0] = (int)alignedY;            // Y
        slot.strides[1] = (int)alignedY;            // UV interleaved (stessa larghezza in byte di Y)
        slot.strides[2] = 0;                        // V unused
    } else {
        slot.strides[0] = (int)alignedY;            // Y
        slot.strides[1] = (int)alignedC;            // U
        slot.strides[2] = (int)alignedC;            // V
    }

    VkDeviceSize sizes[3];
    if (slot.format == RGB2YUVFormat::NV12) {
        sizes[0] = (VkDeviceSize)alignedY * slot.height;
        sizes[1] = (VkDeviceSize)alignedY * (slot.height / 2);
        sizes[2] = 16;                              // dummy buffer (richiesto dal descriptor)
    } else {
        sizes[0] = (VkDeviceSize)alignedY * slot.height;
        sizes[1] = (VkDeviceSize)alignedC * (slot.height / 2);
        sizes[2] = (VkDeviceSize)alignedC * (slot.height / 2);
    }

    for (int i = 0; i < 3; i++) {
        VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bi.size        = sizes[i];
        bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT| VK_BUFFER_USAGE_TRANSFER_SRC_BIT;;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_ctx->device, &bi, nullptr, &slot.buffers[i]) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[RGB2YUV] vkCreateBuffer[%d] failed", i);
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

bool RGB2YUVPipeline::createSync(RGB2YUVSlotResources& slot) {
    // Ring di semafori BINARI (output verso l'encoder) — invariato.
    {
        VkSemaphoreCreateInfo sem{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        for (int i = 0; i < RGB2YUVSlotResources::kSemRing; ++i) {
            if (vkCreateSemaphore(m_ctx->device, &sem, nullptr, &slot.semRing[i]) != VK_SUCCESS) {
                DEJAVISUI_LOG_ERROR("[RGB2YUV] semaphore[%d] failed", i);
                return false;
            }
        }
        slot.semWriteIdx     = 0;
        slot.computeFinished = slot.semRing[0];
    }

    {
        VkSemaphoreTypeCreateInfo timelineInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
        timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timelineInfo.initialValue  = 0;

        VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        semInfo.pNext = &timelineInfo;

        if (vkCreateSemaphore(m_ctx->device, &semInfo, nullptr, &slot.mixerTimeline) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[RGB2YUV] mixerTimeline create failed");
            return false;
        }
        slot.mixerTimelineValue.store(0, std::memory_order_release);
    }

    // Fence e cmd buffer — invariato.
    VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(m_ctx->device, &fi, nullptr, &slot.fence) != VK_SUCCESS) return false;

    VkCommandBufferAllocateInfo cbAlloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbAlloc.commandPool        = m_ctx->computeCommandPool;
    cbAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_ctx->device, &cbAlloc, &slot.cmd) != VK_SUCCESS) return false;

    return true;
}

bool RGB2YUVPipeline::createDescriptors(RGB2YUVSlotResources& slot) {
    VkDescriptorPoolSize sizes[2] = {};
    sizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    sizes[0].descriptorCount = 1;
    sizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sizes[1].descriptorCount = 3;

    VkDescriptorPoolCreateInfo pi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pi.maxSets       = 1;
    pi.poolSizeCount = 2;
    pi.pPoolSizes    = sizes;
    if (vkCreateDescriptorPool(m_ctx->device, &pi, nullptr, &slot.descriptorPool) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[RGB2YUV] descriptor pool failed");
        return false;
    }

    VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ai.descriptorPool     = slot.descriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &m_setLayout;
    if (vkAllocateDescriptorSets(m_ctx->device, &ai, &slot.computeDescriptorSet) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[RGB2YUV] descriptor set alloc failed");
        return false;
    }

    writeDescriptors(slot);
    return true;
}

void RGB2YUVPipeline::writeDescriptors(RGB2YUVSlotResources& slot) {
    VkDescriptorImageInfo inImg{};
    inImg.imageView   = slot.inputView;
    inImg.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo bufs[3];
    for (int i = 0; i < 3; ++i) {
        bufs[i].buffer = slot.buffers[i];
        bufs[i].offset = 0;
        bufs[i].range  = VK_WHOLE_SIZE;
    }

    VkWriteDescriptorSet w[4] = {};
    w[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet          = slot.computeDescriptorSet;
    w[0].dstBinding      = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[0].pImageInfo      = &inImg;

    for (int i = 0; i < 3; ++i) {
        w[i + 1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i + 1].dstSet          = slot.computeDescriptorSet;
        w[i + 1].dstBinding      = i + 1;
        w[i + 1].descriptorCount = 1;
        w[i + 1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[i + 1].pBufferInfo     = &bufs[i];
    }

    vkUpdateDescriptorSets(m_ctx->device, 4, w, 0, nullptr);
}

// =============================================================================
//  createSlot / destroySlot / resizeSlot
// =============================================================================
bool RGB2YUVPipeline::createSlot(RGB2YUVSlotResources& slot,
                                 uint32_t width, uint32_t height,
                                 RGB2YUVFormat format,
                                 VkImageView inputView)
{
    if (!m_initialized) {
        DEJAVISUI_LOG_ERROR("[RGB2YUV] createSlot before init");
        return false;
    }
    if (inputView == VK_NULL_HANDLE) {
        DEJAVISUI_LOG_ERROR("[RGB2YUV] inputView nullo");
        return false;
    }

    slot.width     = width;
    slot.height    = height;
    slot.format    = format;
    slot.inputView = inputView;

    if (!createBuffers(slot))     return false;
    if (!createSync(slot))        return false;
    if (!createDescriptors(slot)) return false;

    slot.valid = true;
    DEJAVISUI_LOG_DEBUG("[RGB2YUV] slot ready: %ux%u fmt=%s",
                        width, height,
                        format == RGB2YUVFormat::NV12 ? "NV12" : "YUV420P");
    return true;
}

void RGB2YUVPipeline::destroySlot(RGB2YUVSlotResources& slot) {
    if (!slot.valid) return;
    vkDeviceWaitIdle(m_ctx->device);

    if (slot.fence) vkDestroyFence(m_ctx->device, slot.fence, nullptr);
    for (int i = 0; i < RGB2YUVSlotResources::kSemRing; ++i) {
        if (slot.semRing[i]) {
            vkDestroySemaphore(m_ctx->device, slot.semRing[i], nullptr);
            slot.semRing[i] = VK_NULL_HANDLE;
        }
    }
    if (slot.mixerTimeline) {
        vkDestroySemaphore(m_ctx->device, slot.mixerTimeline, nullptr);
        slot.mixerTimeline = VK_NULL_HANDLE;
    }
    slot.mixerTimelineValue.store(0, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(slot.pendingSemMutex);
        slot.pendingSemaphores.clear();
    }
    if (slot.cmd) vkFreeCommandBuffers(m_ctx->device, m_ctx->computeCommandPool, 1, &slot.cmd);
    if (slot.descriptorPool) vkDestroyDescriptorPool(m_ctx->device, slot.descriptorPool, nullptr);

    for (int i = 0; i < 3; ++i) {
        if (slot.mappedPtrs[i])     vkUnmapMemory(m_ctx->device, slot.bufferMemories[i]);
        if (slot.buffers[i])        vkDestroyBuffer(m_ctx->device, slot.buffers[i], nullptr);
        if (slot.bufferMemories[i]) vkFreeMemory(m_ctx->device, slot.bufferMemories[i], nullptr);
    }

    slot.fence = VK_NULL_HANDLE;
    slot.computeFinished = VK_NULL_HANDLE;
    slot.semWriteIdx = 0;
    slot.cmd = VK_NULL_HANDLE;
    slot.descriptorPool = VK_NULL_HANDLE;
    slot.computeDescriptorSet = VK_NULL_HANDLE;
    for (int i = 0; i < 3; ++i) {
        slot.buffers[i] = VK_NULL_HANDLE;
        slot.bufferMemories[i] = VK_NULL_HANDLE;
        slot.mappedPtrs[i] = nullptr;
        slot.bufferSizes[i] = 0;
        slot.strides[i] = 0;
    }
    slot.inputView = VK_NULL_HANDLE;
    slot.width = slot.height = 0;
    slot.rangeFull = 0;
    slot.submitted.store(false);
    slot.semaphoreSignaled.store(false);
    slot.valid = false;
}

bool RGB2YUVPipeline::resizeSlot(RGB2YUVSlotResources& slot,
                                 uint32_t width, uint32_t height,
                                 RGB2YUVFormat format,
                                 VkImageView inputView)
{
    if (!m_initialized) return false;
    if (!slot.valid) {
        return createSlot(slot, width, height, format, inputView);
    }
    if (slot.width == width && slot.height == height &&
        slot.format == format && slot.inputView == inputView) {
        return true;
    }
    DEJAVISUI_LOG_DEBUG("[RGB2YUV] resize slot");
    vkDeviceWaitIdle(m_ctx->device);
    destroySlot(slot);
    return createSlot(slot, width, height, format, inputView);
}

// =============================================================================
//  recordDispatch
// =============================================================================
void RGB2YUVPipeline::recordDispatch(VkCommandBuffer cmd, RGB2YUVSlotResources& slot) {
    if (!slot.valid) return;
    VkPipeline pipe = (slot.format == RGB2YUVFormat::NV12)
                      ? m_pipelineNV12 : m_pipelineYUV420P;
    if (pipe == VK_NULL_HANDLE) return;

    // Bind
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);

    RGB2YUVPushConstants pc{};
    pc.strideY   = slot.strides[0];
    pc.strideU   = slot.strides[1];
    pc.strideV   = slot.strides[2];
    pc.rangeFull = slot.rangeFull;
    pc.width     = (int)slot.width;
    pc.height    = (int)slot.height;
    pc.padding0  = 0;
    pc.padding1  = 0;

    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_pipelineLayout, 0, 1, &slot.computeDescriptorSet, 0, nullptr);

    uint32_t gx = (slot.width  + 31) / 32;
    uint32_t gy = (slot.height + 15) / 16;

    vkCmdDispatch(cmd, gx, gy, 1);

    int numBufs = (slot.format == RGB2YUVFormat::NV12) ? 2 : 3;
    VkBufferMemoryBarrier bars[3]{};
    for (int i = 0; i < numBufs; ++i) {
        bars[i].sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bars[i].srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        bars[i].dstAccessMask       = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        bars[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bars[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bars[i].buffer              = slot.buffers[i];
        bars[i].offset              = 0;
        bars[i].size                = VK_WHOLE_SIZE;
    }
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, numBufs, bars, 0, nullptr);
}

// =============================================================================
//  setInputView
// =============================================================================
void RGB2YUVPipeline::setInputView(RGB2YUVSlotResources& slot, VkImageView newView) {
    // Memorizza solo. L'update effettivo del descriptor avviene in submitAsync,
    // dopo aver atteso il fence dello slot (cosi' il descriptor non e' in uso
    // da un dispatch in volo).
    slot.inputView = newView;
}

// =============================================================================
//  submitAsync
// =============================================================================
void RGB2YUVPipeline::submitAsync(RGB2YUVSlotResources& slot,
                                  uint64_t              waitTimelineValue,
                                  VkPipelineStageFlags  waitStage)
{
    if (!slot.valid) return;
    if (slot.fence == VK_NULL_HANDLE || slot.cmd == VK_NULL_HANDLE ||
        slot.semRing[0] == VK_NULL_HANDLE) {
        DEJAVISUI_LOG_ERROR("[RGB2YUV] sync non inizializzata");
        return;
    }
    if (slot.inputView == VK_NULL_HANDLE) {
        DEJAVISUI_LOG_ERROR("[RGB2YUV] inputView nulla, skip submit");
        return;
    }

    std::lock_guard<std::mutex> instanceLock(slot.submitMutex);

    {
        std::lock_guard<std::mutex> lk(slot.pendingSemMutex);
        if ((int)slot.pendingSemaphores.size() >= RGB2YUVSlotResources::kSemRing) {
            DEJAVISUI_LOG_DEBUG("[RGB2YUV] ring saturo, skip frame");
            // Nessun problema: waitTimelineValue rimarrà valido. Il prossimo
            // submit ci aspetterà comunque, anche se intanto il renderer ha
            // firmato un valore più alto. Timeline = monotonico → safe.
            return;
        }
    }

#ifndef __APPLE__
    VkResult waitRes = vkWaitForFences(m_ctx->device, 1, &slot.fence, VK_TRUE, 1'000'000'000);
    if (waitRes != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[RGB2YUV] vkWaitForFences: %d", waitRes);
        return;
    }
#endif
    vkResetFences(m_ctx->device, 1, &slot.fence);

  {
        VkDescriptorImageInfo inImg{};
        inImg.imageView   = slot.inputView;
        inImg.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        w.dstSet          = slot.computeDescriptorSet;
        w.dstBinding      = 0;
        w.descriptorCount = 1;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w.pImageInfo      = &inImg;
        vkUpdateDescriptorSets(m_ctx->device, 1, &w, 0, nullptr);
    }

    vkResetCommandBuffer(slot.cmd, 0);
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(slot.cmd, &bi);
    recordDispatch(slot.cmd, slot);
    vkEndCommandBuffer(slot.cmd);

    // Sceglie il semaforo binario dal ring di output
    VkSemaphore signalSem = slot.semRing[slot.semWriteIdx];
    slot.semWriteIdx = (slot.semWriteIdx + 1) % RGB2YUVSlotResources::kSemRing;
    slot.computeFinished = signalSem;

    bool gpuWait = (waitTimelineValue != 0);

    VkSemaphore          waitSems[1]   = { slot.mixerTimeline };
    uint64_t             waitValues[1] = { waitTimelineValue };
    VkPipelineStageFlags waitStages[1] = { waitStage };

    // --- CORREZIONE SEMAFORI: Prepariamo le strutture per agganciare sia il wait timeline che il signal binario ---
    VkSemaphore signalSems[1]   = { signalSem };
    uint64_t    signalValues[1] = { 0 }; // 0 per i semafori binari standard

    VkTimelineSemaphoreSubmitInfo timelineInfo{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };

    // Configurazione Signal (Semaforo Binario di fine calcolo compute)
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues    = signalValues;

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.pNext                = &timelineInfo;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &slot.cmd;

    // Assegnazione dei semafori di Signal nel SubmitInfo principale
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = signalSems;

    if (gpuWait) {
        timelineInfo.waitSemaphoreValueCount = 1;
        timelineInfo.pWaitSemaphoreValues    = waitValues;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores    = waitSems;
        si.pWaitDstStageMask  = waitStages;
    }

    {
        std::lock_guard<std::mutex> qlock(m_ctx->computeQueueMutexRef());
        VkResult r = VulkanQueueSubmit(m_ctx->computeQueue, 1, &si, slot.fence, "RGB2YUVPipeline::submitAsync");
        if ( r != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[RGB2YUV] vkQueueSubmit fallito: %d", r);
            return;
        }

    }
    {
        std::lock_guard<std::mutex> lk(slot.pendingSemMutex);
        slot.pendingSemaphores.push_back(signalSem);
    }

    slot.submitted.store(true);
    slot.semaphoreSignaled.store(true);
}

// =============================================================================
//  drainPendingSemaphores
// =============================================================================
void RGB2YUVPipeline::drainPendingSemaphores(RGB2YUVSlotResources& slot,
                                             std::vector<VkSemaphore>& out) {
    if (!slot.valid) return;
    std::lock_guard<std::mutex> lk(slot.pendingSemMutex);
    out.insert(out.end(),
               slot.pendingSemaphores.begin(),
               slot.pendingSemaphores.end());
    slot.pendingSemaphores.clear();
    slot.submitted.store(false);
    slot.semaphoreSignaled.store(false);
}

uint64_t RGB2YUVPipeline::reserveMixerTimelineValue(RGB2YUVSlotResources& slot) {
    return slot.mixerTimelineValue.fetch_add(1, std::memory_order_acq_rel) + 1;
}

VkSemaphore RGB2YUVPipeline::getMixerTimelineSemaphore(
    const RGB2YUVSlotResources& slot) const
{
    return slot.mixerTimeline;
}


