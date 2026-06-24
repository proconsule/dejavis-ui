#include "av_decoder.h"
#ifdef __WIN32__
#include <io.h>
#endif
#include <iostream>

#include "logger.h"
#include "video/vulkan_utils.h"


CAV_DECODER::CAV_DECODER() {

}

CAV_DECODER::~CAV_DECODER() {
    cleanup();
}

/*
static void ff_vk_lock_queue(AVHWDeviceContext* ctx, uint32_t qf, uint32_t)
{
    auto* vk = static_cast<VulkanContext*>(ctx->user_opaque);

    if      (qf == vk->graphicsQueueFamily) vk->graphicsQueueMutex.lock();
    else if (qf == vk->computeQueueFamily)  vk->computeQueueMutex.lock();
    else if (qf == vk->transferQueueFamily) vk->transferQueueMutex.lock();
    else if (qf == vk->decodeQueueFamily)   vk->decodeQueueMutex.lock();  // <- da aggiungere
}

static void ff_vk_unlock_queue(AVHWDeviceContext* ctx, uint32_t qf, uint32_t)
{
    auto* vk = static_cast<VulkanContext*>(ctx->user_opaque);
    if      (qf == vk->graphicsQueueFamily) vk->graphicsQueueMutex.unlock();
    else if (qf == vk->computeQueueFamily)  vk->computeQueueMutex.unlock();
    else if (qf == vk->transferQueueFamily) vk->transferQueueMutex.unlock();
    else if (qf == vk->decodeQueueFamily)   vk->decodeQueueMutex.unlock();
}
*/

static void ff_vk_lock_queue(AVHWDeviceContext* c, uint32_t qf, uint32_t){
    static_cast<VulkanContext*>(c->user_opaque)->queueMutex(qf).lock();
}

static void ff_vk_unlock_queue(AVHWDeviceContext* c, uint32_t qf, uint32_t){
    static_cast<VulkanContext*>(c->user_opaque)->queueMutex(qf).unlock();
}

static inline void nv12_plane_formats(VkFormat& y, VkFormat& uv) {
    y  = VK_FORMAT_R8_UNORM;     // PLANE_0
    uv = VK_FORMAT_R8G8_UNORM;   // PLANE_1
}

static VkImageView make_plane_view(VkDevice dev, VkImage img,
                                   VkFormat planeFmt, VkImageAspectFlagBits aspect)
{
    VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image    = img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format   = planeFmt;
    vi.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                      VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
    vi.subresourceRange.aspectMask     = aspect;   // VK_IMAGE_ASPECT_PLANE_0/1_BIT
    vi.subresourceRange.baseMipLevel   = 0;
    vi.subresourceRange.levelCount     = 1;
    vi.subresourceRange.baseArrayLayer = 0;
    vi.subresourceRange.layerCount     = 1;

    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(dev, &vi, nullptr, &view) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return view;
}



