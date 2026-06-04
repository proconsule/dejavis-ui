#include "videofx_pipeline.h"

#include <cassert>

#include "videofx_shader_sources.h"
#include "../logger.h"
#include "vulkan_utils.h"

#include <vector>
#include <cstring>
#include <shaderc/shaderc.hpp>

// =========================================================================
//  Init / Shutdown
// =========================================================================
bool VideoFXPipeline::init(VulkanContext* ctx) {
    if (m_initialized) return true;
    m_ctx = ctx;

    if (!createDescriptorLayouts()) return false;
    if (!createPipelines()) {
        if (m_chromaLayout) vkDestroyDescriptorSetLayout(m_ctx->device, m_chromaLayout, nullptr);
        if (m_colorLayout)  vkDestroyDescriptorSetLayout(m_ctx->device, m_colorLayout, nullptr);
        m_chromaLayout = m_colorLayout = VK_NULL_HANDLE;
        return false;
    }

    VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.queueFamilyIndex = m_ctx->computeQueueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_ctx->device, &poolInfo, nullptr, &m_computeCommandPool) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[VideoFX] Fallita creazione compute command pool");
        return false;
    }
    m_initialized = true;
    DEJAVISUI_LOG_INFO("[VideoFX] Pipeline initialized");
    return true;
}

void VideoFXPipeline::shutdown() {
    if (!m_initialized) return;
    vkDeviceWaitIdle(m_ctx->device);
    if (m_chromaPipeline)       vkDestroyPipeline(m_ctx->device, m_chromaPipeline, nullptr);
    if (m_lumaPipeline)         vkDestroyPipeline(m_ctx->device, m_lumaPipeline, nullptr);
    if (m_colorPipeline)        vkDestroyPipeline(m_ctx->device, m_colorPipeline, nullptr);
    if (m_chromaPipelineLayout) vkDestroyPipelineLayout(m_ctx->device, m_chromaPipelineLayout, nullptr);
    if (m_lumaPipelineLayout)   vkDestroyPipelineLayout(m_ctx->device, m_lumaPipelineLayout, nullptr);
    if (m_colorPipelineLayout)  vkDestroyPipelineLayout(m_ctx->device, m_colorPipelineLayout, nullptr);
    if (m_chromaLayout)         vkDestroyDescriptorSetLayout(m_ctx->device, m_chromaLayout, nullptr);
    if (m_colorLayout)          vkDestroyDescriptorSetLayout(m_ctx->device, m_colorLayout, nullptr);
    m_chromaPipeline       = VK_NULL_HANDLE;
    m_lumaPipeline         = VK_NULL_HANDLE;
    m_colorPipeline        = VK_NULL_HANDLE;
    m_chromaPipelineLayout = VK_NULL_HANDLE;
    m_lumaPipelineLayout   = VK_NULL_HANDLE;
    m_colorPipelineLayout  = VK_NULL_HANDLE;
    m_chromaLayout         = VK_NULL_HANDLE;
    m_colorLayout          = VK_NULL_HANDLE;
    m_initialized = false;
}

