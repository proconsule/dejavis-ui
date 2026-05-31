#include "av_encoder.h"
#include <iostream>

CAV_ENCODER::CAV_ENCODER() {

}
CAV_ENCODER::~CAV_ENCODER() {
    cleanup();
}

bool CAV_ENCODER::init(bool ndi_output,int width, int height, int video_bitrate,int audio_bitrate, int sampleRate, MultiChannelRingBuffer* audioBuf) {
    if (m_running) {
        DEJAVISUI_LOG_DEBUG("[INIT] Encoder già attivo, eseguo cleanup automatico prima di ripartire...");
        cleanup();
    }
    m_audio_ring_buffer_planar = audioBuf;
    m_width = width;
    m_height = height;
    ndi_enabled = ndi_output;

    if (!setupVideo(width, height, video_bitrate)) return false;
    if (m_audio_ring_buffer_planar && sampleRate > 0) {
        if (setupAudio(sampleRate, 2,audio_bitrate)) m_audio_enabled = true;
    }

    m_masterclock.reset_vis_time();

    m_t0_us.store(-1);
    m_audio_samples_sent.store(0);

    if (!allocateSlotPool()) {
        DEJAVISUI_LOG_ERROR("[INIT] Allocazione pool YUV fallita");
        return false;
    }else {
        DEJAVISUI_LOG_DEBUG("Created %d slots",m_slots.size());
    }

    m_running = true;
    m_connected = false;

    m_video_thread = std::thread(&CAV_ENCODER::videoLoop, this);
    if (m_audio_enabled) {
        m_audio_thread = std::thread(&CAV_ENCODER::audioLoop, this);
    }

    if (m_watchdog_thread.joinable()) {
        m_watchdog_thread.join();
    }


    if (ndi_enabled) {
        m_ndi_sender.Init_VideoAudio(m_ctx,"Dejavis Video-Audio Out",width,height,48000,2);
    }
    m_watchdog_thread = std::thread(&CAV_ENCODER::connectionWatchdog, this);

    return true;
}

bool CAV_ENCODER::allocateSlotPool() {
    m_slots.clear();
    m_slots.reserve(SLOT_COUNT);
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        m_slots.push_back(std::make_unique<RGB2YUVSlotResources>());
    }
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_free_slots.clear();
        m_ready_slots.clear();
        for (auto& s : m_slots) {
            m_free_slots.push_back(s.get());
        }
    }
    return true;
}

void CAV_ENCODER::releaseSlotPool() {

    std::lock_guard<std::mutex> lock(m_queue_mutex);
    m_free_slots.clear();
    m_ready_slots.clear();
    m_slots.clear();
}

RGB2YUVSlotResources* CAV_ENCODER::acquireSlot() {
    if (!m_running.load(std::memory_order_relaxed)) return nullptr;
    if (m_active_services_count.load(std::memory_order_relaxed) == 0) return nullptr;

    std::lock_guard<std::mutex> lock(m_queue_mutex);
    if (m_free_slots.empty()) {
        // Encoder lento: droppiamo il frame. Zero stall sul renderer.
        return nullptr;
    }
    RGB2YUVSlotResources* slot = m_free_slots.front();
    m_free_slots.pop_front();
    return slot;
}

void CAV_ENCODER::submitSlot(RGB2YUVSlotResources* slot, int64_t pts_us) {
    if (!slot) return;
    slot->pts_us = pts_us;

    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_ready_slots.push_back(slot);
    }
    m_queue_cv.notify_one();
}

void CAV_ENCODER::addOutput(const std::string& url, const std::string& format) {
    auto service = std::make_shared<OutputService>();
    service->url = url;
    service->format_name = format;

    if (avformat_alloc_output_context2(&service->fmt_ctx, nullptr,
                                       format.empty() ? nullptr : format.c_str(),
                                       url.c_str()) < 0) {
        return;
                                       }

    if (m_video_codec_ctx) {
        addStreamToContext(service->fmt_ctx, m_video_codec_ctx, &service->video_stream);
    }

    if (m_audio_enabled && m_audio_codec_ctx) {
        addStreamToContext(service->fmt_ctx, m_audio_codec_ctx, &service->audio_stream);
    }

    {
        std::lock_guard<std::mutex> lock(m_services_vector_mutex);
        m_active_services.push_back(service);
        m_active_services_count.store(m_active_services.size(), std::memory_order_release);
    }

    DEJAVISUI_LOG_INFO("Service %s ready", url.c_str());
}

