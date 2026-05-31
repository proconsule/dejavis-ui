#include "rgb2yuv_pipeline.h"
#include "../logger.h"

#include <vector>
#include <cstring>
#include <shaderc/shaderc.hpp>

#include "vulkan_utils.h"

// =============================================================================
//  Shader sorgente NV12
//  Una invocation copre un blocco 2x2 pixel per scrivere un solo campione UV.
//  Ogni invocation calcola 4 sample Y e 1 sample UV mediato sul 2x2.
//  local size = 16x16 (in pixel), quindi gx = (w+15)/16 / 2 -> usiamo (w+1)/2
//  Per semplicita' uso local_size 8x8 (in blocchi 2x2 = 16x16 pixel).
// =============================================================================
static const char* rgb2yuv_nv12_glsl = R"(#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, rgba8) uniform readonly image2D inImage;
layout(std430, binding = 1) writeonly buffer YBuf  { uint dataY[];  };
layout(std430, binding = 2) writeonly buffer UVBuf { uint dataUV[]; };
// binding 3 (V) presente nel descriptor ma non scritto in NV12

layout(push_constant) uniform PC {
    int strideY;
    int strideU;    // stride UV in NV12
    int strideV;    // unused
    int rangeFull;
    int width;
    int height;
    int padding0;
    int padding1;
} pc;

vec3 sampleRGB(int x, int y) {
    x = clamp(x, 0, pc.width  - 1);
    y = clamp(y, 0, pc.height - 1);
    return imageLoad(inImage, ivec2(x, y)).rgb;
}

// Atomicamente scrive un byte in un buffer di uint usando bitwise ops.
// Ogni invocation aggiorna solo i propri byte, mai con altre — quindi non
// servirebbe atomic se le invocation non si sovrappongono. In pratica ogni
// invocation scrive 4 byte di Y (4 sample distinti) e 2 byte di UV: tutti
// in posizioni esclusive. Possiamo fare un read-modify-write non atomico
// solo se le scritture concomitanti su byte diversi della stessa uint sono
// safe... NO, non lo sono in GLSL su buffer. Usiamo atomicAnd/atomicOr.

void writeByte(int byteIdx, uint val, bool isUV) {
    int wordIdx = byteIdx >> 2;
    int shift   = (byteIdx & 3) * 8;
    uint clearMask = ~(0xFFu << shift);
    uint setMask   = (val & 0xFFu) << shift;
    if (isUV) {
        atomicAnd(dataUV[wordIdx], clearMask);
        atomicOr (dataUV[wordIdx], setMask);
    } else {
        atomicAnd(dataY[wordIdx], clearMask);
        atomicOr (dataY[wordIdx], setMask);
    }
}

float clamp01(float v) { return clamp(v, 0.0, 1.0); }

void rgbToYUV(vec3 rgb, out float Y, out float U, out float V) {
    // BT.601
    float y = 0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
    float u = -0.168736 * rgb.r - 0.331264 * rgb.g + 0.5 * rgb.b;
    float v = 0.5 * rgb.r - 0.418688 * rgb.g - 0.081312 * rgb.b;

    if (pc.rangeFull == 1) {
        Y = clamp01(y) * 255.0;
        U = clamp(u + 0.5, 0.0, 1.0) * 255.0;
        V = clamp(v + 0.5, 0.0, 1.0) * 255.0;
    } else {
        // BT.601 limited: Y in [16, 235], UV in [16, 240]
        Y = 16.0  + clamp01(y) * 219.0;
        U = 128.0 + clamp(u, -0.5, 0.5) * 224.0;
        V = 128.0 + clamp(v, -0.5, 0.5) * 224.0;
    }
}

