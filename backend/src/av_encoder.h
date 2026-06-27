//
// Created by ceco on 07/03/2026.
//

#ifndef DEJAVIS_APP_AV_ENCODER_H
#define DEJAVIS_APP_AV_ENCODER_H

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <functional>

#include <vulkan/vulkan.h>
#include "logger.h"
#include "ndi_sender.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vulkan.h>
}

#include "dejatimer.h"
#include "ringbuffer.h"
#include "video/render_globals.h"
#include  "ringbuffer.h"
#include "multichannel_ringbuffer.h"

#include <json/json.h>

#include "av_encdec_globals.h"
#include "logger.h"
#include "video/rgb2yuv_pipeline.h"
#include <memory>

struct CodecInfo {
    std::string name;
    bool is_hardware;
    AVCodecID id;
};

struct EncoderCandidate {
    const char* name;
    AVPixelFormat input_format;  // formato che la tua pipeline deve produrre
};

#ifdef __linux__
static const EncoderCandidate kCandidates[] = {
    { "h264_vulkan",  AV_PIX_FMT_VULKAN },
    { "h264_nvenc",   AV_PIX_FMT_NV12 },
    { "h264_vaapi",     AV_PIX_FMT_VAAPI },
    { "libx264",      AV_PIX_FMT_YUV420P },
};
#elif _WIN32
static const EncoderCandidate kCandidates[] = {
    { "h264_vulkan",  AV_PIX_FMT_VULKAN },
    { "h264_nvenc",   AV_PIX_FMT_NV12 },
    { "libx264",      AV_PIX_FMT_YUV420P },
};
#else
static const EncoderCandidate kCandidates[] = {
    { "h264_videotoolbox",   AV_PIX_FMT_YUV420P },
    { "libx264",      AV_PIX_FMT_YUV420P },
};
#endif

struct OutputService {
    AVFormatContext* fmt_ctx = nullptr;
    AVStream* video_stream = nullptr;
    AVStream* audio_stream = nullptr;

    std::string url;
    std::string format_name;

    std::atomic<bool> connected{false};
    std::atomic<bool> is_connecting{false};
    std::atomic<bool> running{true}; // Per gestire la chiusura del thread

    std::mutex service_mutex;

    // --- NUOVA LOGICA ASINCRONA ---
    std::queue<AVPacket*> packet_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::thread worker_thread;

    int64_t last_dts_v = -1;
    int64_t last_dts_a = -1;

    OutputService() {
        worker_thread = std::thread(&OutputService::ServiceWorker, this);
    }

    ~OutputService() {
        running.store(false);
        queue_cv.notify_all();
        if (worker_thread.joinable()) worker_thread.join();

        // Svuota la coda se sono rimasti pacchetti
        while(!packet_queue.empty()) {
            AVPacket* p = packet_queue.front();
            av_packet_free(&p);
            packet_queue.pop();
        }

        if (connected.load() && fmt_ctx) {
            av_write_trailer(fmt_ctx);
        }
        if (fmt_ctx && fmt_ctx->pb) avio_closep(&fmt_ctx->pb);
        if (fmt_ctx) avformat_free_context(fmt_ctx);
    }

    void EnqueuePacket(AVPacket* pkt) {
        if (!pkt) return;

        if (!connected.load()) {
            av_packet_free(&pkt);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (packet_queue.size() > 100) {
                AVPacket* old = packet_queue.front();
                av_packet_free(&old);
                packet_queue.pop();
            }
            packet_queue.push(pkt);
        }
        queue_cv.notify_one();
    }

private:
    void ServiceWorker() {
        while (running.load()) {
            AVPacket* pkt = nullptr;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [this] { return !packet_queue.empty() || !running.load(); });

                if (!running.load() && packet_queue.empty()) break;

                pkt = packet_queue.front();
                packet_queue.pop();
            }

            if (pkt) {
                std::lock_guard<std::mutex> lock(service_mutex);
                int ret = av_interleaved_write_frame(fmt_ctx, pkt);
                if (ret < 0) {
                    connected.store(false);
                }
                av_packet_free(&pkt);
            }
        }
    }
};

class CAV_ENCODER {
public:
    CAV_ENCODER();
    ~CAV_ENCODER();

    bool init(bool ndi_output,int width, int height, int video_bitrate,int audio_bitrate, int sampleRate, MultiChannelRingBuffer* audioBuf);

    bool InitVulkanEncoderHW(AVBufferRef *shared_hw_ctx);

    bool init_vaapi_context(AVCodecContext* ctx);
    void addOutput(const std::string& url, const std::string& format);
    bool addStreamToContext(AVFormatContext* fmt_ctx, AVCodecContext* codec_ctx, AVStream** out_stream);
    void pushToServices(AVPacket* pkt, bool is_video);
    std::atomic<size_t> m_active_services_count{0};

    bool InitHW(VulkanContext *_ctx);
    bool InitFramesContext(uint32_t w, uint32_t h);

    AVFrame *acquireEncodeVulkanFrame();

    bool sendEncodeVulkanFrame(AVFrame *f, int64_t pts, uint64_t signaledValue);

    void pushFrame();
    void cleanup();
    AVFrame* WrapVulkanImageInAVFrame(const YUVVideoResources& res);
    bool isRunning(){return m_running;};
    bool HaveServices(){return !m_active_services.empty();};

    void TestYUVTarget(VulkanContext *_ctx);

    void List_Hardware_Encoders();

