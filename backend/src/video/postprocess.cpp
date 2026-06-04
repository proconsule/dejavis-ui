#include "postprocess.h"
#include "../logger.h"
#include <cstring>

// =============================================================================
//  Costruttore / Distruttore
// =============================================================================
CPostProcessor::CPostProcessor(VulkanContext* ctx)
    : m_ctx(ctx)
    , m_mixerLayout(VK_NULL_HANDLE)
{
    // Il mixer descriptor layout vive nel contesto Vulkan dell'app.
    if (m_ctx) m_mixerLayout = m_ctx->m_mixerDescriptorLayout;
}

CPostProcessor::~CPostProcessor() {
    cleanup();
}

// =============================================================================
//  Init: scelta della modalità input
// =============================================================================
bool CPostProcessor::initYuvInput() {
    // Niente da fare adesso: il primo uploadYuvFrame creerà lo slot con le
    // dimensioni giuste. È un init "lazy".
    if (!m_ctx || m_mixerLayout == VK_NULL_HANDLE) {
        DEJAVISUI_LOG_ERROR("[PostProc] initYuvInput: ctx/mixerLayout mancanti");
        return false;
    }
    return true;
}

bool CPostProcessor::initRgbaInput(const VulkanUniTexture& externalTex,
                                   uint32_t w, uint32_t h)
{
    auto fresh = buildSlot(w, h, ProcessingSlot::InputKind::ExternalRgba, &externalTex);
    if (!fresh) return false;

    ProcessingSlot* old = m_activeSlot.exchange(fresh.release(), std::memory_order_acq_rel);
    if (old) retireSlot(old);
    return true;
}

// =============================================================================
//  Upload: produttore (receiver / loader thread)
// =============================================================================
bool CPostProcessor::uploadYuvFrame(AVFrame* frame) {
    if (m_shuttingDown.load(std::memory_order_acquire) || !frame) return false;
    const uint32_t w = (uint32_t)frame->width;
    const uint32_t h = (uint32_t)frame->height;
    if (w == 0 || h == 0) return false;

    ProcessingSlot* current = m_activeSlot.load(std::memory_order_acquire);
    const bool needsNew =
        !current
        || current->kind != ProcessingSlot::InputKind::Yuv
        || current->width    != w
        || current->height   != h
        || current->yuvPixFmt != frame->format;   // ← nuovo

    if (needsNew) {
        auto fresh = buildSlot(w, h,
                               ProcessingSlot::InputKind::Yuv,
                               nullptr,
                               frame->format);
        if (!fresh) return false;
        ProcessingSlot* old = m_activeSlot.exchange(fresh.release(),
                                                    std::memory_order_acq_rel);
        if (old) retireSlot(old);
        current = m_activeSlot.load(std::memory_order_acquire);
    }

    return YUV2RGBPipeline::instance().uploadFrame(current->yuv, frame);
}

bool CPostProcessor::uploadYuvFrame(const NDIlib_video_frame_v2_t& v) {
    if (m_shuttingDown.load(std::memory_order_acquire)) return false;
    if (!v.p_data || v.xres <= 0 || v.yres <= 0) return false;

    const uint32_t w = (uint32_t)v.xres;
    const uint32_t h = (uint32_t)v.yres;

    ProcessingSlot* current = m_activeSlot.load(std::memory_order_acquire);
    const bool needsNew =
        !current
        || current->kind != ProcessingSlot::InputKind::Yuv
        || current->width  != w
        || current->height != h;

    if (needsNew) {
        auto fresh = buildSlot(w, h, ProcessingSlot::InputKind::Yuv, nullptr, AV_PIX_FMT_UYVY422);
        if (!fresh) return false;

        ProcessingSlot* old = m_activeSlot.exchange(fresh.release(), std::memory_order_acq_rel);
        if (old) retireSlot(old);
        current = m_activeSlot.load(std::memory_order_acquire);
    }

    return YUV2RGBPipeline::instance().uploadNDIFrame(current->yuv, v);
}