void main() {
    ivec2 block = ivec2(gl_GlobalInvocationID.xy); // blocco 2x2
    int bx2 = block.x * 2;
    int by2 = block.y * 2;
    if (bx2 >= pc.width || by2 >= pc.height) return;

    // 4 sample RGB del blocco
    vec3 rgb00 = sampleRGB(bx2,     by2);
    vec3 rgb10 = sampleRGB(bx2 + 1, by2);
    vec3 rgb01 = sampleRGB(bx2,     by2 + 1);
    vec3 rgb11 = sampleRGB(bx2 + 1, by2 + 1);

    float Y00, U00, V00;
    float Y10, U10, V10;
    float Y01, U01, V01;
    float Y11, U11, V11;
    rgbToYUV(rgb00, Y00, U00, V00);
    rgbToYUV(rgb10, Y10, U10, V10);
    rgbToYUV(rgb01, Y01, U01, V01);
    rgbToYUV(rgb11, Y11, U11, V11);

    // Y: 4 byte distinti
    writeByte(by2     * pc.strideY + bx2,     uint(Y00 + 0.5), false);
    if (bx2 + 1 < pc.width)
        writeByte(by2     * pc.strideY + bx2 + 1, uint(Y10 + 0.5), false);
    if (by2 + 1 < pc.height) {
        writeByte((by2+1) * pc.strideY + bx2,     uint(Y01 + 0.5), false);
        if (bx2 + 1 < pc.width)
            writeByte((by2+1) * pc.strideY + bx2 + 1, uint(Y11 + 0.5), false);
    }

    // UV: media dei 4 chroma, 1 sample
    float U = (U00 + U10 + U01 + U11) * 0.25;
    float V = (V00 + V10 + V01 + V11) * 0.25;

    // NV12: UV interleaved, riga = block.y, byte index = block.x * 2
    int uvBase = block.y * pc.strideU + block.x * 2;
    writeByte(uvBase,     uint(U + 0.5), true);
    writeByte(uvBase + 1, uint(V + 0.5), true);
}
)";

// =============================================================================
//  Shader sorgente YUV420P (3 piani separati Y, U, V)
// =============================================================================
static const char* rgb2yuv_yuv420p_glsl = R"(#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

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
    int padding0;
    int padding1;
} pc;

vec3 sampleRGB(int x, int y) {
    x = clamp(x, 0, pc.width  - 1);
    y = clamp(y, 0, pc.height - 1);
    return imageLoad(inImage, ivec2(x, y)).rgb;
}

#define WRITE_BYTE(BUF, BIDX, VAL)                              \
    {                                                            \
        int _wi = (BIDX) >> 2;                                   \
        int _sh = ((BIDX) & 3) * 8;                              \
        uint _cm = ~(0xFFu << _sh);                              \
        uint _sm = ((VAL) & 0xFFu) << _sh;                       \
        atomicAnd(BUF[_wi], _cm);                                \
        atomicOr (BUF[_wi], _sm);                                \
    }

float clamp01(float v) { return clamp(v, 0.0, 1.0); }

void rgbToYUV(vec3 rgb, out float Y, out float U, out float V) {
    float y = 0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
    float u = -0.168736 * rgb.r - 0.331264 * rgb.g + 0.5 * rgb.b;
    float v = 0.5 * rgb.r - 0.418688 * rgb.g - 0.081312 * rgb.b;

    if (pc.rangeFull == 1) {
        Y = clamp01(y) * 255.0;
        U = clamp(u + 0.5, 0.0, 1.0) * 255.0;
        V = clamp(v + 0.5, 0.0, 1.0) * 255.0;
    } else {
        Y = 16.0  + clamp01(y) * 219.0;
        U = 128.0 + clamp(u, -0.5, 0.5) * 224.0;
        V = 128.0 + clamp(v, -0.5, 0.5) * 224.0;
    }
}

