#include "UYVYConverter.h"
#include "../YUVConverterCommon.h"
#include <cstring>

static const char* kUYVYShaderGLSL = R"(#version 450 core
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform writeonly image2D outImage;
layout(std430, binding = 1) readonly buffer UYVYBuf { uint data[]; };

layout(push_constant) uniform PC {
    int stride;
    int rangeFull;
    int colorSpace;
    int width;
    int height;
} pc;

float byteAt(int ofs) {
    return float((data[ofs >> 2] >> ((ofs & 3) * 8)) & 0xFFu);
}

vec4 yuv2rgb(float Y, float U, float V) {
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

    float r, g, b;
    if (pc.colorSpace == 9) {              // BT.2020 NCL
        r = yN + 1.4746   * vN;
        g = yN - 0.16455  * uN - 0.57135 * vN;
        b = yN + 1.8814   * uN;
    } else if (pc.colorSpace == 1) {       // BT.709
        r = yN + 1.5748   * vN;
        g = yN - 0.18733  * uN - 0.46812 * vN;
        b = yN + 1.8556   * uN;
    } else {                                // BT.601 fallback (5, 6, unspecified)
        r = yN + 1.402    * vN;
        g = yN - 0.344136 * uN - 0.714136 * vN;
        b = yN + 1.772    * uN;
    }
    return vec4(clamp(vec3(r, g, b), 0.0, 1.0), 1.0);
}

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= pc.width || px.y >= pc.height) return;

    int pairX = px.x & ~1;
    int base  = px.y * pc.stride + pairX * 2;

    float u  = byteAt(base + 0);
    float y0 = byteAt(base + 1);
    float v  = byteAt(base + 2);
    float y1 = byteAt(base + 3);
    float Y  = ((px.x & 1) == 0) ? y0 : y1;

    imageStore(outImage, px, yuv2rgb(Y, u, v));
}
)";

struct UYVYPushConstants {
    int stride;
    int rangeFull;
    int colorSpace;
    int width;
    int height;
};

struct UYVYSlot : public IConverterSlot {
    VkBuffer        buffer       = VK_NULL_HANDLE;
    VkDeviceMemory  memory       = VK_NULL_HANDLE;
    void*           mapped       = nullptr;
    VkDeviceSize    capacity     = 0;
    int             stride       = 0;
    uint32_t        width        = 0;
    uint32_t        height       = 0;
    int             rangeFull    = 0;

    VkDescriptorPool pool        = VK_NULL_HANDLE;
    VkDescriptorSet  set         = VK_NULL_HANDLE;

    IYUVConverter::OutputBinding out{};
};