bool CAV_ENCODER::addStreamToContext(AVFormatContext* fmt_ctx, AVCodecContext* codec_ctx, AVStream** out_stream) {
    AVStream* stream = avformat_new_stream(fmt_ctx, nullptr);
    if (!stream) return false;

    if (avcodec_parameters_from_context(stream->codecpar, codec_ctx) < 0) {
        return false;
    }

    stream->codecpar->codec_tag = 0;
    stream->id = fmt_ctx->nb_streams - 1;

    if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (out_stream) *out_stream = stream;
    return true;
}

void CAV_ENCODER::pushToServices(AVPacket* pkt, bool is_video) {

    EncodedPacketCallback cb_copy;
    {
        std::lock_guard<std::mutex> lock(m_callback_mutex);
        cb_copy = m_encoded_packet_callback;
    }


    if (cb_copy) {
        AVCodecContext* src_codec = is_video ? m_video_codec_ctx : m_audio_codec_ctx;
        cb_copy(pkt, is_video, src_codec->time_base);
    }

    if (m_active_services_count.load(std::memory_order_acquire) == 0) return;

    std::vector<std::shared_ptr<OutputService>> services_to_process;
    {
        std::lock_guard<std::mutex> lock(m_services_vector_mutex);
        services_to_process = m_active_services; // Copia veloce di smart pointers
    }



    if (m_active_services_count.load() == 0) {
        static int warn_count = 0;
        return;
    }
    /*
    for (auto& service : services_to_process) {
        if (!service->connected.load()) {
            static int conn_warn = 0;
            continue;
        }
        std::lock_guard<std::mutex> lock(service->service_mutex);

        AVPacket* local_pkt = av_packet_clone(pkt);
        if (!local_pkt) continue;

        AVStream* target_stream = is_video ? service->video_stream : service->audio_stream;
        AVCodecContext* codec_ctx = is_video ? m_video_codec_ctx : m_audio_codec_ctx;

        if (!target_stream) {
            av_packet_free(&local_pkt);
            continue;
        }

        av_packet_rescale_ts(local_pkt, codec_ctx->time_base, target_stream->time_base);
        local_pkt->stream_index = target_stream->index;

        int64_t& last_dts = is_video ? service->last_dts_v : service->last_dts_a;

        if (local_pkt->dts <= last_dts) {
            local_pkt->dts = last_dts + 1;
        }
        if (local_pkt->pts < local_pkt->dts) {
            local_pkt->pts = local_pkt->dts;
        }
        last_dts = local_pkt->dts;

        int ret = av_interleaved_write_frame(service->fmt_ctx, local_pkt);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            DEJAVISUI_LOG_ERROR("[ENCODER] Errore scrittura su %s: %s", service->url.c_str(), errbuf);
            service->connected.store(false);
        } else {

        }

        av_packet_free(&local_pkt);

    }
    */
    for (auto& service : services_to_process) {
        if (!service->connected.load()) continue;

        // Prepariamo i metadati (PTS/DTS/StreamIndex) prima di inviarlo alla coda
        // per evitare race conditions tra i vari thread dei servizi.
        AVStream* target_stream = is_video ? service->video_stream : service->audio_stream;
        AVCodecContext* codec_ctx = is_video ? m_video_codec_ctx : m_audio_codec_ctx;

        if (!target_stream) continue;

        // Nota: Qui scaliamo i tempi PRIMA di clonare per la coda,
        // oppure lo facciamo dentro EnqueuePacket. Facciamolo qui per semplicità:
        av_packet_rescale_ts(pkt, codec_ctx->time_base, target_stream->time_base);
        pkt->stream_index = target_stream->index;

        // Invio asincrono
        service->EnqueuePacket(pkt);
    }
}