bool VideoFXPipeline::createDescriptorLayouts() {
    // Chroma layout: sampler2D + storage image
    {
        VkDescriptorSetLayoutBinding b[2] = {};
        b[0].binding         = 0;
        b[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[0].descriptorCount = 1;
        b[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        b[1].binding         = 1;
        b[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b[1].descriptorCount = 1;
        b[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = 2;
        info.pBindings    = b;
        if (vkCreateDescriptorSetLayout(m_ctx->device, &info, nullptr, &m_chromaLayout) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[VideoFX] chroma layout create failed");
            return false;
        }
    }
    // Color layout: storage image (read) + storage image (write)
    {
        VkDescriptorSetLayoutBinding b[2] = {};
        b[0].binding         = 0;
        b[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b[0].descriptorCount = 1;
        b[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        b[1].binding         = 1;
        b[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b[1].descriptorCount = 1;
        b[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = 2;
        info.pBindings    = b;
        if (vkCreateDescriptorSetLayout(m_ctx->device, &info, nullptr, &m_colorLayout) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[VideoFX] color layout create failed");
            return false;
        }
    }
    return true;
}

VkShaderModule VideoFXPipeline::loadShader(const char* glslSource, const char* name) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);

    shaderc::SpvCompilationResult res = compiler.CompileGlslToSpv(
        glslSource, shaderc_glsl_compute_shader, name, options);
    if (res.GetCompilationStatus() != shaderc_compilation_status_success) {
        DEJAVISUI_LOG_ERROR("[VideoFX] %s compile error:\n%s",
                            name, res.GetErrorMessage().c_str());
        return VK_NULL_HANDLE;
    }
    std::vector<uint32_t> spv(res.cbegin(), res.cend());
    return CreateShaderModule(spv.data(), spv.size() * sizeof(uint32_t));
}

VkShaderModule VideoFXPipeline::CreateShaderModule(const uint32_t* code, size_t sizeInBytes) {
    VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    info.codeSize = sizeInBytes;
    info.pCode    = code;
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_ctx->device, &info, nullptr, &mod) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[VideoFX] vkCreateShaderModule failed");
        return VK_NULL_HANDLE;
    }
    return mod;
}

VkShaderModule VideoFXPipeline::createChromaShader() {
    return loadShader(videofx_chromakey_glsl, "videofx_chromakey.comp");
}

VkShaderModule VideoFXPipeline::createLumaShader() {
    return loadShader(videofx_lumakey_glsl, "videofx_lumakey.comp");
}

VkShaderModule VideoFXPipeline::createColorShader() {
    return loadShader(videofx_color_glsl, "videofx_color.comp");
}

bool VideoFXPipeline::createPipelines() {
    // Push constants chroma: KeyerPushConstants (32 byte, vec3 + 5 float in std430)
    VkPushConstantRange chromaPCR{};
    chromaPCR.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    chromaPCR.offset     = 0;
    chromaPCR.size       = sizeof(KeyerPushConstants);

    // Push constants luma: LumaPushConstants (20 byte, 5 float lineari)
    VkPushConstantRange lumaPCR{};
    lumaPCR.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    lumaPCR.offset     = 0;
    lumaPCR.size       = sizeof(LumaPushConstants);

    // Push constants color: ColorParams (32 byte)
    VkPushConstantRange colorPCR{};
    colorPCR.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    colorPCR.offset     = 0;
    colorPCR.size       = sizeof(ColorParams);

    // Chroma pipeline layout
    {
        VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        info.setLayoutCount         = 1;
        info.pSetLayouts            = &m_chromaLayout;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges    = &chromaPCR;
        if (vkCreatePipelineLayout(m_ctx->device, &info, nullptr, &m_chromaPipelineLayout) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[VideoFX] chroma pipeline layout failed");
            return false;
        }
    }
    // Luma pipeline layout — stesso descriptor set layout del chroma, push range diverso
    {
        VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        info.setLayoutCount         = 1;
        info.pSetLayouts            = &m_chromaLayout;        // stesso descriptor
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges    = &lumaPCR;               // push diverso
        if (vkCreatePipelineLayout(m_ctx->device, &info, nullptr, &m_lumaPipelineLayout) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[VideoFX] luma pipeline layout failed");
            return false;
        }
    }
    // Color pipeline layout
    {
        VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        info.setLayoutCount         = 1;
        info.pSetLayouts            = &m_colorLayout;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges    = &colorPCR;
        if (vkCreatePipelineLayout(m_ctx->device, &info, nullptr, &m_colorPipelineLayout) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[VideoFX] color pipeline layout failed");
            return false;
        }
    }

    VkShaderModule sChroma = createChromaShader();
    VkShaderModule sLuma   = createLumaShader();
    VkShaderModule sColor  = createColorShader();
    if (!sChroma || !sLuma || !sColor) {
        if (sChroma) vkDestroyShaderModule(m_ctx->device, sChroma, nullptr);
        if (sLuma)   vkDestroyShaderModule(m_ctx->device, sLuma,   nullptr);
        if (sColor)  vkDestroyShaderModule(m_ctx->device, sColor,  nullptr);
        return false;
    }

    VkComputePipelineCreateInfo info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    info.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.pName  = "main";

    // Ognuna ha il SUO pipeline layout — niente condivisione di push range.
    info.stage.module = sChroma;
    info.layout       = m_chromaPipelineLayout;
    VkResult r1 = vkCreateComputePipelines(m_ctx->device, VK_NULL_HANDLE, 1, &info, nullptr, &m_chromaPipeline);

    info.stage.module = sLuma;
    info.layout       = m_lumaPipelineLayout;
    VkResult r3 = vkCreateComputePipelines(m_ctx->device, VK_NULL_HANDLE, 1, &info, nullptr, &m_lumaPipeline);

    info.stage.module = sColor;
    info.layout       = m_colorPipelineLayout;
    VkResult r2 = vkCreateComputePipelines(m_ctx->device, VK_NULL_HANDLE, 1, &info, nullptr, &m_colorPipeline);

    vkDestroyShaderModule(m_ctx->device, sChroma, nullptr);
    vkDestroyShaderModule(m_ctx->device, sLuma,   nullptr);
    vkDestroyShaderModule(m_ctx->device, sColor,  nullptr);

    if (r1 != VK_SUCCESS || r2 != VK_SUCCESS || r3 != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[VideoFX] pipeline create failed (chroma=%d luma=%d color=%d)",
                            r1, r3, r2);
        return false;
    }
    return true;
}


bool VideoFXPipeline::createTargetImage(uint32_t w, uint32_t h, VkImageUsageFlags usage,
                                        VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView)
{
    VkImageCreateInfo imgInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent        = { w, h, 1 };
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = usage;
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_ctx->device, &imgInfo, nullptr, &outImage) != VK_SUCCESS) return false;

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_ctx->device, outImage, &memReq);

    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(m_ctx,memReq.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) return false;
    if (vkAllocateMemory(m_ctx->device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) return false;
    vkBindImageMemory(m_ctx->device, outImage, outMemory, 0);

    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image            = outImage;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(m_ctx->device, &viewInfo, nullptr, &outView) != VK_SUCCESS) return false;

    return true;
}

