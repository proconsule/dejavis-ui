#include "P010Converter.h"
#include "../YUVConverterCommon.h"
#include <cstring>

static const char* kP010ShaderGLSL = R"(
#version 450 core
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform writeonly image2D outImage;
layout(std430, binding = 1) readonly buffer YBuf  { uint dataY[];  };
layout(std430, binding = 2) readonly buffer UVBuf { uint dataUV[]; };

layout(push_constant) uniform PC {
    int strideY;
    int strideUV;
    int rangeFull;
    int colorSpace;   // AVCOL_SPC_*: 1=BT709, 5=BT470BG, 6=SMPTE170M, 9=BT2020_NCL
    int width;
    int height;
} pc;


uint readU16Y(int byteOfs) {
    int wordOfs = byteOfs >> 2;
    int shift   = (byteOfs & 2) * 8;
    return (dataY[wordOfs] >> shift) & 0xFFFFu;
}
uint readU16UV(int byteOfs) {
    int wordOfs = byteOfs >> 2;
    int shift   = (byteOfs & 2) * 8;
    return (dataUV[wordOfs] >> shift) & 0xFFFFu;
}

vec4 yuv2rgb_10bit(float Y, float U, float V) {
    float yN, uN, vN;
    if (pc.rangeFull == 1) {
        yN =  Y          / 1023.0;
        uN = (U - 512.0) / 1023.0;
        vN = (V - 512.0) / 1023.0;
    } else {
        // Limited range a 10-bit:
        //   Y in [64, 940]   range = 876   ( = 219 * 4)
        //   U/V in [64, 960] range = 896   ( = 224 * 4), zero a 512
        yN = (Y -  64.0) / 876.0;
        uN = (U - 512.0) / 896.0;
        vN = (V - 512.0) / 896.0;
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
    } else {                                // BT.601 fallback
        r = yN + 1.402    * vN;
        g = yN - 0.344136 * uN - 0.714136 * vN;
        b = yN + 1.772    * uN;
    }
    return vec4(clamp(vec3(r, g, b), 0.0, 1.0), 1.0);
}

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= pc.width || px.y >= pc.height) return;

    int yByteOfs = px.y * pc.strideY + px.x * 2;
    float Y = float(readU16Y(yByteOfs) >> 6);   // 10-bit nei MSB

    int cx = px.x >> 1;
    int cy = px.y >> 1;
    int uvByteOfs = cy * pc.strideUV + cx * 4;  // 2 uint16 per coppia
    float U = float(readU16UV(uvByteOfs    ) >> 6);
    float V = float(readU16UV(uvByteOfs + 2) >> 6);

    imageStore(outImage, px, yuv2rgb_10bit(Y, U, V));
}
)";

struct P010PushConstants {
    int strideY;
    int strideUV;
    int rangeFull;
    int colorSpace;
    int width;
    int height;
};

struct P010Slot : public IConverterSlot {
    VkBuffer        bufY = VK_NULL_HANDLE,  bufUV = VK_NULL_HANDLE;
    VkDeviceMemory  memY = VK_NULL_HANDLE,  memUV = VK_NULL_HANDLE;
    void*           mapY = nullptr,         *mapUV = nullptr;
    VkDeviceSize    capY = 0,                capUV = 0;
    int             strideY = 0,             strideUV = 0;
    uint32_t        width = 0,               height = 0;
    int             rangeFull = 0;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet  set  = VK_NULL_HANDLE;
    IYUVConverter::OutputBinding out{};
};