bool UYVYConverter::init(VulkanContext* ctx) {
    m_ctx = ctx;

    // Descriptor set layout: 0 = storage image (out), 1 = storage buffer (in)
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo li{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    li.bindingCount = 2;
    li.pBindings    = bindings;
    if (vkCreateDescriptorSetLayout(m_ctx->device, &li, nullptr, &m_setLayout) != VK_SUCCESS) {
        return false;
    }

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(UYVYPushConstants);

    VkPipelineLayoutCreateInfo pli{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.setLayoutCount         = 1;
    pli.pSetLayouts            = &m_setLayout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pcr;
    if (vkCreatePipelineLayout(m_ctx->device, &pli, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        return false;
    }

    VkShaderModule mod = yuvconv::compileComputeShader(m_ctx, kUYVYShaderGLSL, "uyvy.comp");
    if (!mod) return false;

    VkComputePipelineCreateInfo cpi{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    cpi.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = mod;
    cpi.stage.pName  = "main";
    cpi.layout       = m_pipelineLayout;
    VkResult r = vkCreateComputePipelines(m_ctx->device, VK_NULL_HANDLE, 1, &cpi, nullptr, &m_pipeline);
    vkDestroyShaderModule(m_ctx->device, mod, nullptr);
    return r == VK_SUCCESS;
}

void UYVYConverter::shutdown() {
    if (!m_ctx) return;
    if (m_pipeline)       vkDestroyPipeline(m_ctx->device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_ctx->device, m_pipelineLayout, nullptr);
    if (m_setLayout)      vkDestroyDescriptorSetLayout(m_ctx->device, m_setLayout, nullptr);
    m_pipeline = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_setLayout = VK_NULL_HANDLE;
}

std::unique_ptr<IConverterSlot>
UYVYConverter::createSlot(uint32_t w, uint32_t h, const OutputBinding& out) {
    auto s = std::make_unique<UYVYSlot>();
    s->out    = out;
    s->width  = w;
    s->height = h;

    // UYVY: 2 byte per pixel. Allinea la riga a 256B per uniformita'.
    const uint32_t alignedRow = yuvconv::alignUp(w * 2, yuvconv::kRowAlign);
    s->stride = (int)alignedRow;
    const VkDeviceSize bufSize = (VkDeviceSize)alignedRow * h;

    if (!yuvconv::createMappedStorageBuffer(m_ctx, bufSize,
                                            s->buffer, s->memory,
                                            &s->mapped, s->capacity)) {
        return nullptr;
    }


    VkDescriptorPoolSize ps[2]{};
    ps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;  ps[0].descriptorCount = 1;
    ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; ps[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo pi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pi.maxSets       = 1;
    pi.poolSizeCount = 2;
    pi.pPoolSizes    = ps;
    if (vkCreateDescriptorPool(m_ctx->device, &pi, nullptr, &s->pool) != VK_SUCCESS) {
        return nullptr;
    }

    VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ai.descriptorPool     = s->pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &m_setLayout;
    if (vkAllocateDescriptorSets(m_ctx->device, &ai, &s->set) != VK_SUCCESS) {
        return nullptr;
    }

    VkDescriptorImageInfo ii{};
    ii.imageView   = out.view;
    ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo bi{};
    bi.buffer = s->buffer;
    bi.offset = 0;
    bi.range  = VK_WHOLE_SIZE;

    VkWriteDescriptorSet ws[2]{};
    ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[0].dstSet = s->set; ws[0].dstBinding = 0; ws[0].descriptorCount = 1;
    ws[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ws[0].pImageInfo = &ii;
    ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[1].dstSet = s->set; ws[1].dstBinding = 1; ws[1].descriptorCount = 1;
    ws[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ws[1].pBufferInfo = &bi;
    vkUpdateDescriptorSets(m_ctx->device, 2, ws, 0, nullptr);

    return s;
}

void UYVYConverter::destroySlot(IConverterSlot* slot) {
    auto* s = static_cast<UYVYSlot*>(slot);
    if (!s) return;
    if (s->pool) vkDestroyDescriptorPool(m_ctx->device, s->pool, nullptr);
    yuvconv::destroyMappedBuffer(m_ctx, s->buffer, s->memory, &s->mapped);
}

bool UYVYConverter::uploadFrame(IConverterSlot* slot, AVFrame* f) {
    auto* s = static_cast<UYVYSlot*>(slot);
    if (!s || !f) return false;
    if (f->format != AV_PIX_FMT_UYVY422) return false;
    if ((uint32_t)f->width != s->width || (uint32_t)f->height != s->height) {
        DEJAVISUI_LOG_ERROR("[UYVY] dimensioni inattese %dx%d (slot %ux%u)",
                            f->width, f->height, s->width, s->height);
        return false;
    }
    if (!f->data[0]) return false;

    s->stride = f->linesize[0];
    const size_t bytes = (size_t)f->linesize[0] * f->height;
    if (bytes > s->capacity) {
        DEJAVISUI_LOG_ERROR("[UYVY] plane oversize: %zu > %llu",
                            bytes, (unsigned long long)s->capacity);
        return false;
    }
    memcpy(s->mapped, f->data[0], bytes);

    // Flush memoria mappata (necessario se non HOST_COHERENT)
    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = s->memory;
    range.offset = 0;
    range.size = bytes;

    vkFlushMappedMemoryRanges(m_ctx->device, 1, &range);
    return true;
}

void UYVYConverter::recordDispatch(VkCommandBuffer cmd, IConverterSlot* slot,
                                   const FrameMetadata& meta,
                                   VkImageLayout& outCurrentLayout) {
    auto* s = static_cast<UYVYSlot*>(slot);
    if (!s || !m_pipeline) return;

    // Buffer barrier: HOST_WRITE -> SHADER_READ
    yuvconv::bufferBarrier(cmd, s->buffer,
        VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // Image: layout corrente -> GENERAL
    yuvconv::imageBarrier(cmd, s->out.image,
        outCurrentLayout, VK_IMAGE_LAYOUT_GENERAL,
        0, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    outCurrentLayout = VK_IMAGE_LAYOUT_GENERAL;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);

    UYVYPushConstants pc{
        s->stride,
        meta.rangeFull,
        meta.colorSpace,
        static_cast<int>(s->width),
        static_cast<int>(s->height)
    };
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_pipelineLayout, 0, 1, &s->set, 0, nullptr);

    uint32_t gx = (s->width  + 15) / 16;
    uint32_t gy = (s->height + 15) / 16;
    vkCmdDispatch(cmd, gx, gy, 1);

    // GENERAL -> SHADER_READ_ONLY_OPTIMAL
    yuvconv::imageBarrier(cmd, s->out.image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    outCurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

bool UYVYConverter::uploadNDIFrame(IConverterSlot* slot, const NDIlib_video_frame_v2_t& v) {
    if (!slot) return false;
    auto* s = static_cast<UYVYSlot*>(slot);

    const uint32_t h = (uint32_t)v.yres;
    const uint32_t w = (uint32_t)v.xres;

    if (w != s->width || h != s->height) return false;

    const uint8_t* data = v.p_data;
    const int      stride = v.line_stride_in_bytes;

    s->stride = stride;

    if (!s->mapped) return false;

    const size_t bytes = (size_t)stride * h;
    if (bytes > s->capacity) return false;

    memcpy(s->mapped, data, bytes);

    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = s->memory;
    range.offset = 0;
    range.size = bytes;

    vkFlushMappedMemoryRanges(m_ctx->device, 1, &range);
    return true;
}