bool CPostProcessor::uploadYuvFrameVulkan(AVFrame* frame) {
    if (m_shuttingDown.load(std::memory_order_acquire) || !frame) return false;
    if (frame->format != AV_PIX_FMT_VULKAN || !frame->hw_frames_ctx) return false;

    const uint32_t w = (uint32_t)frame->width;
    const uint32_t h = (uint32_t)frame->height;
    if (w == 0 || h == 0) return false;

    auto* fctx = reinterpret_cast<AVHWFramesContext*>(frame->hw_frames_ctx->data);
    const int swFmt = (int)fctx->sw_format;

    ProcessingSlot* current = m_activeSlot.load(std::memory_order_acquire);

    const bool needsNew =
        !current
        || current->kind != ProcessingSlot::InputKind::Yuv
        || current->width  != w
        || current->height != h
        || current->yuvPixFmt != swFmt;

    if (needsNew) {
        auto fresh = buildSlot(w, h, ProcessingSlot::InputKind::Yuv, nullptr, swFmt);
        if (!fresh) return false;
        ProcessingSlot* old = m_activeSlot.exchange(fresh.release(),
                                                    std::memory_order_acq_rel);
        if (old) retireSlot(old);
        current = m_activeSlot.load(std::memory_order_acquire);
    }

    return YUV2RGBPipeline::instance().uploadVulkanFrame(current->yuv, frame);
}

bool CPostProcessor::notifyRgbaInputChanged(const VulkanUniTexture& newTex,
                                            uint32_t w, uint32_t h)
{
    if (m_shuttingDown.load(std::memory_order_acquire)) return false;

    auto fresh = buildSlot(w, h, ProcessingSlot::InputKind::ExternalRgba, &newTex);
    if (!fresh) return false;

    ProcessingSlot* old = m_activeSlot.exchange(fresh.release(), std::memory_order_acq_rel);
    if (old) retireSlot(old);
    return true;
}

bool CPostProcessor::submit(VkSemaphore externalWait) {
    if (m_shuttingDown.load(std::memory_order_acquire)) return false;

    ProcessingSlot* slot = m_activeSlot.load(std::memory_order_acquire);
    if (!slot || !slot->valid) return false;

    const bool fxOn = m_enabled.load(std::memory_order_acquire);

    if (fxOn) {
        std::lock_guard<std::mutex> g1(m_paramsMutex);
        std::lock_guard<std::mutex> g2(slot->fx.paramsMutex);
        slot->fx.keyerMode.store(m_keyerMode, std::memory_order_release);
        slot->fx.chromaParams = m_chromaParams;
        slot->fx.lumaParams   = m_lumaParams;
        slot->fx.colorParams  = m_colorParams;
    }

    if (slot->kind == ProcessingSlot::InputKind::Yuv) {
        if (slot->yuv.vulkanFed)
            YUV2RGBPipeline::instance().submitAsyncVk(slot->yuv);
        else
            YUV2RGBPipeline::instance().submitAsync(slot->yuv);

    if (fxOn) {
            std::vector<VkSemaphore> yuvSems;
            YUV2RGBPipeline::drainPendingSemaphores(slot->yuv, yuvSems);
            VkSemaphore yuvSignal = yuvSems.empty() ? VK_NULL_HANDLE : yuvSems.back();
            VideoFXPipeline::instance().submitAsync(
            slot->fx, slot->yuv.rgbaTexture.VkTexture.view,
            slot->yuv.rgbaTexture.VkTexture.sampler, yuvSignal);
        }
    } else {
        if (fxOn) {
            VideoFXPipeline::instance().submitAsync(
                slot->fx,
                slot->externalView,
                slot->externalSampler,
                externalWait
            );
        }
    }

    return true;
}

// =============================================================================
//  Output / Semafori
// =============================================================================
VkDescriptorSet CPostProcessor::getOutputDescriptorSet() const {
    ProcessingSlot* slot = m_activeSlot.load(std::memory_order_acquire);


    if (!slot || !slot->valid) return VK_NULL_HANDLE;

    const bool fxOn = m_enabled.load(std::memory_order_acquire);

    if (fxOn && slot->fx.valid) return slot->fx.finalMixerDescriptorSet;
    return slot->bypassMixerDescriptor;
}