void CAV_ENCODER::connectionWatchdog() {
    while (m_running) {
        {
            std::lock_guard<std::mutex> lock(m_services_vector_mutex);
            for (auto& service : m_active_services) {
                if (!service->connected.load() && !service->is_connecting.load()) {
                    startConnectionThread(service);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

void CAV_ENCODER::tryConnectService(std::shared_ptr<OutputService> service) {
    std::lock_guard<std::mutex> lock(service->service_mutex);

    if (service->fmt_ctx->pb) {
        avio_closep(&service->fmt_ctx->pb);
    }

    AVDictionary* options = nullptr;
    av_dict_set(&options, "srt_latency", "20", 0);
    av_dict_set(&options, "rw_timeout", "2000000", 0); // 2 secondi timeout

    DEJAVISUI_LOG_DEBUG("[WATCHDOG] Tentativo connessione a: %s", service->url.c_str());

    if (avio_open2(&service->fmt_ctx->pb, service->url.c_str(), AVIO_FLAG_WRITE, nullptr, &options) < 0) {
        av_dict_free(&options);
        return;
    }

    if (avformat_write_header(service->fmt_ctx, &options) < 0) {
        avio_closep(&service->fmt_ctx->pb);
        av_dict_free(&options);
        return;
    }

    av_dict_free(&options);

    service->last_dts_v = -1;
    service->last_dts_a = -1;

    service->connected.store(true);
    DEJAVISUI_LOG_DEBUG("[WATCHDOG] Servizio connesso con successo: %s", service->url.c_str());
}

void CAV_ENCODER::startConnectionThread(std::shared_ptr<OutputService> service) {
    service->is_connecting.store(true);

    std::thread([this, service]() {
        std::lock_guard<std::mutex> lock(service->service_mutex);

        if (service->fmt_ctx->pb) {
            avio_closep(&service->fmt_ctx->pb);
        }

        AVDictionary* options = nullptr;
        av_dict_set(&options, "srt_latency", "20", 0);
        av_dict_set(&options, "rw_timeout", "2000000", 0);

        DEJAVISUI_LOG_DEBUG("[CONNECT-THREAD] Apertura: %s", service->url.c_str());

        if (avio_open2(&service->fmt_ctx->pb, service->url.c_str(), AVIO_FLAG_WRITE, nullptr, &options) >= 0) {

            if (avformat_write_header(service->fmt_ctx, &options) >= 0) {
                service->last_dts_v = -1;
                service->last_dts_a = -1;
                service->connected.store(true);
                DEJAVISUI_LOG_DEBUG("[CONNECT-THREAD] Successo: %s", service->url.c_str());
            } else {
                DEJAVISUI_LOG_ERROR("[CONNECT-THREAD] Errore Header: %s", service->url.c_str());
                avio_closep(&service->fmt_ctx->pb);
            }
        } else {
            DEJAVISUI_LOG_DEBUG("[CONNECT-THREAD] Fallimento apertura: %s", service->url.c_str());
        }

        av_dict_free(&options);
        service->is_connecting.store(false);
    }).detach();
}

bool CAV_ENCODER::TryVideoEncoder(std::string name,int width, int height, int bitrate,AVCodecContext** outCtx, AVPixelFormat* outPixFmt)
{
    const AVCodec* codec = avcodec_find_encoder_by_name(name.c_str());
    if (!codec) return false;

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) return false;

    // Trova il pix_fmt giusto per questo encoder
    AVPixelFormat pf = AV_PIX_FMT_NV12;
    for (auto& c : kCandidates) {
        if (std::string(c.name) == name) { pf = c.input_format; break; }
    }

    ctx->width        = width;
    ctx->height       = height;
    ctx->time_base    = {1, 1000000};
    ctx->framerate    = {60, 1};
    ctx->bit_rate     = bitrate;
    ctx->gop_size     = 60;
    ctx->keyint_min   = 60;
    ctx->max_b_frames = 0;
    ctx->pix_fmt      = pf;
    ctx->profile      = AV_PROFILE_H264_CONSTRAINED_BASELINE;

    // Opzioni per encoder (se vuoi raffinare per ognuno)
    const std::string nm = name;
    if (nm == "h264_nvenc") {
        av_opt_set    (ctx->priv_data, "preset",      "p1",       0);
        av_opt_set    (ctx->priv_data, "tune",        "ull",      0);
        av_opt_set    (ctx->priv_data, "rc",          "cbr",      0);
        av_opt_set_int(ctx->priv_data, "zerolatency", 1,          0);
        av_opt_set_int(ctx->priv_data, "forced-idr",  1,          0);
    } else if (nm == "h264_amf") {
        av_opt_set    (ctx->priv_data, "usage",       "ultralowlatency", 0);
        av_opt_set    (ctx->priv_data, "rc",          "cbr",      0);
        av_opt_set    (ctx->priv_data, "profile",     "baseline", 0);
    } else if (nm == "h264_qsv") {
        av_opt_set    (ctx->priv_data, "preset",      "veryfast", 0);
        av_opt_set    (ctx->priv_data, "look_ahead",  "0",        0);
    } else if (nm == "h264_mf") {
        av_opt_set    (ctx->priv_data, "rate_control","cbr",      0);
        av_opt_set    (ctx->priv_data, "scenario",    "live_streaming", 0);
    } else if (nm == "libx264") {
        av_opt_set(ctx->priv_data, "preset", "ultrafast",  0);
        av_opt_set(ctx->priv_data, "tune",   "zerolatency", 0);
    }
    if (name == "h264_vaapi") {
        if (init_vaapi_context(ctx)) {
            m_use_vaapi = true;
            ctx->pix_fmt        = AV_PIX_FMT_VAAPI;
            // Apri UNA SOLA VOLTA con tutti i parametri già settati
            if (avcodec_open2(ctx, codec, nullptr) >= 0) {
                DEJAVISUI_LOG_DEBUG("VAAPI ready: %dx%d @ %d kbps, gop=60, baseline",
                                    width, height, bitrate / 1000);

            }else {
                DEJAVISUI_LOG_ERROR("VAAPI avcodec_open2 failed");
            }

        }
    }else {
        int r = avcodec_open2(ctx, codec, nullptr);
        if (r < 0) {
            char err[256];
            av_strerror(r, err, sizeof(err));
            DEJAVISUI_LOG_DEBUG("[ENCODER] %s open failed: %s", "h264", err);
            avcodec_free_context(&ctx);
            return false;
        }
    }


    *outCtx    = ctx;
    *outPixFmt = pf;
    DEJAVISUI_LOG_INFO("[ENCODER] %s opened OK", name.c_str());
    return true;
}

bool CAV_ENCODER::setupVideo(int width, int height, int bitrate) {

#ifdef __APPLE__
    /*
    const AVCodec* codec = avcodec_find_encoder_by_name("h264_videotoolbox");

    if (!codec) {
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        DEJAVISUI_LOG_ERROR("FALLBACK TO libx264");
    }else {
        DEJAVISUI_LOG_INFO("Using h264_videotoolbox");
    }
    m_video_codec_ctx = avcodec_alloc_context3(codec);
    m_video_codec_ctx->pix_fmt = AV_PIX_FMT_NV12;

    m_video_codec_ctx->width          = width;
    m_video_codec_ctx->height         = height;
    m_video_codec_ctx->time_base      = {1, 1000000};
    m_video_codec_ctx->framerate      = {60, 1};
    m_video_codec_ctx->bit_rate       = bitrate;
    //m_video_codec_ctx->rc_max_rate    = bitrate;
    //m_video_codec_ctx->rc_buffer_size = bitrate;
    m_video_codec_ctx->gop_size       = 60;
    m_video_codec_ctx->keyint_min     = 60;
    m_video_codec_ctx->max_b_frames   = 0;
    m_video_codec_ctx->profile        = AV_PROFILE_H264_CONSTRAINED_BASELINE;
    m_video_codec_ctx->flags         &= ~AV_CODEC_FLAG_GLOBAL_HEADER;

    av_opt_set(m_video_codec_ctx->priv_data, "realtime", "1", 0);
    av_opt_set(m_video_codec_ctx->priv_data, "profile", "baseline", 0);



    if (avcodec_open2(m_video_codec_ctx, codec, nullptr) >= 0) {
        DEJAVISUI_LOG_DEBUG("Software/HW encoder %s ready: %dx%d @ %d kbps",
                            codec ? codec->name : "unknown",
                            width, height, bitrate / 1000);
        return true;
    }
    return false;
    */
    for (const auto& cand : kCandidates) {
        if (TryVideoEncoder(cand.name,width,height,bitrate,&m_video_codec_ctx,&m_choosen_pixfmt))break;
    }
    return true;
#else
    for (const auto& cand : kCandidates) {
        if (TryVideoEncoder(cand.name,width,height,bitrate,&m_video_codec_ctx,&m_choosen_pixfmt))break;
    }
    return true;
#endif

}

bool CAV_ENCODER::setupAudio(int sample_rate, int channels,int bitrate) {
    const AVCodec* codec = avcodec_find_encoder_by_name("libopus");
    if (!codec) return false;

    m_audio_codec_ctx = avcodec_alloc_context3(codec);

    m_audio_codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLT;
    m_audio_codec_ctx->sample_rate = sample_rate;
    m_audio_codec_ctx->bit_rate = bitrate;
    m_audio_codec_ctx->time_base = {1, sample_rate};

    av_channel_layout_default(&m_audio_codec_ctx->ch_layout, channels);

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "vbr", "on", 0);
    av_dict_set(&opts, "compression_level", "10", 0);

    if (avcodec_open2(m_audio_codec_ctx, codec, &opts) < 0) {
        av_dict_free(&opts);
        return false;
    }
    av_dict_free(&opts);

    m_audio_frame = av_frame_alloc();
    m_audio_frame->nb_samples = m_audio_codec_ctx->frame_size;
    m_audio_frame->format = m_audio_codec_ctx->sample_fmt;
    m_audio_frame->sample_rate = m_audio_codec_ctx->sample_rate;
    av_channel_layout_copy(&m_audio_frame->ch_layout, &m_audio_codec_ctx->ch_layout);

    if (av_frame_get_buffer(m_audio_frame, 0) < 0) return false;

    return true;
}

void CAV_ENCODER::pushFrame() {
    if (!m_running || m_active_services_count.load(std::memory_order_relaxed) == 0) {
        return;
    }

    m_masterclock.update();

    //YUVComputeResources rescopy = res;
    m_yuv_compute_resources->pts = m_masterclock.get_now_vis_us();

    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);


        while (m_frame_queue.size() >= MAX_QUEUE_SIZE) {
            m_frame_queue.pop();
        }
        //m_frame_queue.push(m_yuv_compute_resources);
    }

    m_queue_cv.notify_one();
}

void CAV_ENCODER::adjustT0(int64_t offset_us) {

    int64_t t0 = m_t0_us.load(std::memory_order_acquire);
    if (t0 < 0) return;  // non ancora fissato, niente da aggiustare
    m_t0_us.store(t0 - offset_us, std::memory_order_release);
}


void CAV_ENCODER::audioLoop() {
    DEJAVISUI_LOG_DEBUG("[AUDIO] Thread audioLoop avviato.\n");

    const int  sample_rate = m_audio_codec_ctx->sample_rate;
    const int  frame_size  = m_audio_codec_ctx->frame_size;
    const int  channels    = m_audio_codec_ctx->ch_layout.nb_channels;
    const AVSampleFormat enc_fmt = m_audio_codec_ctx->sample_fmt;
    const bool enc_planar  = av_sample_fmt_is_planar(enc_fmt) == 1;

    // Slot del ringbuffer = FRAME (non più sample interleaved totali)
    const size_t needed_frames = (size_t)frame_size;
    const int    slots_per_sec = sample_rate;          // niente più * channels

    // Buffer temp planar usato SOLO se l'encoder è interleaved (per fare la
    // passata di interleave finale). Allocati una volta sola fuori dal while.
    std::vector<std::vector<float>> tempPlanar;
    std::vector<float*>             tempPtrs;
    if (!enc_planar) {
        tempPlanar.assign(channels, std::vector<float>(frame_size));
        tempPtrs.resize(channels);
        for (int ch = 0; ch < channels; ch++)
            tempPtrs[ch] = tempPlanar[ch].data();
    }

    bool    first_sample_done   = false;
    int64_t m_audio_pts_counter = 0;

    while (m_running) {

        if (m_active_services_count.load(std::memory_order_relaxed) == 0) {
            m_audio_ring_buffer_planar->reset();
            first_sample_done   = false;
            m_audio_pts_counter = 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (m_audio_ring_buffer_planar->getAvailableRead() < needed_frames) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        // === Riferimento (writePtr, wall-clock) ===
        int64_t ref_wall_us   = av_gettime_relative();
        int64_t ref_slot      = (int64_t)m_audio_ring_buffer_planar->getWritePtr();
        int64_t read_slot_idx = (int64_t)m_audio_ring_buffer_planar->getReadPtr();



        // === Lettura dal ringbuffer ===
        bool ok = false;
        if (enc_planar) {
            // Scrivi direttamente nei buffer del frame del codec (FLTP)
            float* dst[AV_NUM_DATA_POINTERS] = {};
            for (int ch = 0; ch < channels; ch++)
                dst[ch] = (float*)m_audio_frame->data[ch];
            ok = m_audio_ring_buffer_planar->read(dst, needed_frames);
        } else {
            // Leggi in temp planar, poi interleava in data[0]
            ok = m_audio_ring_buffer_planar->read(tempPtrs.data(), needed_frames);
            if (ok) {
                float* dst = (float*)m_audio_frame->data[0];
                for (int i = 0; i < frame_size; i++) {
                    for (int ch = 0; ch < channels; ch++) {
                        dst[i * channels + ch] = tempPlanar[ch][i];
                    }
                }
            }
        }
        if (!ok) continue;

        // === PTS: tutte le quantità ora sono in FRAME, non in sample interleaved ===
        int64_t delta_slots = ref_slot - read_slot_idx;
        int64_t delta_us    = av_rescale_q(delta_slots,
                                           AVRational{1, slots_per_sec},
                                           AVRational{1, 1000000});
        int64_t first_sample_wall_us = ref_wall_us - delta_us;

        if (!first_sample_done) {
            getMasterClockUs();
            first_sample_done = true;
            DEJAVISUI_LOG_DEBUG("[AUDIO] primed: delta_slots=%lld (%lld us), T0=%lld",
                                (long long)delta_slots,
                                (long long)delta_us,
                                (long long)m_t0_us.load());
        }

        int64_t t0 = m_t0_us.load(std::memory_order_acquire);
        int64_t first_sample_stream_us = first_sample_wall_us - t0;

        int64_t pts_samples = av_rescale_q(first_sample_stream_us,
                                           AVRational{1, 1000000},
                                           AVRational{1, sample_rate});

        // Monotonicità
        if (m_audio_pts_counter > 0 && pts_samples <= m_audio_pts_counter) {
            pts_samples = m_audio_pts_counter;
        }

        m_audio_frame->pts = pts_samples;
        m_audio_pts_counter = pts_samples + frame_size;

        if (avcodec_send_frame(m_audio_codec_ctx, m_audio_frame) >= 0) {
            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(m_audio_codec_ctx, pkt) >= 0) {
                pushToServices(pkt, false);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
        }
    }

    DEJAVISUI_LOG_DEBUG("[AUDIO] Thread audioLoop terminato.\n");
}

void CAV_ENCODER::videoLoop() {
    DEJAVISUI_LOG_DEBUG("[VIDEO] Thread video avviato correttamente.");

    while (m_running.load(std::memory_order_relaxed)) {
        // Se non ci sono servizi, svuota la ready queue rimettendo gli slot come free.
        if (m_active_services_count.load(std::memory_order_relaxed) == 0) {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            while (!m_ready_slots.empty()) {
                m_free_slots.push_back(m_ready_slots.front());
                m_ready_slots.pop_front();
            }
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Attendi uno slot ready.
        RGB2YUVSlotResources* slot = nullptr;
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            m_queue_cv.wait_for(lock, std::chrono::milliseconds(20), [&]{
                return !m_ready_slots.empty() || !m_running.load();
            });
            if (!m_running.load()) break;
            if (m_ready_slots.empty()) continue;

            slot = m_ready_slots.front();
            m_ready_slots.pop_front();
        }
        VkResult fr = vkWaitForFences(m_ctx->device, 1, &slot->fence,
                                      VK_TRUE, 500'000'000ull);
        if (fr != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[VIDEO] vkWaitForFences fallita: %d", (int)fr);
            // Rimetti lo slot fra i free e skippa il frame.
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            m_free_slots.push_back(slot);
            continue;
        }

        std::vector<VkSemaphore> drained;
        RGB2YUVPipeline::drainPendingSemaphores(*slot, drained);


        AVFrame* encode_frame = nullptr;

        if (m_use_vaapi) {
            AVFrame* sw_frame = av_frame_alloc();
            sw_frame->format = (slot->format == RGB2YUVFormat::NV12)
                               ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
            sw_frame->width  = m_width;
            sw_frame->height = m_height;
            int planes = (slot->format == RGB2YUVFormat::NV12) ? 2 : 3;
            for (int i = 0; i < planes; i++) {
                sw_frame->data[i]     = (uint8_t*)slot->mappedPtrs[i];
                sw_frame->linesize[i] = (int)slot->strides[i];
            }

            encode_frame = av_frame_alloc();
            if (av_hwframe_get_buffer(m_video_codec_ctx->hw_frames_ctx, encode_frame, 0) < 0) {
                av_frame_free(&sw_frame);
                av_frame_free(&encode_frame);
                std::lock_guard<std::mutex> lock(m_queue_mutex);
                m_free_slots.push_back(slot);
                continue;
            }
            av_hwframe_transfer_data(encode_frame, sw_frame, 0);
            av_frame_free(&sw_frame);
        } else {
            encode_frame = av_frame_alloc();
            encode_frame->format = m_video_codec_ctx->pix_fmt;
            encode_frame->width  = m_width;
            encode_frame->height = m_height;
            int planes = (slot->format == RGB2YUVFormat::NV12) ? 2 : 3;
            for (int i = 0; i < planes; i++) {
                encode_frame->data[i]     = (uint8_t*)slot->mappedPtrs[i];
                encode_frame->linesize[i] = (int)slot->strides[i];
            }
        }



        encode_frame->pts = slot->pts_us;
        if (m_keyframe_requested.exchange(false, std::memory_order_acquire)) {
            encode_frame->pict_type = AV_PICTURE_TYPE_I;
            encode_frame->flags |= AV_FRAME_FLAG_KEY;
        }
        if (avcodec_send_frame(m_video_codec_ctx, encode_frame) >= 0) {
            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(m_video_codec_ctx, pkt) >= 0) {
                pushToServices(pkt, true);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
        }

        av_frame_free(&encode_frame);
        if (ndi_enabled) {
            m_ndi_sender.SendMuxedFrame(*slot);
        }
        // Slot encodato: torna nel pool dei free.
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            m_free_slots.push_back(slot);
        }
    }

    DEJAVISUI_LOG_DEBUG("[VIDEO] Thread videoLoop terminato.");
}

bool CAV_ENCODER::InitHW(VulkanContext *_ctx) {
    m_ctx = _ctx;
    return true;
}

void CAV_ENCODER::List_Hardware_Encoders() {
    m_available_encoders.clear();

    struct Target {
        std::string label;
        AVCodecID id;
    };

    std::vector<Target> targets = {
        {"H.264", AV_CODEC_ID_H264},
        {"HEVC",  AV_CODEC_ID_HEVC},
        {"VP9",   AV_CODEC_ID_VP9},
        {"AV1",   AV_CODEC_ID_AV1}
    };

    for (const auto& target : targets) {
        const AVCodec* codec = find_best_encoder_by_id(target.id);

        if (codec) {
            CodecInfo info;
            info.name = codec->name;
            info.id = target.id;

            info.is_hardware = (info.name.find("nvenc") != std::string::npos ||
                                info.name.find("qsv")   != std::string::npos ||
                                info.name.find("amf")   != std::string::npos);

            m_available_encoders.push_back(info);

            DEJAVISUI_LOG_DEBUG("[SCAN] %s supportato via %s (%s)\n",
                   target.label.c_str(),
                   info.name.c_str(),
                   info.is_hardware ? "HARDWARE" : "SOFTWARE");
        }
    }
}

void CAV_ENCODER::cleanup() {
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false)) {
        return;
    }

    DEJAVISUI_LOG_DEBUG("Inizio procedura di arresto...\n");

    m_queue_cv.notify_all(); // Sblocca il videoLoop se è in wait_for

    if (m_video_thread.joinable()) m_video_thread.join();
    if (m_audio_thread.joinable()) m_audio_thread.join();

    if (m_watchdog_thread.joinable()) m_watchdog_thread.join();

    flush_encoders();

    {
        std::lock_guard<std::mutex> lock(m_services_vector_mutex);
        for (auto& service : m_active_services) {
            std::lock_guard<std::mutex> s_lock(service->service_mutex);

            if (service->fmt_ctx) {
                if (service->connected.load()) {
                    av_write_trailer(service->fmt_ctx);
                }
                if (service->fmt_ctx->pb) {
                    avio_closep(&service->fmt_ctx->pb);
                }
                avformat_free_context(service->fmt_ctx);
                service->fmt_ctx = nullptr;
            }
        }
        m_active_services.clear();
    }

    if (m_video_codec_ctx) {
        avcodec_free_context(&m_video_codec_ctx);
        m_video_codec_ctx = nullptr;
    }
    if (m_audio_codec_ctx) {
        avcodec_free_context(&m_audio_codec_ctx);
        m_audio_codec_ctx = nullptr;
    }

    if (m_audio_frame) {
        av_frame_free(&m_audio_frame);
        m_audio_frame = nullptr;
    }

    {
        std::lock_guard<std::mutex> q_lock(m_queue_mutex);
        while(!m_frame_queue.empty()) m_frame_queue.pop();
    }

    releaseSlotPool();
    DEJAVISUI_LOG_DEBUG("Shutdown completato con successo.\n");
}