bool P010Converter::init(VulkanContext* ctx) {
    m_ctx = ctx;

    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    bindings[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };

    VkDescriptorSetLayoutCreateInfo li{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    li.bindingCount = 3; li.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(m_ctx->device, &li, nullptr, &m_setLayout) != VK_SUCCESS)
        return false;

    VkPushConstantRange pcr{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(P010PushConstants) };
    VkPipelineLayoutCreateInfo pli{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.setLayoutCount = 1; pli.pSetLayouts = &m_setLayout;
    pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(m_ctx->device, &pli, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        return false;

    VkShaderModule mod = yuvconv::compileComputeShader(m_ctx, kP010ShaderGLSL, "p010.comp");
    if (!mod) return false;

    VkComputePipelineCreateInfo cpi{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = mod; cpi.stage.pName = "main";
    cpi.layout = m_pipelineLayout;
    VkResult r = vkCreateComputePipelines(m_ctx->device, VK_NULL_HANDLE, 1, &cpi, nullptr, &m_pipeline);
    vkDestroyShaderModule(m_ctx->device, mod, nullptr);
    return r == VK_SUCCESS;
}

void P010Converter::shutdown() {
    if (!m_ctx) return;
    if (m_pipeline)       vkDestroyPipeline(m_ctx->device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_ctx->device, m_pipelineLayout, nullptr);
    if (m_setLayout)      vkDestroyDescriptorSetLayout(m_ctx->device, m_setLayout, nullptr);
    m_pipeline = VK_NULL_HANDLE; m_pipelineLayout = VK_NULL_HANDLE; m_setLayout = VK_NULL_HANDLE;
}

std::unique_ptr<IConverterSlot>
P010Converter::createSlot(uint32_t w, uint32_t h, const OutputBinding& out) {
    auto s = std::make_unique<P010Slot>();
    s->out = out; s->width = w; s->height = h;

    const uint32_t alignedY  = yuvconv::alignUp(w * 2, yuvconv::kRowAlign);
    const uint32_t alignedUV = yuvconv::alignUp(w * 2, yuvconv::kRowAlign);
    s->strideY  = (int)alignedY;
    s->strideUV = (int)alignedUV;

    const VkDeviceSize ySize  = (VkDeviceSize)alignedY  * h;
    const VkDeviceSize uvSize = (VkDeviceSize)alignedUV * (h / 2);

    if (!yuvconv::createMappedStorageBuffer(m_ctx, ySize,  s->bufY,  s->memY,  &s->mapY,  s->capY))  return nullptr;
    if (!yuvconv::createMappedStorageBuffer(m_ctx, uvSize, s->bufUV, s->memUV, &s->mapUV, s->capUV)) return nullptr;

    VkDescriptorPoolSize ps[2]{};
    ps[0] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1 };
    ps[1] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 };
    VkDescriptorPoolCreateInfo pi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pi.maxSets = 1; pi.poolSizeCount = 2; pi.pPoolSizes = ps;
    if (vkCreateDescriptorPool(m_ctx->device, &pi, nullptr, &s->pool) != VK_SUCCESS) return nullptr;

    VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ai.descriptorPool = s->pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &m_setLayout;
    if (vkAllocateDescriptorSets(m_ctx->device, &ai, &s->set) != VK_SUCCESS) return nullptr;

    VkDescriptorImageInfo  ii{ VK_NULL_HANDLE, out.view, VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorBufferInfo bY  { s->bufY,  0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo bUV { s->bufUV, 0, VK_WHOLE_SIZE };

    VkWriteDescriptorSet ws[3]{};
    ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[0].dstSet = s->set; ws[0].dstBinding = 0; ws[0].descriptorCount = 1;
    ws[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;  ws[0].pImageInfo  = &ii;
    ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[1].dstSet = s->set; ws[1].dstBinding = 1; ws[1].descriptorCount = 1;
    ws[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; ws[1].pBufferInfo = &bY;
    ws[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[2].dstSet = s->set; ws[2].dstBinding = 2; ws[2].descriptorCount = 1;
    ws[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; ws[2].pBufferInfo = &bUV;
    vkUpdateDescriptorSets(m_ctx->device, 3, ws, 0, nullptr);
    return s;
}

void P010Converter::destroySlot(IConverterSlot* slot) {
    auto* s = static_cast<P010Slot*>(slot);
    if (!s) return;
    if (s->pool) vkDestroyDescriptorPool(m_ctx->device, s->pool, nullptr);
    yuvconv::destroyMappedBuffer(m_ctx, s->bufY,  s->memY,  &s->mapY);
    yuvconv::destroyMappedBuffer(m_ctx, s->bufUV, s->memUV, &s->mapUV);
}

bool P010Converter::uploadFrame(IConverterSlot* slot, AVFrame* f) {
    auto* s = static_cast<P010Slot*>(slot);
    if (!s || !f) return false;
    if (f->format != AV_PIX_FMT_P010LE) return false;
    if ((uint32_t)f->width != s->width || (uint32_t)f->height != s->height) return false;
    if (!f->data[0] || !f->data[1]) return false;

    s->strideY  = f->linesize[0];
    s->strideUV = f->linesize[1];

    const size_t bY  = (size_t)f->linesize[0] * f->height;
    const size_t bUV = (size_t)f->linesize[1] * (f->height / 2);
    if (bY > s->capY || bUV > s->capUV) {
        DEJAVISUI_LOG_ERROR("[P010] plane oversize Y=%zu/%llu UV=%zu/%llu",
                            bY,  (unsigned long long)s->capY,
                            bUV, (unsigned long long)s->capUV);
        return false;
    }
    memcpy(s->mapY,  f->data[0], bY);
    memcpy(s->mapUV, f->data[1], bUV);

    // Flush memoria mappata (necessario se non HOST_COHERENT)
    VkMappedMemoryRange ranges[2] = {};
    ranges[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    ranges[0].memory = s->memY;
    ranges[0].offset = 0;
    ranges[0].size = bY;

    ranges[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    ranges[1].memory = s->memUV;
    ranges[1].offset = 0;
    ranges[1].size = bUV;

    vkFlushMappedMemoryRanges(m_ctx->device, 2, ranges);
    return true;
}

void P010Converter::recordDispatch(VkCommandBuffer cmd, IConverterSlot* slot,
                                   const FrameMetadata& meta,
                                   VkImageLayout& outCurrentLayout) {
    auto* s = static_cast<P010Slot*>(slot);
    if (!s || !m_pipeline) return;

    yuvconv::bufferBarrier(cmd, s->bufY,
        VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    yuvconv::bufferBarrier(cmd, s->bufUV,
        VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    yuvconv::imageBarrier(cmd, s->out.image,
        outCurrentLayout, VK_IMAGE_LAYOUT_GENERAL,
        0, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    outCurrentLayout = VK_IMAGE_LAYOUT_GENERAL;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    P010PushConstants pc{ s->strideY, s->strideUV, meta.rangeFull, meta.colorSpace, (int)s->width, (int)s->height };
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_pipelineLayout, 0, 1, &s->set, 0, nullptr);
    vkCmdDispatch(cmd, (s->width + 15) / 16, (s->height + 15) / 16, 1);

    yuvconv::imageBarrier(cmd, s->out.image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    outCurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

bool P010Converter::uploadNDIFrame(IConverterSlot* slot, const NDIlib_video_frame_v2_t& v) {
    if (!slot) return false;
    auto* s = static_cast<P010Slot*>(slot);

    const uint32_t h = (uint32_t)v.yres;
    const uint32_t w = (uint32_t)v.xres;

    if (w != s->width || h != s->height) return false;

    const uint8_t* yPlane  = v.p_data;
    const int      yStride = v.line_stride_in_bytes;
    const uint8_t* uvPlane = yPlane + (size_t)yStride * h;
    const int      uvStride = v.line_stride_in_bytes;

    s->strideY  = yStride;
    s->strideUV = uvStride;

    if (!s->mapY || !s->mapUV) return false;


    const size_t bY  = (size_t)yStride  * h;
    const size_t bUV = (size_t)uvStride * (h / 2);

    if (bY > s->capY || bUV > s->capUV) return false;

    memcpy(s->mapY,  yPlane,  bY);
    memcpy(s->mapUV, uvPlane, bUV);

    VkMappedMemoryRange ranges[2] = {};
    ranges[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    ranges[0].memory = s->memY;
    ranges[0].offset = 0;
    ranges[0].size = bY;
    ranges[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    ranges[1].memory = s->memUV;
    ranges[1].offset = 0;
    ranges[1].size = bUV;

    vkFlushMappedMemoryRanges(m_ctx->device, 2, ranges);
    return true;
}