void main() {
    ivec2 block = ivec2(gl_GlobalInvocationID.xy);
    int bx2 = block.x * 2;
    int by2 = block.y * 2;
    if (bx2 >= pc.width || by2 >= pc.height) return;

    vec3 rgb00 = sampleRGB(bx2,     by2);
    vec3 rgb10 = sampleRGB(bx2 + 1, by2);
    vec3 rgb01 = sampleRGB(bx2,     by2 + 1);
    vec3 rgb11 = sampleRGB(bx2 + 1, by2 + 1);

    float Y00, U00, V00, Y10, U10, V10, Y01, U01, V01, Y11, U11, V11;
    rgbToYUV(rgb00, Y00, U00, V00);
    rgbToYUV(rgb10, Y10, U10, V10);
    rgbToYUV(rgb01, Y01, U01, V01);
    rgbToYUV(rgb11, Y11, U11, V11);

    WRITE_BYTE(dataY, by2 * pc.strideY + bx2, uint(Y00 + 0.5))
    if (bx2 + 1 < pc.width)
        WRITE_BYTE(dataY, by2 * pc.strideY + bx2 + 1, uint(Y10 + 0.5))
    if (by2 + 1 < pc.height) {
        WRITE_BYTE(dataY, (by2+1) * pc.strideY + bx2, uint(Y01 + 0.5))
        if (bx2 + 1 < pc.width)
            WRITE_BYTE(dataY, (by2+1) * pc.strideY + bx2 + 1, uint(Y11 + 0.5))
    }

    float U = (U00 + U10 + U01 + U11) * 0.25;
    float V = (V00 + V10 + V01 + V11) * 0.25;

    int uIdx = block.y * pc.strideU + block.x;
    int vIdx = block.y * pc.strideV + block.x;
    WRITE_BYTE(dataU, uIdx, uint(U + 0.5))
    WRITE_BYTE(dataV, vIdx, uint(V + 0.5))
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
        bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
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

    // Lo shader lavora a blocchi 2x2 pixel (1 invocation = 1 blocco), local size 8x8.
    // gx*gy invocations = (w/2)*(h/2), arrotondate per eccesso.
    uint32_t blocksX = (slot.width  + 1) / 2;
    uint32_t blocksY = (slot.height + 1) / 2;
    uint32_t gx = (blocksX + 7) / 8;
    uint32_t gy = (blocksY + 7) / 8;
    vkCmdDispatch(cmd, gx, gy, 1);

    // Barriera: SHADER_WRITE -> HOST_READ sui buffer.
    // L'encoder leggera' i mappedPtrs dopo il wait sul semaforo; questa barriera
    // garantisce visibility lato host.
    int numBufs = (slot.format == RGB2YUVFormat::NV12) ? 2 : 3;
    VkBufferMemoryBarrier bars[3]{};
    for (int i = 0; i < numBufs; ++i) {
        bars[i].sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bars[i].srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        bars[i].dstAccessMask       = VK_ACCESS_HOST_READ_BIT;
        bars[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bars[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bars[i].buffer              = slot.buffers[i];
        bars[i].offset              = 0;
        bars[i].size                = VK_WHOLE_SIZE;
    }
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT,
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

    VkResult waitRes = vkWaitForFences(m_ctx->device, 1, &slot.fence, VK_TRUE, 1'000'000'000);
    if (waitRes != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[RGB2YUV] vkWaitForFences: %d", waitRes);
        return;
    }
    vkResetFences(m_ctx->device, 1, &slot.fence);

    // Update descriptor (sicuro adesso: fence già atteso).
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

    // Sceglie il semaforo binario del ring per segnalare il completamento.
    VkSemaphore signalSem = slot.semRing[slot.semWriteIdx];
    slot.semWriteIdx = (slot.semWriteIdx + 1) % RGB2YUVSlotResources::kSemRing;
    slot.computeFinished = signalSem;

    // === Setup wait/signal con mix di timeline (mixerTimeline) e binari ===
    //
    // Wait:   timeline mixerTimeline @ waitTimelineValue   (se != 0)
    // Signal: ring binario signalSem
    //
    // Il timelineSubmitInfo serve a passare i valori uint64 per i semafori
    // timeline. Per i binari (signalSem) il valore è ignorato — passiamo 0.
    VkSemaphore          waitSems[1]   = { slot.mixerTimeline };
    uint64_t             waitValues[1] = { waitTimelineValue };
    VkPipelineStageFlags waitStages[1] = { waitStage };

    VkSemaphore signalSems[1]   = { signalSem };
    uint64_t    signalValues[1] = { 0 };   // ignored per i binari

    VkTimelineSemaphoreSubmitInfo timelineInfo{
        VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues    = signalValues;

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.pNext                = &timelineInfo;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &slot.cmd;
    si.signalSemaphoreCount = 0;
    si.pSignalSemaphores    = nullptr;

    if (waitTimelineValue != 0) {
        timelineInfo.waitSemaphoreValueCount = 1;
        timelineInfo.pWaitSemaphoreValues    = waitValues;

        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores    = waitSems;
        si.pWaitDstStageMask  = waitStages;
    }

    {
        std::lock_guard<std::mutex> qlock(m_ctx->computeQueueMutex);
        VkResult r = VulkanQueueSubmit(m_ctx->computeQueue, 1, &si, slot.fence,"RGB2YUVPipeline::submitAsync");
        if (r != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[RGB2YUV] vkQueueSubmit: %d", r);
            vkResetFences(m_ctx->device, 1, &slot.fence);
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