    using EncodedPacketCallback =
    std::function<void(const AVPacket* pkt, bool is_video,
                       AVRational time_base)>;

    void setEncodedPacketCallback(EncodedPacketCallback cb);

    // Forza un keyframe sul prossimo frame video (chiamato dal broadcaster su PLI).
    void requestKeyframe();


    YUVComputeResources *m_yuv_compute_resources = nullptr;
    Json::Value getStatus();
    Json::Value getSupportedVideoCodec() const;

    RGB2YUVSlotResources* acquireSlot();
    void submitSlot(RGB2YUVSlotResources* slot, int64_t pts_us);
    void releaseSlot(RGB2YUVSlotResources* slot);

    size_t getSlotCount() const { return m_slots.size(); }
    RGB2YUVSlotResources& getSlot(size_t i) { return *m_slots[i]; }


    std::atomic<int64_t> m_audio_samples_sent{0};

    std::atomic<int64_t> m_t0_us{-1};  // wall-clock del T=0 dello stream

    // Metodo pubblico thread-safe
    int64_t getAudioClockUs() const {
        const int64_t n = m_audio_samples_sent.load(std::memory_order_acquire);
        const int64_t sr = m_audio_codec_ctx ? m_audio_codec_ctx->sample_rate : 48000;
        return av_rescale_q(n, {1, (int)sr}, {1, 1000000});
    }

    int64_t peekMasterClockUs() const;
    int64_t getMasterClockUs();
    void adjustT0(int64_t offset_us);
    const std::string& getEncoderName() const { return m_chosen_encoder_name; }

    CNDISender m_ndi_sender;


private:


    std::string   m_chosen_encoder_name;
    AVPixelFormat m_choosen_pixfmt;
    std::atomic<bool> m_use_vaapi{false};
    AVBufferRef* m_vaapi_device_ref = nullptr;

    bool ndi_enabled = false;

    void startConnectionThread(std::shared_ptr<OutputService> service);

    void connectionWatchdog();
    void tryConnectService(std::shared_ptr<OutputService> service);

    const AVCodec* find_best_encoder_by_id(AVCodecID id);
    bool test_encoder(const char* name);
    std::vector<CodecInfo> m_available_encoders;

    std::thread m_watchdog_thread;

    std::vector<std::shared_ptr<OutputService>> m_active_services;
    std::mutex m_services_vector_mutex;


    double m_last_push_time;
    VulkanContext *m_ctx;
    AVBufferRef* m_hw_device_ref = nullptr;
    AVCodecContext* m_enc_ctx = nullptr;
    AVBufferRef* m_hw_frames_ref = nullptr;
    YUVVideoResources m_current_res;

    bool setupVideo(int width, int height, int bitrate);
    bool TryVideoEncoder(std::string name,int width, int height, int bitrate,AVCodecContext** outCtx, AVPixelFormat* outPixFmt);
    bool setupAudio(int sample_rate, int channels,int bitrate);
    void videoLoop();
    void audioLoop();
    void flush_encoders();
    std::atomic<int64_t> m_current_audio_pts_us{0};

    AVBufferRef* hw_device_ctx = nullptr;
    AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE;

    AVCodecContext* m_video_codec_ctx = nullptr;
    AVCodecContext* m_audio_codec_ctx = nullptr;
    AVStream* m_video_stream = nullptr;
    AVStream* m_audio_stream = nullptr;
    AVFrame* m_persistent_frame = nullptr;
    AVFrame* m_audio_frame = nullptr;

    std::thread m_video_thread;
    std::thread m_audio_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    std::mutex m_muxer_mutex;

    std::mutex m_frame_mutex;
    std::vector<uint8_t> m_last_pixel_data;
    bool m_has_frame = false;

    MultiChannelRingBuffer * m_audio_ring_buffer_planar = nullptr;
    bool m_audio_enabled = false;

    CDEJATIMER m_masterclock;
    CDEJATIMER m_pushframetimer;
    double m_frame_accumulator = 0.0;
    std::string m_url;
    int m_width, m_height;

    VkSubresourceLayout m_current_layouts[3];


    std::queue<RGB2YUVSlotResources *> m_frame_queue;
    //std::mutex m_queue_mutex;
    //std::condition_variable m_queue_cv;
    const size_t MAX_QUEUE_SIZE = 3;

    double m_last_sent_pts = 0.0f;


    static constexpr size_t SLOT_COUNT = 3;
    std::vector<std::unique_ptr<RGB2YUVSlotResources>> m_slots;

    std::deque<RGB2YUVSlotResources*> m_free_slots;
    std::deque<RGB2YUVSlotResources*> m_ready_slots;
    std::mutex m_queue_mutex;
    std::condition_variable m_queue_cv;

    bool allocateSlotPool();
    void releaseSlotPool();

    EncodedPacketCallback m_encoded_packet_callback;
    std::mutex m_callback_mutex;
    std::atomic<bool> m_keyframe_requested{false};

    std::atomic<bool> m_has_webrtc_consumer{false};

    std::atomic<bool>            m_use_vulkan_enc{false};
    VkCommandPool                m_encCopyPool{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> m_encCopyCmds;
    std::vector<VkFence>         m_encCopyFences;
    uint32_t                     m_encCopyIdx{0};
    bool createCopyRing();
    void destroyCopyRing();
    bool encodeSlotVulkan(RGB2YUVSlotResources* slot);

};

#endif //DEJAVIS_APP_AV_ENCODER_H