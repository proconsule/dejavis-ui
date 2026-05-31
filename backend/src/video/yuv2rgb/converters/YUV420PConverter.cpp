#include "YUV420PConverter.h"
#include "../YUVConverterCommon.h"
#include <cstring>

static const char* kYUV420PShaderGLSL = R"(#version 450 core
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform writeonly image2D outImage;
layout(std430, binding = 1) readonly buffer YBuf { uint dataY[]; };
layout(std430, binding = 2) readonly buffer UBuf { uint dataU[]; };
layout(std430, binding = 3) readonly buffer VBuf { uint dataV[]; };

layout(push_constant) uniform PC {
    int strideY;
    int strideU;
    int strideV;
    int rangeFull;
    int colorSpace;
    int width;
    int height;
} pc;

float byteY(int ofs) { return float((dataY[ofs >> 2] >> ((ofs & 3) * 8)) & 0xFFu); }
float byteU(int ofs) { return float((dataU[ofs >> 2] >> ((ofs & 3) * 8)) & 0xFFu); }
float byteV(int ofs) { return float((dataV[ofs >> 2] >> ((ofs & 3) * 8)) & 0xFFu); }

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

    float Y = byteY(px.y * pc.strideY + px.x);
    int cx = px.x >> 1, cy = px.y >> 1;
    float U = byteU(cy * pc.strideU + cx);
    float V = byteV(cy * pc.strideV + cx);

    imageStore(outImage, px, yuv2rgb(Y, U, V));
}
)";

struct YUV420PPushConstants {
    int strideY;
    int strideU;
    int strideV;
    int rangeFull;
    int colorSpace;
    int width;
    int height;
};

struct YUV420PSlot : public IConverterSlot {
    VkBuffer        buf[3]   = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory  mem[3]   = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    void*           mapped[3]= { nullptr, nullptr, nullptr };
    VkDeviceSize    cap[3]   = { 0, 0, 0 };
    int             stride[3]= { 0, 0, 0 };
    uint32_t        width = 0, height = 0;
    int             rangeFull = 0;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet  set  = VK_NULL_HANDLE;

    IYUVConverter::OutputBinding out{};
};

