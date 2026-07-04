#include "YUV2RGBPipeline.h"
#include "YUVConverterCommon.h"

// I converter built-in. Se ne aggiungi uno nuovo, includilo qui e registralo
// in YUV2RGBPipeline::init() qui sotto.
#include <Processing.NDI.deprecated.h>
#include <libavutil/pixdesc.h>

#include "backend/src/video/vulkan_utils.h"
#include "converters/YUV420PConverter.h"
#include "converters/NV12Converter.h"
#include "converters/UYVYConverter.h"
#include "converters/P010Converter.h"

// =============================================================================
//  Lifecycle
// =============================================================================
bool YUV2RGBPipeline::init(VulkanContext* ctx) {
    if (m_initialized) return true;
    m_ctx = ctx;

    if (m_ctx->computeQueue) {
        m_computeQueue = m_ctx->computeQueue;
        m_computeQueueFamily = m_ctx->computeQueueFamily;
    } else {
        m_computeQueue = m_ctx->graphicsQueue;
        m_computeQueueFamily = m_ctx->graphicsQueueFamily;
    }

    {
        std::lock_guard<std::mutex> lk(m_registryMutex);
        if (m_converters.empty()) {
            m_converters.push_back(std::make_unique<YUV420PConverter>());
            m_converters.push_back(std::make_unique<NV12Converter>());
            m_converters.push_back(std::make_unique<UYVYConverter>());
            m_converters.push_back(std::make_unique<P010Converter>());

        }
    }

    for (auto& c : m_converters) {
        if (!c->init(m_ctx)) {
            DEJAVISUI_LOG_ERROR("[YUV2RGB] init converter '%s' fallita", c->name());
            return false;
        }
    }

    // Pool centralizzato per i descriptor set "mixer sampler" degli slot.
    // Dimensiona per ~16 slot concorrenti; aumenta se servisse.
    VkDescriptorPoolSize ps{};
    ps.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = 32;
    VkDescriptorPoolCreateInfo pi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pi.maxSets       = 32;
    pi.poolSizeCount = 1;
    pi.pPoolSizes    = &ps;
    pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    if (vkCreateDescriptorPool(m_ctx->device, &pi, nullptr, &m_mixerPool) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] vkCreateDescriptorPool mixer fallito");
        return false;
    }


    m_initialized = true;
    DEJAVISUI_LOG_INFO("[YUV2RGB] Pipeline manager initialized (%zu converters)",
                       m_converters.size());
    return true;
}

void YUV2RGBPipeline::shutdown() {
    if (!m_initialized) return;
    vkDeviceWaitIdle(m_ctx->device);
    for (auto& c : m_converters) c->shutdown();
    m_converters.clear();
    if (m_mixerPool) {
        vkDestroyDescriptorPool(m_ctx->device, m_mixerPool, nullptr);
        m_mixerPool = VK_NULL_HANDLE;
    }
    m_initialized = false;
}

void YUV2RGBPipeline::registerConverter(std::unique_ptr<IYUVConverter> c) {
    std::lock_guard<std::mutex> lk(m_registryMutex);
    if (m_initialized && !c->init(m_ctx)) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] init converter dinamico '%s' fallita", c->name());
        return;
    }
    m_converters.push_back(std::move(c));
}

IYUVConverter* YUV2RGBPipeline::findConverter(int avPixFmt) const {
    for (auto& c : m_converters) {
        if (c->supports(avPixFmt)) return c.get();
    }
    return nullptr;
}