bool CAV_DECODER::InitFFmpegVulkanHW()
{
    if (!m_ctx || m_ctx->device == VK_NULL_HANDLE) {
        DEJAVISUI_LOG_ERROR("[DECODER] InitFFmpegVulkanHW: VulkanContext non valido");
        return false;
    }

    hw_device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
    if (!hw_device_ctx) {
        DEJAVISUI_LOG_ERROR("[DECODER] av_hwdevice_ctx_alloc(VULKAN) fallito");
        return false;
    }

    AVHWDeviceContext*     devCtx = reinterpret_cast<AVHWDeviceContext*>(hw_device_ctx->data);
    AVVulkanDeviceContext* vkCtx  = reinterpret_cast<AVVulkanDeviceContext*>(devCtx->hwctx);

    // Serve ai callback di lock per ritrovare i tuoi mutex.
    devCtx->user_opaque = m_ctx;

    // --- Handle base -----------------------------------------------------------
    vkCtx->get_proc_addr = vkGetInstanceProcAddr;   // se usi volk, passa il loader di volk
    vkCtx->alloc         = nullptr;
    vkCtx->inst          = m_ctx->instance;
    vkCtx->phys_dev      = m_ctx->physicalDevice;
    vkCtx->act_dev       = m_ctx->device;

    // --- Feature realmente abilitate al device-creation (con la sua pNext-chain)
    vkCtx->device_features = m_ctx->deviceFeatures2;

    // --- Estensioni ------------------------------------------------------------
    // Instance: non le memorizzi nel context. Con instance 1.3 le capability di
    // external memory sono core, quindi NULL/0 va bene. Se av_hwdevice_ctx_init
    // si lamenta di una instance extension mancante, dovrai memorizzarle e passarle.
    vkCtx->enabled_inst_extensions    = nullptr;
    vkCtx->nb_enabled_inst_extensions = 0;

    vkCtx->enabled_dev_extensions     = m_ctx->devExt.data();    // deve restare vivo
    vkCtx->nb_enabled_dev_extensions  = (int)m_ctx->devExt.size();

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(59, 34, 100)
    int n = 0;
    auto addQF = [&](uint32_t idx, int num, VkQueueFlagBits flags,
                     VkVideoCodecOperationFlagBitsKHR vcaps)
    {
        if (idx == UINT32_MAX || n >= 64) return;
        vkCtx->qf[n].idx        = (int)idx;
        vkCtx->qf[n].num        = num;          // tu crei 1 queue per family
        vkCtx->qf[n].flags      = flags;
        vkCtx->qf[n].video_caps = vcaps;
        ++n;
    };

    addQF(m_ctx->graphicsQueueFamily, 1, VK_QUEUE_GRAPHICS_BIT,
          (VkVideoCodecOperationFlagBitsKHR)0);
    addQF(m_ctx->computeQueueFamily,  1, VK_QUEUE_COMPUTE_BIT,
          (VkVideoCodecOperationFlagBitsKHR)0);
    addQF(m_ctx->transferQueueFamily, 1, VK_QUEUE_TRANSFER_BIT,
          (VkVideoCodecOperationFlagBitsKHR)0);
    addQF(m_ctx->decodeQueueFamily,   1, VK_QUEUE_VIDEO_DECODE_BIT_KHR,
          (VkVideoCodecOperationFlagBitsKHR)(
              VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR |
              VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR));
    addQF(m_ctx->encodeQueueFamily, 1, VK_QUEUE_VIDEO_ENCODE_BIT_KHR,
          (VkVideoCodecOperationFlagBitsKHR)0);

    vkCtx->nb_qf = n;

    // --- Campi deprecati: la struct e' zero-init, ma 0 e' un indice VALIDO,
    //     quindi su versioni transitorie va riempito esplicitamente per non far
    //     credere a FFmpeg che la family 0 sia tx/comp/decode.
#else
    vkCtx->queue_family_index        = (int)m_ctx->graphicsQueueFamily; vkCtx->nb_graphics_queues = 1;
    vkCtx->queue_family_tx_index     = (int)m_ctx->transferQueueFamily; vkCtx->nb_tx_queues       = 1;
    vkCtx->queue_family_comp_index   = (int)m_ctx->computeQueueFamily;  vkCtx->nb_comp_queues     = 1;
    vkCtx->queue_family_decode_index = -1;
    vkCtx->nb_decode_queues   = 0;
    if (m_ctx->decodeQueueFamily != UINT32_MAX) {
        vkCtx->queue_family_decode_index = (int)m_ctx->decodeQueueFamily;
        vkCtx->nb_decode_queues          = 1;
    } else {
        vkCtx->queue_family_decode_index = -1;
        vkCtx->nb_decode_queues          = 0;
    }
    if (m_ctx->encodeQueueFamily != UINT32_MAX) {
        vkCtx->queue_family_encode_index = (int)m_ctx->encodeQueueFamily;
        vkCtx->nb_encode_queues          = 1;
    } else {
        vkCtx->queue_family_encode_index = -1;
        vkCtx->nb_encode_queues          = 0;
    }
#endif

    // --- Lock condiviso con il renderer ---------------------------------------
    vkCtx->lock_queue   = ff_vk_lock_queue;
    vkCtx->unlock_queue = ff_vk_unlock_queue;

    // --- Init: FFmpeg adotta il tuo device ------------------------------------
    int err = av_hwdevice_ctx_init(hw_device_ctx);
    if (err < 0) {
        char e[256]; av_strerror(err, e, sizeof(e));
        DEJAVISUI_LOG_ERROR("[DECODER] av_hwdevice_ctx_init(VULKAN) fallito: %s", e);
        av_buffer_unref(&hw_device_ctx);   // -> nullptr
        return false;
    }

    hw_pix_fmt = AV_PIX_FMT_VULKAN;
    DEJAVISUI_LOG_DEBUG("[DECODER] FFmpeg Vulkan HW pronto sul device condiviso");
    return true;
}

void CAV_DECODER::InitDecoder(VulkanContext *_ctx,MultiChannelRingBuffer* audioBuf, int _samplerate, int _channels) {
    m_ctx = _ctx;
    m_audio_ring_buffer_planar = audioBuf;
    targetSamplerate    = _samplerate;
    targetChannels      = _channels;

    m_loader_exit.store(false);
    m_loader_thread = std::thread([this]{ loaderThreadFn(); });
    m_postProcessor = std::make_unique<CPostProcessor>(m_ctx);
    m_postProcessor->initYuvInput();
}

static enum AVPixelFormat get_hw_format(AVCodecContext* ctx,
                                        const enum AVPixelFormat* pix_fmts) {
    AVPixelFormat target = (AVPixelFormat)(intptr_t)ctx->opaque;

    std::string list;
    for (const enum AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        list += av_get_pix_fmt_name(*p);
        list += " ";
    }
    DEJAVISUI_LOG_DEBUG("[DECODER] get_format: target=%s, candidates=[%s] hw_device_ctx=%p extradata=%d",
        av_get_pix_fmt_name(target), list.c_str(),
        (void*)ctx->hw_device_ctx, ctx->extradata_size);

    for (const enum AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == target) {
            DEJAVISUI_LOG_DEBUG("[DECODER] returning HW format %s", av_get_pix_fmt_name(*p));
            return *p;
        }
    }
    DEJAVISUI_LOG_DEBUG("[DECODER] HW non found, fallback SW");
    return pix_fmts[0];
}