void CAV_ENCODER::releaseSlot(RGB2YUVSlotResources* slot) {
    if (!slot) return;
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    m_free_slots.push_back(slot);
}

void CAV_ENCODER::flush_encoders() {
    // Flush Video
    if (m_video_codec_ctx) {
        avcodec_send_frame(m_video_codec_ctx, nullptr); // Invia frame nullo per flush
        AVPacket* pkt = av_packet_alloc();
        while (avcodec_receive_packet(m_video_codec_ctx, pkt) >= 0) {
            pushToServices(pkt, true);
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
    }

    if (m_audio_codec_ctx) {
        avcodec_send_frame(m_audio_codec_ctx, nullptr);
        AVPacket* pkt = av_packet_alloc();
        while (avcodec_receive_packet(m_audio_codec_ctx, pkt) >= 0) {
            pushToServices(pkt, false);
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
    }
}

const AVCodec* CAV_ENCODER::find_best_encoder_by_id(AVCodecID id) {
    std::vector<std::string> hw_priority;

    if (id == AV_CODEC_ID_H264) {
        hw_priority = {"h264_nvenc", "h264_qsv", "h264_vaapi"};
    } else if (id == AV_CODEC_ID_HEVC) {
        hw_priority = {"hevc_nvenc", "hevc_qsv", "hevc_vaapi"};
    } else if (id == AV_CODEC_ID_AV1) {
        hw_priority = {"av1_nvenc", "av1_qsv","avi_vaapi"};
    }else if (id == AV_CODEC_ID_VP9) {
        hw_priority = {"vp9_nvenc", "vp9_qsv", "vp9_vaapi"};
    }

    for (const auto& name : hw_priority) {
        const AVCodec* codec = avcodec_find_encoder_by_name(name.c_str());
        if (codec) {
            if (test_encoder(name.c_str())) {
                DEJAVISUI_LOG_DEBUG("[AUTO-SELECT] Usando encoder hardware: %s", name.c_str());
                return codec;
            }
        }
    }

    DEJAVISUI_LOG_DEBUG("[AUTO-SELECT] Nessun hardware compatibile trovato. Uso fallback software.");
    return avcodec_find_encoder(id);
}

bool CAV_ENCODER::test_encoder(const char* name) {
    const AVCodec* codec = avcodec_find_encoder_by_name(name);
    if (!codec) return false;

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) return false;

    ctx->width = 640;
    ctx->height = 480;
    ctx->time_base = {1, 25};
    ctx->framerate = {25, 1};

    ctx->pix_fmt = AV_PIX_FMT_NV12;
    if (std::string(name).find("nvenc") != std::string::npos) {
        av_opt_set(ctx->priv_data, "preset", "p1", 0); // p1 = più veloce
    }


    int ret = avcodec_open2(ctx, codec, nullptr);


    avcodec_free_context(&ctx);

    return ret >= 0;
}

bool CAV_ENCODER::init_vaapi_context(AVCodecContext* ctx) {
    if (av_hwdevice_ctx_create(&m_vaapi_device_ref, AV_HWDEVICE_TYPE_VAAPI, "/dev/dri/renderD128", nullptr, 0) < 0) {
        return false;
    }

    AVBufferRef* frame_ref = av_hwframe_ctx_alloc(m_vaapi_device_ref);
    AVHWFramesContext* frames_ctx = (AVHWFramesContext*)frame_ref->data;
    frames_ctx->format = AV_PIX_FMT_VAAPI;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
    frames_ctx->width = ctx->width;
    frames_ctx->height = ctx->height;
    frames_ctx->initial_pool_size = 20;

    if (av_hwframe_ctx_init(frame_ref) < 0) {
        av_buffer_unref(&frame_ref);
        return false;
    }

    ctx->hw_frames_ctx = av_buffer_ref(frame_ref);
    av_buffer_unref(&frame_ref);
    return true;
}


int64_t CAV_ENCODER::peekMasterClockUs() const {
    int64_t t0 = m_t0_us.load(std::memory_order_acquire);
    if (t0 < 0) return 0;  // non ancora iniziato
    return av_gettime_relative() - t0;
}

int64_t CAV_ENCODER::getMasterClockUs() {
    const int64_t now = av_gettime_relative();  // wall clock monotono
    int64_t t0 = m_t0_us.load(std::memory_order_acquire);
    if (t0 < 0) {
        int64_t expected = -1;
        if (m_t0_us.compare_exchange_strong(expected, now)) {
            return 0;
        }
        t0 = m_t0_us.load(std::memory_order_acquire);
    }
    return now - t0;
}

Json::Value CAV_ENCODER::getSupportedVideoCodec() const {
    Json::Value root(Json::arrayValue);

    for (const auto& info : m_available_encoders) {
        Json::Value codecJson;
        codecJson["id"] = (int)info.id;
        codecJson["name"] = info.name;
        codecJson["is_hardware"] = info.is_hardware;

        codecJson["label"] = avcodec_get_name(info.id);

        root.append(codecJson);
    }

    return root;
}

void CAV_ENCODER::setEncodedPacketCallback(EncodedPacketCallback cb) {
    std::lock_guard<std::mutex> lock(m_callback_mutex);
    m_encoded_packet_callback = std::move(cb);
    m_has_webrtc_consumer.store(static_cast<bool>(m_encoded_packet_callback),
                                std::memory_order_release);
    DEJAVISUI_LOG_DEBUG("[ENCODER] callback %s",
                        m_encoded_packet_callback ? "registrata" : "rimossa");
}

void CAV_ENCODER::requestKeyframe() {
    m_keyframe_requested.store(true, std::memory_order_release);
}

Json::Value CAV_ENCODER::getStatus() {
    Json::Value status;

    status["encoder"]["active"] = m_running.load();
    if (m_video_codec_ctx) {
        status["encoder"]["codec"] = m_video_codec_ctx->codec->name;
        status["encoder"]["width"] = m_video_codec_ctx->width;
        status["encoder"]["height"] = m_video_codec_ctx->height;
        status["encoder"]["bitrate"] = (Json::Int64)m_video_codec_ctx->bit_rate;
    }

    status["outputs"] = Json::arrayValue;
    {
        std::lock_guard<std::mutex> lock(m_services_vector_mutex);
        for (const auto& service : m_active_services) {
            Json::Value s;
            s["url"] = service->url;
            s["format"] = service->format_name;
            s["connected"] = service->connected.load();
            s["is_connecting"] = service->is_connecting.load();

            status["outputs"].append(s);
        }
    }

    return status;
}