bool YUV2RGBPipeline::createOutputTexture(YUV2RGBSlotResources& slot,
                                         uint32_t w, uint32_t h) {
    auto& tex = slot.rgbaTexture.VkTexture;

    VkImageCreateInfo ii{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ii.imageType   = VK_IMAGE_TYPE_2D;
    ii.format      = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent      = { w, h, 1 };
    ii.mipLevels   = 1;
    ii.arrayLayers = 1;
    ii.samples     = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling      = VK_IMAGE_TILING_OPTIMAL;
    ii.usage       = VK_IMAGE_USAGE_STORAGE_BIT
                   | VK_IMAGE_USAGE_SAMPLED_BIT
                   | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_ctx->device, &ii, nullptr, &tex.image) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(m_ctx->device, tex.image, &req);

    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = FindMemoryType(m_ctx, req.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX) return false;
    if (vkAllocateMemory(m_ctx->device, &ai, nullptr, &tex.memory) != VK_SUCCESS) {
        return false;
    }
    vkBindImageMemory(m_ctx->device, tex.image, tex.memory, 0);

    VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image            = tex.image;
    vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vi.format           = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(m_ctx->device, &vi, nullptr, &tex.view) != VK_SUCCESS) {
        return false;
    }

    VkSamplerCreateInfo si{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    si.magFilter    = VK_FILTER_LINEAR;
    si.minFilter    = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(m_ctx->device, &si, nullptr, &tex.sampler) != VK_SUCCESS) {
        return false;
    }

    tex.width         = w;
    tex.height        = h;
    tex.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    slot.rgbaLayout   = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void YUV2RGBPipeline::destroyOutputTexture(YUV2RGBSlotResources& slot) {
    auto& tex = slot.rgbaTexture.VkTexture;
    if (!tex.image) return;
    vkDeviceWaitIdle(m_ctx->device);
    if (tex.sampler) vkDestroySampler(m_ctx->device, tex.sampler, nullptr);
    if (tex.view)    vkDestroyImageView(m_ctx->device, tex.view, nullptr);
    if (tex.image)   vkDestroyImage(m_ctx->device, tex.image, nullptr);
    if (tex.memory)  vkFreeMemory(m_ctx->device, tex.memory, nullptr);
    tex.sampler = VK_NULL_HANDLE;
    tex.view    = VK_NULL_HANDLE;
    tex.image   = VK_NULL_HANDLE;
    tex.memory  = VK_NULL_HANDLE;
    tex.width   = 0;
    tex.height  = 0;
    tex.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    slot.rgbaLayout   = VK_IMAGE_LAYOUT_UNDEFINED;
}

bool YUV2RGBPipeline::createMixerDescriptorSet(YUV2RGBSlotResources& slot) {
    if (m_mixerLayout == VK_NULL_HANDLE) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] mixerDescriptorLayout non settato");
        return false;
    }
    VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ai.descriptorPool     = m_mixerPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &m_mixerLayout;
    if (vkAllocateDescriptorSets(m_ctx->device, &ai, &slot.mixerDescriptorSet) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorImageInfo ii{};
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ii.imageView   = slot.rgbaTexture.VkTexture.view;
    ii.sampler     = slot.rgbaTexture.VkTexture.sampler;

    VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet          = slot.mixerDescriptorSet;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo      = &ii;
    vkUpdateDescriptorSets(m_ctx->device, 1, &w, 0, nullptr);
    return true;
}

bool YUV2RGBPipeline::createSlot(YUV2RGBSlotResources& slot,
                                 int avPixFmt, uint32_t w, uint32_t h,bool asyncEnabled) {
    if (!m_initialized) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] createSlot before init");
        return false;
    }
    IYUVConverter* conv = findConverter(avPixFmt);
    if (!conv) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] nessun converter per fmt %d", avPixFmt);
        return false;
    }

    if (!createOutputTexture(slot, w, h)) return false;
    if (!createMixerDescriptorSet(slot)) {
        destroyOutputTexture(slot);
        return false;
    }

    IYUVConverter::OutputBinding ob{
        slot.rgbaTexture.VkTexture.image,
        slot.rgbaTexture.VkTexture.view,
        w, h
    };
    slot.converterSlot = conv->createSlot(w, h, ob);
    if (!slot.converterSlot) {
        if (slot.mixerDescriptorSet) {
            vkFreeDescriptorSets(m_ctx->device, m_mixerPool, 1, &slot.mixerDescriptorSet);
            slot.mixerDescriptorSet = VK_NULL_HANDLE;
        }
        destroyOutputTexture(slot);
        return false;
    }
    slot.converter = conv;
    slot.avPixFmt  = avPixFmt;
    slot.width     = w;
    slot.height    = h;
    slot.valid     = true;
    if (asyncEnabled) {
        if (!createSyncResources(slot)) {
            DEJAVISUI_LOG_ERROR("[YUV2RGB] createSyncResources fallito");
            destroySlot(slot);
            return false;
        }
    }
    DEJAVISUI_LOG_INFO("[YUV2RGB] slot creato: %s %ux%u", conv->name(), w, h);
    return true;
}