void CPostProcessor::getWaitSemaphores(std::vector<VkSemaphore>& out) {
    ProcessingSlot* slot = m_activeSlot.load(std::memory_order_acquire);
    if (!slot || !slot->valid) return;

    const bool fxOn = m_enabled.load(std::memory_order_acquire);

    if (fxOn && slot->fx.valid) {
        VideoFXPipeline::drainPendingSemaphores(slot->fx, out);
    }
    else if (slot->kind == ProcessingSlot::InputKind::Yuv) {
        YUV2RGBPipeline::drainPendingSemaphores(slot->yuv, out);
    }
}

void CPostProcessor::processLifecycle(uint64_t currentFrameIdx) {
    m_currentFrame.store(currentFrameIdx, std::memory_order_release);

    std::lock_guard<std::mutex> lk(m_retiredMutex);
    auto it = m_retiredSlots.begin();
    while (it != m_retiredSlots.end()) {
        if (currentFrameIdx - it->retiredAtFrame >= kDestroyDelayFrames) {
            destroySlotResources(*it->slot);
            it = m_retiredSlots.erase(it);
        } else {
            ++it;
        }
    }
}

void CPostProcessor::setEnabled(bool e) {
    m_enabled.store(e, std::memory_order_release);
}

void CPostProcessor::setKeyerMode(FxKeyerMode m) {
    std::lock_guard<std::mutex> g(m_paramsMutex);
    m_keyerMode = m;
}

void CPostProcessor::setChromaParams(const KeyerPushConstants& cp) {
    std::lock_guard<std::mutex> g(m_paramsMutex);
    m_chromaParams = cp;
}

void CPostProcessor::setLumaParams(const LumaKeyParams& lp) {
    std::lock_guard<std::mutex> g(m_paramsMutex);
    m_lumaParams = lp;
}

void CPostProcessor::setColorParams(const ColorParams& cp) {
    std::lock_guard<std::mutex> g(m_paramsMutex);
    m_colorParams = cp;
}

void CPostProcessor::cleanup() {
    m_shuttingDown.store(true, std::memory_order_release);

    if (m_ctx && m_ctx->device) {
        vkDeviceWaitIdle(m_ctx->device);
    }

    ProcessingSlot* active = m_activeSlot.exchange(nullptr, std::memory_order_acq_rel);
    if (active) {
        destroySlotResources(*active);
        delete active;
    }

    std::lock_guard<std::mutex> lk(m_retiredMutex);
    for (auto& r : m_retiredSlots) {
        destroySlotResources(*r.slot);
    }
    m_retiredSlots.clear();
}