bool YUV420PConverter::init(VulkanContext* ctx) {
    m_ctx = ctx;

    VkDescriptorSetLayoutBinding bindings[4]{};
    bindings[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    bindings[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    bindings[3] = { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };

    VkDescriptorSetLayoutCreateInfo li{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    li.bindingCount = 4; li.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(m_ctx->device, &li, nullptr, &m_setLayout) != VK_SUCCESS)
        return false;

    VkPushConstantRange pcr{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(YUV420PPushConstants) };
    VkPipelineLayoutCreateInfo pli{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.setLayoutCount = 1; pli.pSetLayouts = &m_setLayout;
    pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(m_ctx->device, &pli, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        return false;

    VkShaderModule mod = yuvconv::compileComputeShader(m_ctx, kYUV420PShaderGLSL, "yuv420p.comp");
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

void YUV420PConverter::shutdown() {
    if (!m_ctx) return;
    if (m_pipeline)       vkDestroyPipeline(m_ctx->device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_ctx->device, m_pipelineLayout, nullptr);
    if (m_setLayout)      vkDestroyDescriptorSetLayout(m_ctx->device, m_setLayout, nullptr);
    m_pipeline = VK_NULL_HANDLE; m_pipelineLayout = VK_NULL_HANDLE; m_setLayout = VK_NULL_HANDLE;
}

std::unique_ptr<IConverterSlot>
YUV420PConverter::createSlot(uint32_t w, uint32_t h, const OutputBinding& out) {
    auto s = std::make_unique<YUV420PSlot>();
    s->out = out; s->width = w; s->height = h;

    const uint32_t alignedY = yuvconv::alignUp(w,     yuvconv::kRowAlign);
    const uint32_t alignedC = yuvconv::alignUp(w / 2, yuvconv::kRowAlign);
    s->stride[0] = (int)alignedY;
    s->stride[1] = (int)alignedC;
    s->stride[2] = (int)alignedC;

    const VkDeviceSize sizes[3] = {
        (VkDeviceSize)alignedY * h,
        (VkDeviceSize)alignedC * (h / 2),
        (VkDeviceSize)alignedC * (h / 2)
    };
    for (int i = 0; i < 3; ++i) {
        if (!yuvconv::createMappedStorageBuffer(m_ctx, sizes[i],
                                                s->buf[i], s->mem[i],
                                                &s->mapped[i], s->cap[i])) {
            return nullptr;
        }
    }

    VkDescriptorPoolSize ps[2]{};
    ps[0] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1 };
    ps[1] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 };
    VkDescriptorPoolCreateInfo pi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pi.maxSets = 1; pi.poolSizeCount = 2; pi.pPoolSizes = ps;
    if (vkCreateDescriptorPool(m_ctx->device, &pi, nullptr, &s->pool) != VK_SUCCESS) return nullptr;

    VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ai.descriptorPool = s->pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &m_setLayout;
    if (vkAllocateDescriptorSets(m_ctx->device, &ai, &s->set) != VK_SUCCESS) return nullptr;

    VkDescriptorImageInfo  ii{ VK_NULL_HANDLE, out.view, VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorBufferInfo bb[3] = {
        { s->buf[0], 0, VK_WHOLE_SIZE },
        { s->buf[1], 0, VK_WHOLE_SIZE },
        { s->buf[2], 0, VK_WHOLE_SIZE }
    };
    VkWriteDescriptorSet ws[4]{};
    ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[0].dstSet = s->set; ws[0].dstBinding = 0; ws[0].descriptorCount = 1;
    ws[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;  ws[0].pImageInfo  = &ii;
    for (int i = 0; i < 3; ++i) {
        ws[1+i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[1+i].dstSet = s->set; ws[1+i].dstBinding = 1+i; ws[1+i].descriptorCount = 1;
        ws[1+i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws[1+i].pBufferInfo = &bb[i];
    }
    vkUpdateDescriptorSets(m_ctx->device, 4, ws, 0, nullptr);

    return s;
}

void YUV420PConverter::destroySlot(IConverterSlot* slot) {
    auto* s = static_cast<YUV420PSlot*>(slot);
    if (!s) return;
    if (s->pool) vkDestroyDescriptorPool(m_ctx->device, s->pool, nullptr);
    for (int i = 0; i < 3; ++i)
        yuvconv::destroyMappedBuffer(m_ctx, s->buf[i], s->mem[i], &s->mapped[i]);
}

bool YUV420PConverter::uploadFrame(IConverterSlot* slot, AVFrame* f) {
    auto* s = static_cast<YUV420PSlot*>(slot);
    if (!s || !f) return false;
    if (f->format != AV_PIX_FMT_YUV420P) return false;
    if ((uint32_t)f->width != s->width || (uint32_t)f->height != s->height) return false;
    if (!f->data[0] || !f->data[1] || !f->data[2]) return false;

    s->stride[0] = f->linesize[0];
    s->stride[1] = f->linesize[1];
    s->stride[2] = f->linesize[2];

    const size_t bytes[3] = {
        (size_t)f->linesize[0] * f->height,
        (size_t)f->linesize[1] * (f->height / 2),
        (size_t)f->linesize[2] * (f->height / 2)
    };
    for (int i = 0; i < 3; ++i) {
        if (bytes[i] > s->cap[i]) {
            DEJAVISUI_LOG_ERROR("[YUV420P] plane[%d] oversize %zu > %llu",
                                i, bytes[i], (unsigned long long)s->cap[i]);
            return false;
        }
        memcpy(s->mapped[i], f->data[i], bytes[i]);
    }


    VkMappedMemoryRange ranges[3] = {};
    for (int i = 0; i < 3; ++i) {
        ranges[i].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        ranges[i].memory = s->mem[i];
        ranges[i].offset = 0;
        ranges[i].size = bytes[i];
    }

    vkFlushMappedMemoryRanges(m_ctx->device, 3, ranges);
    return true;
}

void YUV420PConverter::recordDispatch(VkCommandBuffer cmd, IConverterSlot* slot,
                                   const FrameMetadata& meta,
                                   VkImageLayout& outCurrentLayout) {
    auto* s = static_cast<YUV420PSlot*>(slot);
    if (!s || !m_pipeline) return;

    for (int i = 0; i < 3; ++i) {
        yuvconv::bufferBarrier(cmd, s->buf[i],
            VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    yuvconv::imageBarrier(cmd, s->out.image,
        outCurrentLayout, VK_IMAGE_LAYOUT_GENERAL,
        0, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    outCurrentLayout = VK_IMAGE_LAYOUT_GENERAL;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    YUV420PPushConstants pc{
        s->stride[0],
        s->stride[1],
        s->stride[2],
        meta.rangeFull,
        meta.colorSpace,
        static_cast<int>(s->width),
        static_cast<int>(s->height)
    };
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

bool YUV420PConverter::uploadNDIFrame(IConverterSlot* slot, const NDIlib_video_frame_v2_t& v) {
    if (!slot) return false;
    auto* s = static_cast<YUV420PSlot*>(slot);

    DEJAVISUI_LOG_WARN("[YUV420P] NDI non supportato in formato Planar");
    return false;
}