void YUV2RGBPipeline::destroySlot(YUV2RGBSlotResources& slot) {
    if (!slot.valid && !slot.converterSlot && !slot.rgbaTexture.VkTexture.image) return;
    vkDeviceWaitIdle(m_ctx->device);

    destroySyncResources(slot);

    if (slot.converter && slot.converterSlot) {
        slot.converter->destroySlot(slot.converterSlot.get());
    }
    if (slot.inflightVkFrame) av_frame_free(&slot.inflightVkFrame);
    slot.converterSlot.reset();
    slot.converter = nullptr;

    if (slot.mixerDescriptorSet) {
        vkFreeDescriptorSets(m_ctx->device, m_mixerPool, 1, &slot.mixerDescriptorSet);
        slot.mixerDescriptorSet = VK_NULL_HANDLE;
    }
    destroyOutputTexture(slot);

    slot.valid    = false;
    slot.avPixFmt = -1;
    slot.width    = 0;
    slot.height   = 0;
}

bool YUV2RGBPipeline::resizeSlot(YUV2RGBSlotResources& slot,
                                 int avPixFmt, uint32_t w, uint32_t h) {
    if (!m_initialized) return false;
    if (!slot.valid) return createSlot(slot, avPixFmt, w, h);

    // No-op se nulla e' cambiato
    if (slot.avPixFmt == avPixFmt && slot.width == w && slot.height == h) return true;

    const bool wasAsyncEnabled = slot.asyncEnabled;
    destroySlot(slot);
    return createSlot(slot, avPixFmt, w, h, wasAsyncEnabled);
}

// =============================================================================
//  Delega
// =============================================================================
bool YUV2RGBPipeline::uploadFrame(YUV2RGBSlotResources& slot, AVFrame* f) {
    if (!slot.valid || !slot.converter) return false;
    int cs = (int)f->colorspace;
    if (cs == AVCOL_SPC_UNSPECIFIED || cs == AVCOL_SPC_RESERVED) {
        if (f->width >= 3840)       cs = AVCOL_SPC_BT2020_NCL;   // 4K -> BT2020
        else if (f->width >= 1280)  cs = AVCOL_SPC_BT709;        // HD  -> BT709
        else                        cs = AVCOL_SPC_BT470BG;      // SD  -> BT601
    }
    slot.colorSpace = cs;
    slot.pts = (double)f->pts;
    return slot.converter->uploadFrame(slot.converterSlot.get(), f);
}

bool YUV2RGBPipeline::uploadNDIFrame(YUV2RGBSlotResources& slot, const NDIlib_video_frame_v2_t& v) {
    if (!slot.valid || !slot.converter) return false;
    return slot.converter->uploadNDIFrame(slot.converterSlot.get(), v);
}


void YUV2RGBPipeline::recordDispatch(VkCommandBuffer cmd, YUV2RGBSlotResources& slot) {
    if (!slot.valid || !slot.converter) return;
    IYUVConverter::FrameMetadata meta{ slot.colorSpace, slot.rangeFull };
    slot.converter->recordDispatch(cmd, slot.converterSlot.get(), meta, slot.rgbaLayout);
    slot.rgbaTexture.VkTexture.currentLayout = slot.rgbaLayout;
}

bool YUV2RGBPipeline::createSyncResources(YUV2RGBSlotResources& slot) {
    VkCommandPoolCreateInfo pi{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pi.queueFamilyIndex = m_computeQueueFamily;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(m_ctx->device, &pi, nullptr, &slot.cmdPool) != VK_SUCCESS)
        return false;

    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool        = slot.cmdPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_ctx->device, &ai, &slot.cmd) != VK_SUCCESS)
        return false;

    VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // primo wait deve passare
    if (vkCreateFence(m_ctx->device, &fi, nullptr, &slot.fence) != VK_SUCCESS)
        return false;

    VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (int i = 0; i < YUV2RGBSlotResources::kSemRing; ++i) {
        if (vkCreateSemaphore(m_ctx->device, &si, nullptr, &slot.semRing[i]) != VK_SUCCESS)
            return false;
    }
    slot.semWriteIdx     = 0;
    slot.computeFinished = slot.semRing[0];
    slot.asyncEnabled    = true;
    return true;
}