// =========================================================================
//  createSlot / destroySlot
// =========================================================================
bool VideoFXPipeline::createSlot(VideoFXSlotResources& slot,
                                 uint32_t width, uint32_t height,
                                 VkImageView srcView, VkSampler srcSampler,
                                 VkDescriptorSetLayout mixerSamplerLayout)
{
    if (!m_initialized) return false;

    slot.width = width;
    slot.height = height;

    VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    if (!createTargetImage(width, height, usage,
                           slot.stage1Image, slot.stage1Memory, slot.stage1View)) {
        DEJAVISUI_LOG_ERROR("[VideoFX] stage1 image failed");
        return false;
    }
    if (!createTargetImage(width, height, usage,
                           slot.finalImage, slot.finalMemory, slot.finalView)) {
        DEJAVISUI_LOG_ERROR("[VideoFX] final image failed");
        return false;
    }

    // Sampler per il mixer
    {
        VkSamplerCreateInfo s{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        s.magFilter = VK_FILTER_LINEAR;
        s.minFilter = VK_FILTER_LINEAR;
        s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        if (vkCreateSampler(m_ctx->device, &s, nullptr, &slot.finalSampler) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[VideoFX] sampler failed");
            return false;
        }
    }

    // Descriptor pool: 2 sampler + 3 storage = 3 set
    {
        VkDescriptorPoolSize sizes[2] = {};
        sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sizes[0].descriptorCount = 2;  // chroma src + mixer final
        sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        sizes[1].descriptorCount = 3;  // chroma out + color in + color out

        VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        info.maxSets       = 3;
        info.poolSizeCount = 2;
        info.pPoolSizes    = sizes;
        if (vkCreateDescriptorPool(m_ctx->device, &info, nullptr, &slot.descriptorPool) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[VideoFX] descriptor pool failed");
            return false;
        }
    }

    {
        VkDescriptorSetLayout layouts[3] = {
            m_chromaLayout,
            m_colorLayout,
            mixerSamplerLayout
        };
        VkDescriptorSet sets[3] = {};
        VkDescriptorSetAllocateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        info.descriptorPool     = slot.descriptorPool;
        info.descriptorSetCount = 3;
        info.pSetLayouts        = layouts;
        if (vkAllocateDescriptorSets(m_ctx->device, &info, sets) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[VideoFX] descriptor set alloc failed");
            return false;
        }
        slot.chromaDescriptorSet      = sets[0];
        slot.colorDescriptorSet       = sets[1];
        slot.finalMixerDescriptorSet  = sets[2];
    }

    // Scrivi i descriptor set
    //  chroma:  src(sampled) -> stage1(storage)
    //  color:   stage1(storage) -> final(storage)
    //  mixer:   final(sampled)
    {
        VkDescriptorImageInfo srcImg{};
        srcImg.sampler     = srcSampler;
        srcImg.imageView   = srcView;
        srcImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo stage1{}; stage1.imageView = slot.stage1View; stage1.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo finA{};   finA.imageView   = slot.finalView;  finA.imageLayout   = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo finalSampledImg{};
        finalSampledImg.sampler     = slot.finalSampler;
        finalSampledImg.imageView   = slot.finalView;
        finalSampledImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet w[5] = {};

        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = slot.chromaDescriptorSet; w[0].dstBinding = 0; w[0].descriptorCount = 1;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[0].pImageInfo = &srcImg;

        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet = slot.chromaDescriptorSet; w[1].dstBinding = 1; w[1].descriptorCount = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[1].pImageInfo = &stage1;

        w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[2].dstSet = slot.colorDescriptorSet; w[2].dstBinding = 0; w[2].descriptorCount = 1;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[2].pImageInfo = &stage1;

        w[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[3].dstSet = slot.colorDescriptorSet; w[3].dstBinding = 1; w[3].descriptorCount = 1;
        w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[3].pImageInfo = &finA;

        w[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[4].dstSet = slot.finalMixerDescriptorSet; w[4].dstBinding = 0; w[4].descriptorCount = 1;
        w[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[4].pImageInfo = &finalSampledImg;

        vkUpdateDescriptorSets(m_ctx->device, 5, w, 0, nullptr);
    }

    if (!createSync(slot)) {
        DEJAVISUI_LOG_ERROR("[VideoFX] createSync failed for slot");
        // Opzionale: qui dovresti chiamare un metodo di cleanup se createSync fallisce
        return false;
    }

    slot.valid = true;
    DEJAVISUI_LOG_DEBUG("[VideoFX] slot ready: %ux%u (chromakey + color)",
                        width, height);
    return true;
}

void VideoFXPipeline::destroySlot(VideoFXSlotResources& slot) {
    if (!slot.valid) return;
    vkDeviceWaitIdle(m_ctx->device);

    if (slot.descriptorPool) vkDestroyDescriptorPool(m_ctx->device, slot.descriptorPool, nullptr);
    if (slot.finalSampler)   vkDestroySampler(m_ctx->device, slot.finalSampler, nullptr);
    if (slot.finalView)      vkDestroyImageView(m_ctx->device, slot.finalView, nullptr);
    if (slot.finalImage)     vkDestroyImage(m_ctx->device, slot.finalImage, nullptr);
    if (slot.finalMemory)    vkFreeMemory(m_ctx->device, slot.finalMemory, nullptr);
    if (slot.stage1View)     vkDestroyImageView(m_ctx->device, slot.stage1View, nullptr);
    if (slot.stage1Image)    vkDestroyImage(m_ctx->device, slot.stage1Image, nullptr);
    if (slot.stage1Memory)   vkFreeMemory(m_ctx->device, slot.stage1Memory, nullptr);
    slot.reset();
    DEJAVISUI_LOG_DEBUG("[Video FX] destroy slot");
}

// =========================================================================
//  recordDispatch — chromakey + color
// =========================================================================
static void imageBarrier(VkCommandBuffer cmd, VkImage img,
                         VkImageLayout oldL, VkImageLayout newL,
                         VkAccessFlags srcAcc, VkAccessFlags dstAcc,
                         VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.srcAccessMask = srcAcc;
    b.dstAccessMask = dstAcc;
    b.oldLayout     = oldL;
    b.newLayout     = newL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image         = img;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

void VideoFXPipeline::recordDispatch(VkCommandBuffer cmd, VideoFXSlotResources& slot) {
    if (!slot.valid) return;

    const uint32_t gx = (slot.width  + 15) / 16;
    const uint32_t gy = (slot.height + 15) / 16;

    // Snapshot dei parametri sotto lock — niente race con setter da altri thread.
    FxKeyerMode        mode;
    KeyerPushConstants chromaSnap;
    LumaKeyParams      lumaSnap;
    ColorParams        colorSnap;
    {
        std::lock_guard<std::mutex> lk(slot.paramsMutex);
        mode       = slot.keyerMode.load(std::memory_order_acquire);
        chromaSnap = slot.chromaParams;
        lumaSnap   = slot.lumaParams;
        colorSnap  = slot.colorParams;
    }

    // Step 0: stage1 + final in GENERAL
    imageBarrier(cmd, slot.stage1Image, slot.stage1Layout, VK_IMAGE_LAYOUT_GENERAL,
                 0, VK_ACCESS_SHADER_WRITE_BIT,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    slot.stage1Layout = VK_IMAGE_LAYOUT_GENERAL;

    imageBarrier(cmd, slot.finalImage, slot.finalLayout, VK_IMAGE_LAYOUT_GENERAL,
                 0, VK_ACCESS_SHADER_WRITE_BIT,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    slot.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

    // === Pass 1: Keyer (chroma / luma / none) ===
    // I tre path usano lo stesso descriptor set (stesso descriptor set layout:
    // sampler+storage), ma OGNUNO bind-a il proprio pipeline layout perche' i
    // push constant range sono di dimensioni diverse (chroma=32, luma=20).
    switch (mode) {
        case FxKeyerMode::Chroma: {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_chromaPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_chromaPipelineLayout, 0, 1,
                                    &slot.chromaDescriptorSet, 0, nullptr);
            vkCmdPushConstants(cmd, m_chromaPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(KeyerPushConstants), &chromaSnap);
            /* DEJAVISUI_LOG_DEBUG("[FX] dispatch CHROMA slot=%p key=(%.2f,%.2f,%.2f) thr=%.3f soft=%.3f enabled=%.1f",
                (void*)&slot,
                chromaSnap.v0, chromaSnap.v1, chromaSnap.v2,
                chromaSnap.threshold, chromaSnap.softness, chromaSnap.enabled);
            */
            break;
        }

        case FxKeyerMode::Luma: {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_lumaPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_lumaPipelineLayout, 0, 1,
                                    &slot.chromaDescriptorSet, 0, nullptr);
            // Pack lineare di LumaKeyParams in LumaPushConstants (20 byte).
            LumaPushConstants pc;
            pc.lower    = lumaSnap.lower;
            pc.upper    = lumaSnap.upper;
            pc.invert   = lumaSnap.invert;
            pc.softness = lumaSnap.softness;
            pc.enabled  = lumaSnap.enabled;
            vkCmdPushConstants(cmd, m_lumaPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(pc), &pc);
            /* DEJAVISUI_LOG_DEBUG("[FX] dispatch LUMA   slot=%p lower=%.3f upper=%.3f invert=%.1f soft=%.3f enabled=%.1f",
                (void*)&slot,
                pc.lower, pc.upper, pc.invert, pc.softness, pc.enabled);
            */
            break;
        }

        case FxKeyerMode::None:
        default: {

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_chromaPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_chromaPipelineLayout, 0, 1,
                                    &slot.chromaDescriptorSet, 0, nullptr);
            KeyerPushConstants bypass{};
            bypass.enabled = 0.0f;
            vkCmdPushConstants(cmd, m_chromaPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(bypass), &bypass);
            /*
            DEJAVISUI_LOG_DEBUG("[FX] dispatch NONE slot=%p (passthrough)", (void*)&slot);
            */
            break;
        }
    }
    vkCmdDispatch(cmd, gx, gy, 1);

    // Barrier: stage1 write -> read
    imageBarrier(cmd, slot.stage1Image,
                 VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                 VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // === Pass 2: Color ===
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_colorPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_colorPipelineLayout, 0, 1, &slot.colorDescriptorSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_colorPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(ColorParams), &colorSnap);
    vkCmdDispatch(cmd, gx, gy, 1);

    // Barrier finale: final GENERAL -> SHADER_READ_ONLY_OPTIMAL
    imageBarrier(cmd, slot.finalImage,
             VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
             VK_ACCESS_SHADER_WRITE_BIT, 0,
             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    slot.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void VideoFXPipeline::submitAsync(VideoFXSlotResources& slot,
                                  VkImageView srcView, VkSampler srcSampler,
                                  VkSemaphore waitSemaphore)
{
    if (!slot.valid) return;
    if (srcView == VK_NULL_HANDLE || srcSampler == VK_NULL_HANDLE) return;

    vkWaitForFences(m_ctx->device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
    vkResetFences  (m_ctx->device, 1, &slot.fence);

    //updateDescriptorsInternal(slot, srcView, srcSampler);

    VkCommandBuffer cmd = slot.commandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    // recordDispatch fa TUTTE le transizioni necessarie:
    //   stage1 (tracked → GENERAL), final (tracked → GENERAL), dispatch,
    //   final (GENERAL → SHADER_READ_ONLY_OPTIMAL).
    // Usa solo stage COMPUTE_SHADER_BIT / BOTTOM_OF_PIPE_BIT, validi su compute queue.
    //recordDispatch(cmd, slot);

    vkEndCommandBuffer(cmd);

    // === Submit (invariato) ===
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;

    VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (waitSemaphore != VK_NULL_HANDLE) {
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores    = &waitSemaphore;
        si.pWaitDstStageMask  = &waitStageMask;
    }

    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &slot.signalSemaphore;

    {
        std::lock_guard<std::mutex> qlock(m_ctx->computeQueueMutexRef());
        if (vkQueueSubmit(m_ctx->computeQueue, 1, &si, slot.fence) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("Submit FX fallito!");
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lk(slot.pendingSemMutex);
        slot.pendingSemaphores.push_back(slot.signalSemaphore);
    }
}

bool VideoFXPipeline::createSync(VideoFXSlotResources& slot) {
    // 1. Alloca il Command Buffer dal pool della pipeline
    VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.commandPool = m_computeCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(m_ctx->device, &allocInfo, &slot.commandBuffer) != VK_SUCCESS) {
        return false;
    }

    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkResult res = vkCreateFence(m_ctx->device, &fenceInfo, nullptr, &slot.fence);
    if (res != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[VideoFX] vkCreateFence failed: %d", res);
        return false;
    }

    VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    res = vkCreateSemaphore(m_ctx->device, &semInfo, nullptr, &slot.signalSemaphore);
    if (res != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[VideoFX] vkCreateSemaphore failed: %d", res);
        // Pulizia se il semaforo fallisce
        vkDestroyFence(m_ctx->device, slot.fence, nullptr);
        slot.fence = VK_NULL_HANDLE;
        return false;
    }


    return true;
}

void VideoFXPipeline::drainPendingSemaphores(VideoFXSlotResources& slot, std::vector<VkSemaphore>& out) {
    std::lock_guard<std::mutex> lk(slot.pendingSemMutex);
    for (auto s : slot.pendingSemaphores) {
        out.push_back(s);
    }
    slot.pendingSemaphores.clear();
}

// videofx_pipeline.cpp


void VideoFXPipeline::updateDescriptors(VideoFXSlotResources& slot, VkImageView srcView, VkSampler srcSampler) {
    if (!slot.valid) return;
    assert(srcView != VK_NULL_HANDLE && srcSampler != VK_NULL_HANDLE);

    VkDescriptorImageInfo srcImg{};
    srcImg.sampler     = srcSampler;
    srcImg.imageView   = srcView;
    srcImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = slot.chromaDescriptorSet;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo      = &srcImg;

    vkUpdateDescriptorSets(m_ctx->device, 1, &w, 0, nullptr);
}

void VideoFXPipeline::updateDescriptorsInternal(VideoFXSlotResources& slot,
                                   VkImageView srcView, VkSampler srcSampler) {

    if (!slot.valid) return;
    assert(srcView != VK_NULL_HANDLE && srcSampler != VK_NULL_HANDLE);

    VkDescriptorImageInfo srcImg{};
    srcImg.sampler     = srcSampler;
    srcImg.imageView   = srcView;
    srcImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = slot.chromaDescriptorSet;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo      = &srcImg;

    vkUpdateDescriptorSets(m_ctx->device, 1, &w, 0, nullptr);
}