static bool codec_supports_vulkan(const AVCodec* dec) {
    for (int i = 0;; ++i) {
        const AVCodecHWConfig* cfg = avcodec_get_hw_config(dec, i);
        if (!cfg) break;
        if (cfg->device_type == AV_HWDEVICE_TYPE_VULKAN &&
            (cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
            return true;
    }
    return false;
}

void CAV_DECODER::loaderThreadFn() {
    while (true) {
        std::string path;
        {
            std::unique_lock<std::mutex> lk(m_loader_mutex);
            m_loader_cv.wait(lk, [&]{
                return m_loader_exit.load() || !m_loader_queue.empty();
            });
            if (m_loader_exit.load()) return;
            path = std::move(m_loader_queue.front());
            m_loader_queue.pop();
        }


        bool ok = LoadFile(path);
        if (!ok) {
            DEJAVISUI_LOG_ERROR("[DECODER] LoadFileAsync fallito: %s", path.c_str());
        }
        m_isLoading.store(false);
    }
}

void CAV_DECODER::LoadFileAsync(const std::string &_path) {
    {
        std::lock_guard<std::mutex> lk(m_loader_mutex);

        std::queue<std::string> empty;
        std::swap(m_loader_queue, empty);
        m_loader_queue.push(_path);
    }
    m_isLoading.store(true);
    m_loader_cv.notify_one();
}

bool CAV_DECODER::LoadFile(const std::string &_path) {

    if (m_running.load() || m_fmt_ctx != nullptr) {
        std::unique_lock<std::shared_mutex> lk(m_lifecycleMutex);
        DEJAVISUI_LOG_DEBUG("LoadFile: cleanup del file precedente");
        cleanupCurrentFile();
    }

    const bool isNetworkStream = (_path.find("://") != std::string::npos);
    shouldExit = false;

    // -------------------------------------------------------------------------
    // 1) Device Vulkan condiviso: creato UNA volta sola, persiste tra i file.
    // -------------------------------------------------------------------------
    if (hw_device_ctx == nullptr) {
        if (!InitFFmpegVulkanHW()) {
            DEJAVISUI_LOG_ERROR("[DECODER] Vulkan HW non disponibile, si usera' il fallback");
            // hw_device_ctx resta nullptr: nessun path Vulkan, solo fallback.
        }
    }

    // -------------------------------------------------------------------------
    // 2) Apertura container.
    // -------------------------------------------------------------------------
    m_fmt_ctx = avformat_alloc_context();
    if (!m_fmt_ctx) {
        DEJAVISUI_LOG_ERROR("avformat_alloc_context fallita");
        return false;
    }
    m_fmt_ctx->interrupt_callback.callback = [](void* opaque) -> int {
        return static_cast<CAV_DECODER*>(opaque)->shouldExit.load() ? 1 : 0;
    };
    m_fmt_ctx->interrupt_callback.opaque = this;

    if (avformat_open_input(&m_fmt_ctx, _path.c_str(), nullptr, nullptr) < 0) {
        DEJAVISUI_LOG_ERROR("Errore apertura input: %s", _path.c_str());
        return false;
    }
    if (avformat_find_stream_info(m_fmt_ctx, nullptr) < 0) {
        return false;
    }

    // Crea (pigramente) il device HW di fallback per la piattaforma corrente.
    auto ensureFallbackDevice = [this]() -> AVBufferRef* {
        if (m_fallback_hw_device_ctx) return m_fallback_hw_device_ctx;
        int r = -1;
#ifdef _WIN32
        r = av_hwdevice_ctx_create(&m_fallback_hw_device_ctx,
                                   AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
#elif defined(__linux__)
        r = av_hwdevice_ctx_create(&m_fallback_hw_device_ctx,
                                   AV_HWDEVICE_TYPE_VAAPI, "/dev/dri/renderD128", nullptr, 0);
#elif defined(__APPLE__)
        r = av_hwdevice_ctx_create(&m_fallback_hw_device_ctx,
                                   AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0);
#endif
        if (r < 0) {
            DEJAVISUI_LOG_ERROR("[DECODER] fallback HW device init fallito (%d)", r);
            m_fallback_hw_device_ctx = nullptr;
        }
        return m_fallback_hw_device_ctx;
    };

    auto fallbackPixFmt = []() -> AVPixelFormat {
#ifdef _WIN32
        return AV_PIX_FMT_D3D11;
#elif defined(__linux__)
        return AV_PIX_FMT_VAAPI;
#elif defined(__APPLE__)
        return AV_PIX_FMT_VIDEOTOOLBOX;
#else
        return AV_PIX_FMT_NONE;
#endif
    };

    // -------------------------------------------------------------------------
    // 3) Setup video con catena di tentativi:  Vulkan -> fallback HW -> software
    // -------------------------------------------------------------------------
    m_video_stream_idx = av_find_best_stream(m_fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_video_stream_idx >= 0) {
        AVStream*      v_stream = m_fmt_ctx->streams[m_video_stream_idx];
        const AVCodec* v_codec  = avcodec_find_decoder(v_stream->codecpar->codec_id);

        // Apre il codec con (device, target). device==nullptr => software.
        auto tryOpen = [&](AVBufferRef* dev, AVPixelFormat target) -> bool {
            m_video_ctx = avcodec_alloc_context3(v_codec);
            if (!m_video_ctx) return false;
            avcodec_parameters_to_context(m_video_ctx, v_stream->codecpar);

            if (dev) {
                m_video_ctx->hw_device_ctx = av_buffer_ref(dev);
                m_video_ctx->get_format    = get_hw_format;            // legge ctx->opaque
                m_video_ctx->opaque        = (void*)(intptr_t)target;
            }
            av_opt_set(m_video_ctx->priv_data, "delay", "0", 0);

            if (avcodec_open2(m_video_ctx, v_codec, nullptr) < 0) {
                avcodec_free_context(&m_video_ctx);                    // -> nullptr, ritenta
                return false;
            }
            return true;
        };

        bool opened = false;

        // 3a) Vulkan, se il device c'e' e il codec lo supporta.
        if (hw_device_ctx && v_codec && codec_supports_vulkan(v_codec)) {
            DEJAVISUI_LOG_DEBUG("[DECODER] tentativo decode Vulkan per %s", v_codec->name);
            opened = tryOpen(hw_device_ctx, AV_PIX_FMT_VULKAN);
            if (opened) {
                hw_pix_fmt = AV_PIX_FMT_VULKAN;
                DEJAVISUI_LOG_DEBUG("[DECODER] decode Vulkan attivo");
            } else {
                DEJAVISUI_LOG_DEBUG("[DECODER] Vulkan rifiutato a runtime, provo il fallback");
            }
        }

        // 3b) Fallback HW di piattaforma (VAAPI / D3D11VA / VideoToolbox).
        if (!opened && v_codec) {
            if (AVBufferRef* fb = ensureFallbackDevice()) {
                opened = tryOpen(fb, fallbackPixFmt());
                if (opened) {
                    hw_pix_fmt = fallbackPixFmt();
                    DEJAVISUI_LOG_DEBUG("[DECODER] decode HW fallback attivo");
                }
            }
        }

        // 3c) Software puro.
        if (!opened && v_codec) {
            opened = tryOpen(nullptr, AV_PIX_FMT_NONE);
            if (opened) {
                hw_pix_fmt = AV_PIX_FMT_NONE;
                DEJAVISUI_LOG_DEBUG("[DECODER] decode software attivo");
            }
        }

        if (opened) {
            m_frame_hw = av_frame_alloc();
            m_frame_sw = av_frame_alloc();
            DEJAVISUI_LOG_DEBUG("Video Ready: %s (%dx%d) target=%s",
                v_codec->name, m_video_ctx->width, m_video_ctx->height,
                av_get_pix_fmt_name(hw_pix_fmt));
        } else {
            DEJAVISUI_LOG_ERROR("[DECODER] impossibile aprire il video");
        }
    }

    // -------------------------------------------------------------------------
    // 4) Setup audio (invariato).
    // -------------------------------------------------------------------------
    m_audio_stream_idx = av_find_best_stream(m_fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_audio_stream_idx >= 0 && m_audio_ring_buffer_planar) {
        AVStream*      a_stream = m_fmt_ctx->streams[m_audio_stream_idx];
        const AVCodec* a_codec  = avcodec_find_decoder(a_stream->codecpar->codec_id);

        if (a_codec) {
            m_audio_ctx = avcodec_alloc_context3(a_codec);
            avcodec_parameters_to_context(m_audio_ctx, a_stream->codecpar);

            if (avcodec_open2(m_audio_ctx, a_codec, nullptr) >= 0) {
                m_audio_frame = av_frame_alloc();

                AVChannelLayout inLayout;
                av_channel_layout_copy(&inLayout, &m_audio_ctx->ch_layout);
                m_sourceChannels = inLayout.nb_channels;
                bool resamplerOk = decoderResampler.init(
                    m_audio_ctx->sample_rate, targetSamplerate,
                    inLayout.nb_channels, targetChannels,
                    m_audio_ctx->sample_fmt);
                av_channel_layout_uninit(&inLayout);

                if (!resamplerOk) {
                    DEJAVISUI_LOG_ERROR("Errore init resampler");
                    return false;
                }
                m_currentfilename = _path;
                DEJAVISUI_LOG_DEBUG("Audio ready: %d Hz -> %d Hz",
                    m_audio_ctx->sample_rate, targetSamplerate);
            }
        }
    }

    if (!m_video_ctx && !m_audio_ctx) return false;

    if (m_video_ctx) {
        DEJAVISUI_LOG_DEBUG("[DECODER] Stream: codec=%s profile=%d (%s) level=%d %dx%d pix_fmt=%s",
            avcodec_get_name(m_video_ctx->codec_id), m_video_ctx->profile,
            avcodec_profile_name(m_video_ctx->codec_id, m_video_ctx->profile),
            m_video_ctx->level, m_video_ctx->width, m_video_ctx->height,
            av_get_pix_fmt_name(m_video_ctx->pix_fmt));
    }

    ExtractMetadataFromContexts();

    m_masterclock.reset_vis_time();
    m_running  = true;
    shouldExit = false;

    m_demux_thread        = std::thread(&CAV_DECODER::demuxLoop,       this);
    m_decode_video_thread = std::thread(&CAV_DECODER::decodeVideoLoop, this);
    m_decode_audio_thread = std::thread(&CAV_DECODER::decodeAudioLoop, this);
    m_present_thread      = std::thread(&CAV_DECODER::presentLoop,     this);

    play();
    isPlaying = true;
    return true;
}

void CAV_DECODER::play() {
    m_paused.store(false);
    m_frame_cv.notify_all();
    m_video_pkt_cv.notify_all();
    m_audio_pkt_cv.notify_all();
    DEJAVISUI_LOG_DEBUG("Play");
}

void CAV_DECODER::pause() {
    m_paused.store(true);
    DEJAVISUI_LOG_DEBUG("Pause");
}

void CAV_DECODER::togglePause() {
    if (m_paused.load()) play();
    else                 pause();
}

void CAV_DECODER::seek(double seconds) {
    if (!m_fmt_ctx) return;
    seconds = std::max(0.0, std::min(seconds, metadata.duration));
    m_seekTargetSeconds.store(seconds);
    m_seekRequested.store(true);
    m_frame_cv.notify_all();
    m_video_pkt_cv.notify_all();
    m_audio_pkt_cv.notify_all();
    DEJAVISUI_LOG_DEBUG("Seek request: %.2fs", seconds);
}

void CAV_DECODER::seekRelative(double delta) {
    seek(currentPosition.load() + delta);
}

void CAV_DECODER::stop() {

    cleanup();
}

void CAV_DECODER::decodeLoop() {
    AVPacket* pkt = av_packet_alloc();

    while (!shouldExit) {

        if (m_seekRequested.exchange(false)) {
            double target = m_seekTargetSeconds.load();
            int64_t ts = static_cast<int64_t>(target * AV_TIME_BASE);

            av_seek_frame(m_fmt_ctx, -1, ts, AVSEEK_FLAG_BACKWARD);
            if (m_video_ctx) avcodec_flush_buffers(m_video_ctx);
            if (m_audio_ctx) avcodec_flush_buffers(m_audio_ctx);

            {
                std::lock_guard<std::mutex> lock(m_frame_mutex);
                while (!m_frame_queue.empty()) {
                    AVFrame* f = m_frame_queue.front().frame;
                    if (f) av_frame_free(&f);
                    m_frame_queue.pop();
                }
            }
            m_frame_cv.notify_all();

            if (m_audio_ring_buffer_planar) m_audio_ring_buffer_planar->reset();

            m_timerStarted.store(false);
            m_ptsStart.store(-1.0);
            currentPosition.store(target);
            m_eofReached.store(false);

            DEJAVISUI_LOG_DEBUG("Seek eseguito: %.2fs", target);
        }

        if (m_paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        int ret = av_read_frame(m_fmt_ctx, pkt);
        if (ret < 0) {
            av_packet_unref(pkt);

            if (ret == AVERROR_EOF) {

                if (m_video_ctx) {
                    avcodec_send_packet(m_video_ctx, nullptr);
                    while (avcodec_receive_frame(m_video_ctx, m_frame_hw) >= 0) {
                        if (m_frame_hw->format == m_video_ctx->pix_fmt && m_video_ctx->hw_device_ctx) {
                            av_hwframe_transfer_data(m_frame_sw, m_frame_hw, 0);
                            av_frame_copy_props(m_frame_sw, m_frame_hw);
                        } else {
                            av_frame_ref(m_frame_sw, m_frame_hw);
                        }

                        double pts = (m_frame_sw->pts == AV_NOPTS_VALUE) ? 0.0 :
                            m_frame_sw->pts * av_q2d(m_fmt_ctx->streams[m_video_stream_idx]->time_base);

                        AVFrame* cloned = av_frame_clone(m_frame_sw);
                        {
                            std::unique_lock<std::mutex> lock(m_frame_mutex);
                            m_frame_cv.wait(lock, [this] {
                                return m_frame_queue.size() < MAX_QUEUE_SIZE || shouldExit || m_seekRequested.load();
                            });
                            if (shouldExit) { av_frame_free(&cloned); break; }
                            m_frame_queue.push({ cloned, pts });
                        }
                        m_frame_cv.notify_all();

                        av_frame_unref(m_frame_hw);
                        av_frame_unref(m_frame_sw);
                    }
                }

                if (m_audio_ctx) {
                    avcodec_send_packet(m_audio_ctx, nullptr);
                    while (avcodec_receive_frame(m_audio_ctx, m_audio_frame) >= 0) {
                        int produced = 0;
                        AVFrame* out = decoderResampler.processFromAVFrame(m_audio_frame);
                        if (out && m_audio_ring_buffer_planar) {
                            while (m_running) {
                                if (m_audio_ring_buffer_planar->write(
                                        reinterpret_cast<const float* const*>(out->data),
                                        out->nb_samples)) {
                                    break;
                                        }
                                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            }
                        }
                        av_frame_unref(m_audio_frame);
                    }
                }
                isPlaying = false;
                if (m_loopEnabled.load()) {
                    m_seekRequested.store(true);
                    m_seekTargetSeconds.store(0.0);
                    continue;
                } else {
                    m_eofReached.store(true);
                    m_frame_cv.notify_all();
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            } else {
                DEJAVISUI_LOG_ERROR("av_read_frame error: %d", ret);
                break;
            }
        }

        if (pkt->stream_index == m_video_stream_idx) {
            if (avcodec_send_packet(m_video_ctx, pkt) >= 0) {
                while (avcodec_receive_frame(m_video_ctx, m_frame_hw) >= 0) {

                    if (m_frame_hw->format == m_video_ctx->pix_fmt && m_video_ctx->hw_device_ctx) {
                        av_hwframe_transfer_data(m_frame_sw, m_frame_hw, 0);
                        av_frame_copy_props(m_frame_sw, m_frame_hw);
                    } else {
                        av_frame_ref(m_frame_sw, m_frame_hw);
                    }

                    double pts = (m_frame_sw->pts == AV_NOPTS_VALUE) ? 0.0 :
                        m_frame_sw->pts * av_q2d(m_fmt_ctx->streams[m_video_stream_idx]->time_base);

                    AVFrame* cloned = av_frame_clone(m_frame_sw);
                    {
                        std::unique_lock<std::mutex> lock(m_frame_mutex);
                        m_frame_cv.wait(lock, [this] {
                            return m_frame_queue.size() < MAX_QUEUE_SIZE || shouldExit || m_seekRequested.load();
                        });
                        if (shouldExit || m_seekRequested.load()) {
                            av_frame_free(&cloned);
                            av_frame_unref(m_frame_hw);
                            av_frame_unref(m_frame_sw);
                            break;
                        }
                        m_frame_queue.push({ cloned, pts });
                    }
                    m_frame_cv.notify_all();

                    av_frame_unref(m_frame_hw);
                    av_frame_unref(m_frame_sw);
                }
            }
        } else if (pkt->stream_index == m_audio_stream_idx && m_audio_ctx) {
            if (avcodec_send_packet(m_audio_ctx, pkt) >= 0) {
                while (avcodec_receive_frame(m_audio_ctx, m_audio_frame) >= 0) {
                    AVFrame* out = decoderResampler.processFromAVFrame(m_audio_frame);
                    if (out && m_audio_ring_buffer_planar) {
                        double pts = (m_audio_frame->pts == AV_NOPTS_VALUE) ? 0.0 :
                            m_audio_frame->pts * av_q2d(m_fmt_ctx->streams[m_audio_stream_idx]->time_base);

                        const float* const* planar = reinterpret_cast<const float* const*>(out->data);
                        const size_t frames = out->nb_samples;

                        while (m_running && !shouldExit && !m_seekRequested.load()) {
                            if (m_audio_ring_buffer_planar->write(planar, frames)) break;
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }

                        currentPosition.store(pts);
                    }
                    av_frame_unref(m_audio_frame);
                }
            }
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    DEJAVISUI_LOG_DEBUG("decodeLoop exit");
}

void CAV_DECODER::presentLoop() {
    bool clockReady = false;
    double ptsStart = 0.0;

    while (!shouldExit) {
        DecodedFrame df;
        bool hasFrame = false;

        {
            std::unique_lock<std::mutex> lock(m_frame_mutex);
            if (m_frame_cv.wait_for(lock, std::chrono::milliseconds(10),
                [this] { return !m_frame_queue.empty() || shouldExit; })) {
                if (shouldExit) break;
                df = m_frame_queue.front();
                hasFrame = true;
            }
        }
        if (!hasFrame) continue;

        if (!clockReady) {
            ptsStart   = df.pts_seconds;
            clockReady = true;
            DEJAVISUI_LOG_DEBUG("Clock init: ptsStart=%.3f", ptsStart);
        }

        // Clock audio corretto
        double bufferLatency = 0.0;
        if (m_audio_ring_buffer_planar) {
            bufferLatency = (double)m_audio_ring_buffer_planar->getAvailableRead()
                          / (double)(48000);
        }
        double audioClock = currentPosition.load() - bufferLatency;
        double diff = df.pts_seconds - audioClock;

        if (diff > 0.001) {
            std::this_thread::sleep_for(std::chrono::duration<double>(diff - 0.001));
            continue;
        }

        if (diff < -0.080) {
            DEJAVISUI_LOG_DEBUG("Late frame pts=%.3f audio=%.3f diff=%.1fms",
                df.pts_seconds, audioClock, diff * 1000.0);
        }

        {
            std::unique_lock<std::mutex> lock(m_frame_mutex);
            m_frame_queue.pop();
            m_frame_cv.notify_all();
        }



        if (df.frame->format == AV_PIX_FMT_VULKAN) {
            //m_postProcessor->setEnabled(true);
            m_postProcessor->uploadYuvFrameVulkan(df.frame);
            av_frame_free(&df.frame);
        }
        else {
            AVFrame* tmpSw      = av_frame_alloc();
            AVFrame* finalFrame = ensureSoftwareFrame(df.frame, tmpSw);
            if (finalFrame) {
                m_postProcessor->setEnabled(true);
                m_postProcessor->uploadYuvFrame(finalFrame);
            }
            av_frame_free(&tmpSw);
            av_frame_free(&df.frame);
        }

    }
}

void CAV_DECODER::pushAudioToRingBuffer(AVFrame* frame) {
    if (!m_audio_ring_buffer_planar) return;

    int data_size = av_get_bytes_per_sample((AVSampleFormat)frame->format);

}

// av_decoder.cpp
bool CAV_DECODER::resizeOutput(uint32_t width, uint32_t height) {

    return true;
}



void CAV_DECODER::ExtractMetadataFromContexts() {
    AVDictionaryEntry *tag = nullptr;
    if (m_audio_ctx != nullptr && m_video_ctx != nullptr && m_fmt_ctx != nullptr) {
        metadata.video_codecName = m_video_ctx->codec->long_name;
        metadata.audio_codecName = m_audio_ctx->codec->long_name;
        metadata.audio_sampleRate = m_audio_ctx->sample_rate;
        metadata.audio_channels = m_audio_ctx->ch_layout.nb_channels;
        metadata.video_bitrate = m_video_ctx->bit_rate;
        metadata.audio_bitrate = m_audio_ctx->bit_rate;

        metadata.duration = static_cast<double>(m_fmt_ctx->duration) / AV_TIME_BASE;

    }
}

void CAV_DECODER::demuxLoop() {
    while (!shouldExit) {

       if (m_seekRequested.load()) {
            DEJAVISUI_LOG_DEBUG("demuxLoop: Eseguo seek a %f", m_seekTargetSeconds.load());

            double target = m_seekTargetSeconds.load();
            int64_t ts = static_cast<int64_t>(target * AV_TIME_BASE);

            if (av_seek_frame(m_fmt_ctx, -1, ts, AVSEEK_FLAG_BACKWARD) < 0) {
                DEJAVISUI_LOG_ERROR("av_seek_frame fallito");
            }

            {
                std::unique_lock<std::mutex> lockV(m_video_pkt_mutex);
                while(!m_video_packet_queue.empty()) {
                    AVPacket* p = m_video_packet_queue.front();
                    av_packet_free(&p);
                    m_video_packet_queue.pop();
                }
            }
            {
                std::unique_lock<std::mutex> lockA(m_audio_pkt_mutex);
                while(!m_audio_packet_queue.empty()) {
                    AVPacket* p = m_audio_packet_queue.front();
                    av_packet_free(&p);
                    m_audio_packet_queue.pop();
                }
            }

            m_seekRequested.store(false);
            m_video_pkt_cv.notify_all();
            m_audio_pkt_cv.notify_all();

            DEJAVISUI_LOG_DEBUG("demuxLoop: Seek completato");
            continue;
        }

        AVPacket* pkt = av_packet_alloc();
        int ret = av_read_frame(m_fmt_ctx, pkt);

        if (ret < 0) {
            av_packet_free(&pkt);
            if (ret == AVERROR_EOF) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            break;
        }

        if (pkt->stream_index == m_video_stream_idx) {
            std::unique_lock<std::mutex> lock(m_video_pkt_mutex);
            // IMPORTANTE: aggiungi m_seekRequested.load() nella wait!
            m_video_pkt_cv.wait(lock, [this] {
                return m_video_packet_queue.size() < MAX_PACKET_QUEUE || shouldExit || m_seekRequested.load();
            });

            if (shouldExit || m_seekRequested.load()) {
                av_packet_free(&pkt);
                continue;
            }

            m_video_packet_queue.push(pkt);
            m_video_pkt_cv.notify_one();
        } else if (pkt->stream_index == m_audio_stream_idx) {
            std::unique_lock<std::mutex> lock(m_audio_pkt_mutex);
            m_audio_pkt_cv.wait(lock, [this] {
                return m_audio_packet_queue.size() < MAX_PACKET_QUEUE || shouldExit || m_seekRequested.load();
            });

            if (shouldExit || m_seekRequested.load()) {
                av_packet_free(&pkt);
                continue;
            }

            m_audio_packet_queue.push(pkt);
            m_audio_pkt_cv.notify_one();
        } else {
            av_packet_free(&pkt);
        }
    }
    DEJAVISUI_LOG_DEBUG("demuxLoop exit");
}

void CAV_DECODER::decodeAudioLoop() {
    while (!shouldExit) {

        if (m_paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        if (m_seekRequested.load()) {
            if (m_audio_ctx) avcodec_flush_buffers(m_audio_ctx);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        AVPacket* pkt = nullptr;
        {
            std::unique_lock<std::mutex> lock(m_audio_pkt_mutex);
            m_audio_pkt_cv.wait(lock, [this] {
                return !m_audio_packet_queue.empty() || shouldExit || m_seekRequested.load();
            });
            if (shouldExit) break;
            if (m_seekRequested.load()) continue;
            if (m_audio_packet_queue.empty()) continue;

            pkt = m_audio_packet_queue.front();
            m_audio_packet_queue.pop();
        }
        m_audio_pkt_cv.notify_one();
        if (!pkt) continue;
        if (!m_audio_ctx) {
            av_packet_free(&pkt);
            continue;
        }
        if (avcodec_send_packet(m_audio_ctx, pkt) >= 0) {
            while (avcodec_receive_frame(m_audio_ctx, m_audio_frame) >= 0) {

                if (m_seekRequested.load() || shouldExit) {
                    av_frame_unref(m_audio_frame);
                    break;
                }

                AVFrame* out = decoderResampler.processFromAVFrame(m_audio_frame);

                if (out && m_audio_ring_buffer_planar) {
                    double pts = (m_audio_frame->pts == AV_NOPTS_VALUE) ? 0.0 :
                        m_audio_frame->pts * av_q2d(m_fmt_ctx->streams[m_audio_stream_idx]->time_base);

                    const float* const* planar = reinterpret_cast<const float* const*>(out->data);
                    const size_t frames = out->nb_samples;

                    while (m_running && !shouldExit && !m_seekRequested.load()) {
                        if (m_audio_ring_buffer_planar->write(planar, frames)) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }

                    if (!m_seekRequested.load())
                        currentPosition.store(pts);
                }

                av_frame_unref(m_audio_frame);
            }
        }

        av_packet_free(&pkt);
    }
    DEJAVISUI_LOG_DEBUG("decodeAudioLoop exit");
}

void CAV_DECODER::decodeVideoLoop() {
    while (!shouldExit) {

        if (m_paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        if (m_seekRequested.load()) {
            if (m_video_ctx) avcodec_flush_buffers(m_video_ctx);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue; // ricontrolla lo stato
        }

        AVPacket* pkt = nullptr;
        {
            std::unique_lock<std::mutex> lock(m_video_pkt_mutex);
            m_video_pkt_cv.wait(lock, [this] {
                return !m_video_packet_queue.empty() || shouldExit || m_seekRequested.load();
            });
            if (shouldExit) break;
            if (m_seekRequested.load()) continue;

            pkt = m_video_packet_queue.front();
            m_video_packet_queue.pop();
        }
        m_video_pkt_cv.notify_one();

        if (avcodec_send_packet(m_video_ctx, pkt) >= 0) {

            while (avcodec_receive_frame(m_video_ctx, m_frame_hw) >= 0) {
                if (m_seekRequested.load() || shouldExit) {
                    av_frame_unref(m_frame_hw);
                    break;
                }
                // Calcolo PTS
                double pts = (m_frame_hw->pts == AV_NOPTS_VALUE) ? 0.0 :
                    m_frame_hw->pts * av_q2d(m_fmt_ctx->streams[m_video_stream_idx]->time_base);

                AVFrame* frameToQueue = nullptr;

                if (m_frame_hw->format == AV_PIX_FMT_VULKAN) {
                    // Zero-copy: NIENTE readback
                    frameToQueue = av_frame_clone(m_frame_hw);
                }
                else if (m_frame_hw->hw_frames_ctx && gpucopy) {
                    // Fallback HW (VAAPI / D3D11): readback su CPU come prima.
                    av_frame_unref(m_frame_sw);
                    int ret = av_hwframe_transfer_data(m_frame_sw, m_frame_hw, 0);
                    if (ret < 0) {
                        DEJAVISUI_LOG_ERROR("Errore nel trasferimento HW->SW: %d", ret);
                        frameToQueue = av_frame_clone(m_frame_hw);
                    } else {
                        av_frame_copy_props(m_frame_sw, m_frame_hw);
                        frameToQueue = av_frame_clone(m_frame_sw);
                    }
                }
                else {
                    frameToQueue = av_frame_clone(m_frame_hw);
                }

                {
                    std::unique_lock<std::mutex> lock(m_frame_mutex);
                    m_frame_cv.wait(lock, [this] {
                        return m_frame_queue.size() < MAX_QUEUE_SIZE
                            || shouldExit
                            || m_seekRequested.load();
                    });

                    if (shouldExit || m_seekRequested.load()) {
                        av_frame_free(&frameToQueue);
                        av_frame_unref(m_frame_hw);
                        break;
                    }

                    m_frame_queue.push({ frameToQueue, pts });
                }
                m_frame_cv.notify_all();
                av_frame_unref(m_frame_hw);
            }
        }
        av_packet_free(&pkt);
    }
    DEJAVISUI_LOG_DEBUG("decodeVideoLoop exit");
}

void CAV_DECODER::stopThreads() {
    // 1. Segnala a tutti i thread di uscire
    shouldExit.store(true);
    m_running.store(false);
    m_loader_exit.store(true);
    DEJAVISUI_LOG_DEBUG("ATOMIC BOOL");

    m_video_pkt_cv.notify_all();
    m_audio_pkt_cv.notify_all();
    m_frame_cv.notify_all();
    m_loader_cv.notify_all();
    DEJAVISUI_LOG_DEBUG("NOTIFIFY");

    if (m_loader_thread.joinable())       m_loader_thread.join();
    if (m_demux_thread.joinable())        m_demux_thread.join();
    if (m_decode_video_thread.joinable()) m_decode_video_thread.join();
    if (m_decode_audio_thread.joinable()) m_decode_audio_thread.join();
    if (m_present_thread.joinable())      m_present_thread.join();
    DEJAVISUI_LOG_DEBUG("JOINB");
}

void CAV_DECODER::stopFileThreads() {
    // Segnala uscita ai soli thread per-file
    shouldExit.store(true);
    m_running.store(false);

    m_video_pkt_cv.notify_all();
    m_audio_pkt_cv.notify_all();
    m_frame_cv.notify_all();

    if (m_demux_thread.joinable())        m_demux_thread.join();
    if (m_decode_video_thread.joinable()) m_decode_video_thread.join();
    if (m_decode_audio_thread.joinable()) m_decode_audio_thread.join();
    if (m_present_thread.joinable())      m_present_thread.join();

    shouldExit.store(false);
}

void CAV_DECODER::stopLoaderThread() {

    m_loader_exit.store(true);
    shouldExit.store(true);
    m_loader_cv.notify_all();

    if (m_loader_thread.joinable() &&
        m_loader_thread.get_id() != std::this_thread::get_id()) {
        m_loader_thread.join();
    }
}

void CAV_DECODER::cleanupCurrentFile() {
    DEJAVISUI_LOG_DEBUG("[DECODER] cleanupCurrentFile start");

    stopFileThreads();

    {
        std::lock_guard<std::mutex> lk(m_video_pkt_mutex);
        while (!m_video_packet_queue.empty()) {
            AVPacket* p = m_video_packet_queue.front();
            m_video_packet_queue.pop();
            av_packet_free(&p);
        }
    }
    {
        std::lock_guard<std::mutex> lk(m_audio_pkt_mutex);
        while (!m_audio_packet_queue.empty()) {
            AVPacket* p = m_audio_packet_queue.front();
            m_audio_packet_queue.pop();
            av_packet_free(&p);
        }
    }
    {
        std::lock_guard<std::mutex> lk(m_frame_mutex);
        while (!m_frame_queue.empty()) {
            AVFrame* f = m_frame_queue.front().frame;
            m_frame_queue.pop();
            if (f) av_frame_free(&f);
        }
    }

    if (m_frame_hw)    av_frame_free(&m_frame_hw);
    if (m_frame_sw)    av_frame_free(&m_frame_sw);
    if (m_audio_frame) av_frame_free(&m_audio_frame);
    if (m_video_ctx)   avcodec_free_context(&m_video_ctx);
    if (m_audio_ctx)   avcodec_free_context(&m_audio_ctx);
    if (m_fmt_ctx)     avformat_close_input(&m_fmt_ctx);

    decoderResampler.cleanup();

    m_video_stream_idx = -1;
    m_audio_stream_idx = -1;

    m_timerStarted.store(false);
    m_ptsStart.store(-1.0);
    currentPosition.store(0.0);
    m_clockOffsetUs.store(0);
    m_paused.store(false);
    m_seekRequested.store(false);
    m_seekTargetSeconds.store(0.0);
    m_eofReached.store(false);
    m_sourceChannels = 0;
    m_currentfilename.clear();

    DEJAVISUI_LOG_DEBUG("[DECODER] cleanupCurrentFile done");
}

void CAV_DECODER::cleanup() {
    DEJAVISUI_LOG_DEBUG("[DECODER] cleanup (full) start");

    stopLoaderThread();
    cleanupCurrentFile();
    cleanupVulkan();
    if (m_postProcessor) {
        m_postProcessor->cleanup();
        m_postProcessor.reset();
    }
    if (hw_device_ctx) av_buffer_unref(&hw_device_ctx);

    // 4. Riferimenti non-owning
    m_audio_ring_buffer_planar = nullptr;
    m_ctx               = nullptr;

    DEJAVISUI_LOG_DEBUG("[DECODER] cleanup (full) done");
}

void CAV_DECODER::cleanupVulkan() {
    if (m_ctx && m_ctx->device) {
        vkDeviceWaitIdle(m_ctx->device);
    }
}

AVFrame* CAV_DECODER::ensureSoftwareFrame(AVFrame* src, AVFrame* tmpSwBuf) {
    if (!src) return nullptr;
    if (!src->hw_frames_ctx) {
        return src;            // gia' SW
    }
    int r = av_hwframe_transfer_data(tmpSwBuf, src, 0);
    if (r < 0) {
        char err[64]; av_strerror(r, err, sizeof(err));
        DEJAVISUI_LOG_ERROR("[AV] hwframe_transfer fallito: %s", err);
        return nullptr;
    }
    av_frame_copy_props(tmpSwBuf, src);
    return tmpSwBuf;
}


Json::Value CAV_DECODER::getJsonStatus() {

    Json::Value root;
    root["isPlaying"] = isPlaying.load();
    root["position"] = currentPosition.load();
    root["duration"] = metadata.duration;
    root["filename"] = m_currentfilename;
    root["video_codecName"] = metadata.video_codecName;
    root["audio_codecName"] = metadata.audio_codecName;
    root["audio_sampleRate"] = metadata.audio_sampleRate;
    root["audio_bitrate"] = static_cast<Json::Value::Int64>(metadata.audio_bitrate);
    root["audio_channels"] = metadata.audio_channels;
    root["video_bitrate"] = static_cast<Json::Value::Int64>(metadata.video_bitrate);
    root["isResampling"] = true;
    return root;
}