void YUV2RGBPipeline::destroySyncResources(YUV2RGBSlotResources& slot) {
    if (!slot.asyncEnabled) return;
    vkDeviceWaitIdle(m_ctx->device);
    for (auto& s : slot.semRing) {
        if (s) { vkDestroySemaphore(m_ctx->device, s, nullptr); s = VK_NULL_HANDLE; }
    }
    if (slot.fence)   { vkDestroyFence(m_ctx->device, slot.fence, nullptr);       slot.fence   = VK_NULL_HANDLE; }
    if (slot.cmdPool) { vkDestroyCommandPool(m_ctx->device, slot.cmdPool, nullptr); slot.cmdPool = VK_NULL_HANDLE; }
    slot.cmd             = VK_NULL_HANDLE;
    slot.computeFinished = VK_NULL_HANDLE;
    slot.semWriteIdx     = 0;
    slot.asyncEnabled    = false;
    {
        std::lock_guard<std::mutex> lk(slot.pendingSemMutex);
        slot.pendingSemaphores.clear();
    }
}

bool YUV2RGBPipeline::submitAsync(YUV2RGBSlotResources& slot) {
    if (!slot.valid || !slot.asyncEnabled) return false;

    std::lock_guard<std::mutex> lk(slot.submitMutex);

    vkWaitForFences(m_ctx->device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_ctx->device, 1, &slot.fence);
    vkResetCommandBuffer(slot.cmd, 0);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(slot.cmd, &bi);


    recordDispatch(slot.cmd, slot);

    vkEndCommandBuffer(slot.cmd);

    VkSemaphore signalSem = slot.semRing[slot.semWriteIdx];
    slot.semWriteIdx = (slot.semWriteIdx + 1) % YUV2RGBSlotResources::kSemRing;
    slot.computeFinished = slot.semRing[slot.semWriteIdx];

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &slot.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &signalSem;
    VkResult r = VulkanQueueSubmit(m_computeQueue, 1, &si, slot.fence,"YUV2RGBPipeline::submitAsync");
    if (r != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB] vkQueueSubmit async fallito: %d", r);
        return false;
    }

    {
        std::lock_guard<std::mutex> sl(slot.pendingSemMutex);
        slot.pendingSemaphores.push_back(signalSem);
    }
    return true;
}

void YUV2RGBPipeline::drainPendingSemaphores(YUV2RGBSlotResources& slot,
                                             std::vector<VkSemaphore>& out) {
    if (!slot.asyncEnabled) return;
    std::lock_guard<std::mutex> lk(slot.pendingSemMutex);
    out.insert(out.end(), slot.pendingSemaphores.begin(), slot.pendingSemaphores.end());
    slot.pendingSemaphores.clear();
}

bool YUV2RGBPipeline::uploadVulkanFrame(YUV2RGBSlotResources& slot, AVFrame* frame) {
    if (!slot.valid || !frame) return false;
    if (frame->format != AV_PIX_FMT_VULKAN) return false;

    // Nella nuova architettura (IYUVConverter), tutta la logica di sincronizzazione
    // hardware (semafori e timeline) e copia è gestita da submitAsyncVk.
    // Qui ci limitiamo a salvare una reference al frame Vulkan per il prossimo submit.

    if (!slot.inflightVkFrame) {
        slot.inflightVkFrame = av_frame_alloc();
    } else {
        av_frame_unref(slot.inflightVkFrame);
    }

    // Incrementiamo il reference counter del frame per mantenerlo in vita finché non viene processato
    if (av_frame_ref(slot.inflightVkFrame, frame) < 0) {
        return false;
    }

    slot.vulkanFed = true;
    return true;
}