std::unique_ptr<CPostProcessor::ProcessingSlot> CPostProcessor::buildSlot(
    uint32_t w, uint32_t h,
    ProcessingSlot::InputKind kind,
    const VulkanUniTexture* externalRgba,
    int avPixFmt /* = AV_PIX_FMT_NONE */)
{
    if (!m_ctx || m_mixerLayout == VK_NULL_HANDLE) {
        DEJAVISUI_LOG_ERROR("[PostProc] buildSlot: ctx o mixerLayout mancanti");
        return nullptr;
    }
    if (w == 0 || h == 0) return nullptr;

    auto s = std::make_unique<ProcessingSlot>();
    s->width  = w;
    s->height = h;
    s->kind   = kind;

    if (kind == ProcessingSlot::InputKind::Yuv) {
        if (avPixFmt == AV_PIX_FMT_NONE) {
            DEJAVISUI_LOG_ERROR("[PostProc] buildSlot YUV senza avPixFmt valido");
            return nullptr;
        }
        constexpr bool kAsync = true;
        if (!YUV2RGBPipeline::instance().createSlot(s->yuv, avPixFmt, w, h, kAsync)) {
            DEJAVISUI_LOG_ERROR("[PostProc] YUV createSlot fallito (fmt=%d, %ux%u)",
                                avPixFmt, w, h);
            return nullptr;
        }
        s->yuvPixFmt = avPixFmt;
    }
    else {
        if (!externalRgba) {
            DEJAVISUI_LOG_ERROR("[PostProc] external RGBA: texture nulla");
            return nullptr;
        }
        s->externalView    = externalRgba->VkTexture.view;
        s->externalSampler = externalRgba->VkTexture.sampler;
        if (s->externalView == VK_NULL_HANDLE || s->externalSampler == VK_NULL_HANDLE) {
            DEJAVISUI_LOG_ERROR("[PostProc] external RGBA: view/sampler nulli");
            return nullptr;
        }
    }

    auto rollbackInput = [&]() {
        if (kind == ProcessingSlot::InputKind::Yuv) {
            YUV2RGBPipeline::instance().destroySlot(s->yuv);
        }
    };

    if (!VideoFXPipeline::instance().createSlot(
            s->fx, w, h,
            s->fxInputView(),
            s->fxInputSampler(),
            m_mixerLayout))
    {
        DEJAVISUI_LOG_ERROR("[PostProc] FX createSlot fallito (%ux%u)", w, h);
        rollbackInput();
        return nullptr;
    }

    {
        VkDescriptorPoolSize ps{};
        ps.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.maxSets       = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes    = &ps;

        if (vkCreateDescriptorPool(m_ctx->device, &poolInfo, nullptr, &s->bypassPool) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[PostProc] bypass descriptor pool fallito");
            VideoFXPipeline::instance().destroySlot(s->fx);
            rollbackInput();
            return nullptr;
        }

        VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        ai.descriptorPool     = s->bypassPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &m_mixerLayout;

        if (vkAllocateDescriptorSets(m_ctx->device, &ai, &s->bypassMixerDescriptor) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[PostProc] bypass descriptor set alloc fallita");
            vkDestroyDescriptorPool(m_ctx->device, s->bypassPool, nullptr);
            s->bypassPool = VK_NULL_HANDLE;
            VideoFXPipeline::instance().destroySlot(s->fx);
            rollbackInput();
            return nullptr;
        }

        VkDescriptorImageInfo ii{};
        ii.imageView   = s->fxInputView();
        ii.sampler     = s->fxInputSampler();
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet wr{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        wr.dstSet          = s->bypassMixerDescriptor;
        wr.dstBinding      = 0;
        wr.descriptorCount = 1;
        wr.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr.pImageInfo      = &ii;

        vkUpdateDescriptorSets(m_ctx->device, 1, &wr, 0, nullptr);
    }

    s->valid = true;
    DEJAVISUI_LOG_INFO("[PostProc] Slot %ux%u costruito (kind=%s, fmt=%d)",
        w, h,
        kind == ProcessingSlot::InputKind::Yuv ? "YUV" : "RGBA",
        avPixFmt);
    return s;
}

void CPostProcessor::retireSlot(ProcessingSlot* old) {
    if (!old) return;
    old->valid = false;   // nessuno deve più usarlo come "active"

    std::lock_guard<std::mutex> lk(m_retiredMutex);
    m_retiredSlots.push_back(RetiredSlot{
        std::unique_ptr<ProcessingSlot>(old),
        m_currentFrame.load(std::memory_order_acquire)
    });
}

void CPostProcessor::destroySlotResources(ProcessingSlot& s) {
    if (s.kind == ProcessingSlot::InputKind::Yuv) {
        YUV2RGBPipeline::instance().destroySlot(s.yuv);
    }
    VideoFXPipeline::instance().destroySlot(s.fx);

    if (s.bypassPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_ctx->device, s.bypassPool, nullptr);
        s.bypassPool            = VK_NULL_HANDLE;
        s.bypassMixerDescriptor = VK_NULL_HANDLE;  // implicitamente freed col pool
    }

    s.valid = false;
}

uint32_t CPostProcessor::getWidth() const {
    ProcessingSlot* slot = m_activeSlot.load(std::memory_order_acquire);
    return (slot && slot->valid) ? slot->width : 0;
}

uint32_t CPostProcessor::getHeight() const {
    ProcessingSlot* slot = m_activeSlot.load(std::memory_order_acquire);
    return (slot && slot->valid) ? slot->height : 0;
}

bool CPostProcessor::getDimensions(uint32_t& w, uint32_t& h) const {
    ProcessingSlot* slot = m_activeSlot.load(std::memory_order_acquire);
    if (!slot || !slot->valid) {
        w = h = 0;
        return false;
    }
    w = slot->width;
    h = slot->height;
    return true;
}