bool YUV2RGBPipeline::submitAsyncVk(YUV2RGBSlotResources& slot) {
    std::lock_guard<std::mutex> g(slot.submitMutex);

    AVFrame* f = slot.inflightVkFrame;
    if (!f || !slot.converter || !slot.converterSlot) return false;
    if (!f->data[0] || !f->hw_frames_ctx) return false;

    auto* vkf  = reinterpret_cast<AVVkFrame*>(f->data[0]);
    auto* fctx = reinterpret_cast<AVHWFramesContext*>(f->hw_frames_ctx->data);
    auto* vkfc = reinterpret_cast<AVVulkanFramesContext*>(fctx->hwctx);
    const int P = 0;

    if (vkfc->lock_frame) vkfc->lock_frame(fctx, vkf);

    const VkImage       img       = vkf->img[P];
    const VkImageLayout srcLayout = vkf->layout[P];
    const VkAccessFlags srcAccess = (VkAccessFlags)vkf->access[P];
    const VkSemaphore   sem       = vkf->sem[P];
    const uint64_t      waitVal   = vkf->sem_value[P];
    const uint64_t      signalVal = waitVal + 1;

    // 1) Registra: copia image->buffer (converter) + dispatch YUV->RGB.
    vkResetCommandBuffer(slot.cmd, 0);
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(slot.cmd, &bi);

    bool copied = slot.converter->recordUploadFromVulkanImage(
        slot.cmd, slot.converterSlot.get(),
        img, srcLayout, srcAccess, slot.width, slot.height);

    if (copied) {
        recordDispatch(slot.cmd, slot);
    }
    vkEndCommandBuffer(slot.cmd);

    if (!copied) {
        if (vkfc->unlock_frame) vkfc->unlock_frame(fctx, vkf);
        return false;
    }

    VkSemaphore binSignal = slot.semRing[slot.semWriteIdx];
    slot.semWriteIdx = (slot.semWriteIdx + 1) % YUV2RGBSlotResources::kSemRing;

    VkSemaphore          waitSems[1]  = { sem };
    uint64_t             waitVals[1]  = { waitVal };
    VkPipelineStageFlags waitStage[1] = { VK_PIPELINE_STAGE_TRANSFER_BIT }; // la copia legge l'immagine

    VkSemaphore signalSems[2] = { sem,        binSignal };
    uint64_t    signalVals[2] = { signalVal,  0 };

    const bool haveTimeline = (sem != VK_NULL_HANDLE);

    VkTimelineSemaphoreSubmitInfo tl{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    tl.waitSemaphoreValueCount   = haveTimeline ? 1 : 0;
    tl.pWaitSemaphoreValues      = haveTimeline ? waitVals : nullptr;
    tl.signalSemaphoreValueCount = haveTimeline ? 2 : 1;
    tl.pSignalSemaphoreValues    = haveTimeline ? signalVals : &signalVals[1];

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.pNext                = &tl;
    si.waitSemaphoreCount   = haveTimeline ? 1 : 0;
    si.pWaitSemaphores      = haveTimeline ? waitSems : nullptr;
    si.pWaitDstStageMask    = haveTimeline ? waitStage : nullptr;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &slot.cmd;
    si.signalSemaphoreCount = haveTimeline ? 2 : 1;
    si.pSignalSemaphores    = haveTimeline ? signalSems : &binSignal;

    vkResetFences(m_ctx->device, 1, &slot.fence);

    VkResult sr;
    {
        std::lock_guard<std::mutex> qlk(m_ctx->computeQueueMutexRef());
        sr = vkQueueSubmit(m_computeQueue, 1, &si, slot.fence);
    }

    if (sr == VK_SUCCESS) {
        vkf->layout[P]    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vkf->access[P]    = VK_ACCESS_TRANSFER_READ_BIT;
        vkf->sem_value[P] = signalVal;
    }
    if (vkfc->unlock_frame) vkfc->unlock_frame(fctx, vkf);

    if (sr != VK_SUCCESS) return false;

    {
        std::lock_guard<std::mutex> lk(slot.pendingSemMutex);
        slot.pendingSemaphores.push_back(binSignal);
        slot.computeFinished = binSignal;
    }
    return true;
}