 #ifndef DEJAVIS_APP_AV_DECODER_H
#define DEJAVIS_APP_AV_DECODER_H

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <queue>

#include <vulkan/vulkan.h>
#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif
#ifdef __linux__
#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan_wayland.h>
#endif
#include <json/json.h>

#include <unistd.h>

#include "video/render_globals.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
    #include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vulkan.h>
#include <libavutil/hwcontext_drm.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

#include "ringbuffer.h"
#include "av_encdec_globals.h"
#include "dejatimer.h"
#include "audio/audio_resampler.h"
#include "video/render_globals.h"
#include "fs/localfilebrowser.h"


#include "video/postprocess.h"

#include "multichannel_ringbuffer.h"





class CAV_DECODER {
public:
    CAV_DECODER();
    ~CAV_DECODER();
    struct VideoFileMetadata {
        std::string video_codecName = "";
        std::string audio_codecName = "";
        int audio_sampleRate = 0;
        int audio_channels = 0;
        long long video_bitrate = 0;
        long long audio_bitrate = 0;
        double duration = 0.0;
        int width = -1;
        int height = -1;
        double fps = 0;
    };

    struct DecodedFrame {
        AVFrame* frame = nullptr;
        double pts_seconds = 0.0;
    };

    void InitDecoder(VulkanContext *_ctx,MultiChannelRingBuffer* audioBuf, int _samplerate, int _channels);
    void InitDecoderGPUCOPY(VulkanContext *_ctx,VulkanUniTexture * _tex,MultiChannelRingBuffer* audioBuf, int _samplerate, int _channels);
    bool LoadFile(const std::string& _path);

    bool ensureYUVSlotForFrame(AVFrame *f);

    bool InitFFmpegVulkanHW(AVBufferRef* shared_hw_ctx);

    void LoadFileAsync(const std::string& _path);

    void stop();
    void cleanup();

    void play();
    void pause();
    void togglePause();
    bool isPaused() const { return m_paused.load(); }

    void seek(double seconds);      // seek assoluto
    void seekRelative(double delta); // seek relativo a currentPosition

    double getDuration() const { return metadata.duration; }
    double getCurrentPosition() const { return currentPosition.load(); }

    bool getFrameForVulkan(YUVFrameData& out_data);
    bool getLatestFrame(AVFrame* out_frame);

    bool isRunning() const { return m_running; }

    int audio_mixer_id = -1;

    bool gpucopy = true;

    std::string getFileName() {
        return m_currentfilename;
    }

    AVCodecContext* getVideoCtx() {
        return m_video_ctx;
    }

    AVVulkanDeviceContext a;

    void SetFileBroswerBasePath(std::string _path) {
        FileBrowser.clearExtensions();
        FileBrowser.addExtension(".mp4");
        FileBrowser.addExtension(".avi");
        FileBrowser.addExtension(".mov");
        FileBrowser.addExtension(".mkv");
        FileBrowser.addExtension(".webm");

        FileBrowser.setRootPath(_path);
    }
    CLocalFileBrowser FileBrowser;


    uint32_t getOutputWidth() const {
        return m_postProcessor->getWidth();
    }
    uint32_t getOutputHeight() const {
       return m_postProcessor->getHeight();
    }

    bool resizeOutput(uint32_t width, uint32_t height);

    void TestYUVBuffersStatic();


    void submitPostProcess() {
        std::shared_lock<std::shared_mutex> lk(m_lifecycleMutex);
        if (m_postProcessor) m_postProcessor->submit();
    }
    void drainWaitSemaphores(std::vector<VkSemaphore>& out) {
        std::shared_lock<std::shared_mutex> lk(m_lifecycleMutex);
        if (m_postProcessor) m_postProcessor->getWaitSemaphores(out);
    }

    VkDescriptorSet getMixerDescriptorSet() {
        std::shared_lock<std::shared_mutex> lk(m_lifecycleMutex);
        return m_postProcessor ? m_postProcessor->getOutputDescriptorSet()
            : VK_NULL_HANDLE;
    }

    Json::Value getJsonStatus();


    void setKeyer(FxKeyerMode _keyer) {
        // Supponendo che 'm_postProcessor' sia la tua istanza di CPostProcessor
        if (m_postProcessor) {
            m_postProcessor->setEnabled(true);
            m_postProcessor->setKeyerMode(_keyer);
        }
    }

    void setLumaKey(LumaKeyParams &myparams) {
        if (m_postProcessor) {
            m_postProcessor->setEnabled(true);
            m_postProcessor->setLumaParams(myparams);
        }
    }

    void setChromaKey(KeyerPushConstants &_mycroma) {
        if (m_postProcessor) {
            m_postProcessor->setEnabled(true);
            m_postProcessor->setChromaParams(_mycroma);
        }
    }

    void setColor(ColorParams &col) {
        if (m_postProcessor) {
            m_postProcessor->setEnabled(true);
            m_postProcessor->setColorParams(col);
        }
    }

    CPostProcessor* getPostProcessor() { return m_postProcessor.get(); }

private:

    enum class Anime4kMode { Disabled, Factor, FixedTarget, AutoMax };
    void stopThreads();

    void stopFileThreads();

    void stopLoaderThread();

    void cleanupDeinterlaceFilter();

    void cleanupCurrentFile();

    void cleanupFFmpeg();
    void cleanupVulkan();

    AVFrame *ensureSoftwareFrame(AVFrame *src, AVFrame *tmpSwBuf);

    void initDeinterlaceFilter(AVFrame* frame);

    AVFilterGraph* m_filter_graph = nullptr;
    AVFilterContext* m_buffer_src = nullptr;
    AVFilterContext* m_buffer_sink = nullptr;


    std::unique_ptr<CPostProcessor> m_postProcessor;
    bool m_fxEnabled = false;

    // Wrapper per aggiornare i parametri FX dall'esterno


    // Nuovo metodo per ottenere il descrittore finale
    VkDescriptorSet getOutputDescriptorSet();

    std::atomic<bool>   m_paused{false};
    std::atomic<bool>   m_seekRequested{false};
    std::atomic<double> m_seekTargetSeconds{0.0};

    std::atomic<bool> m_eofReached{false};
    std::atomic<bool> m_loopEnabled{false};

    VulkanContext *m_ctx = nullptr;

    std::atomic<bool> isPlaying{false};
    std::atomic<bool> shouldExit{false};
    std::atomic<double> currentPosition{0.0};

    std::atomic<int64_t> m_clockOffsetUs{0};
    CDEJATIMER m_avTimer;
    std::atomic<bool> m_timerStarted{false};
    std::atomic<double> m_ptsStart{-1.0};

    VideoFileMetadata metadata;

    bool interlaced = false;

    void decodeLoop();


    void pushAudioToRingBuffer(AVFrame* frame);

    AVVulkanDeviceContext* vkDevCtx = nullptr;
    AVFormatContext* m_fmt_ctx = nullptr;
    AVCodecContext* m_video_ctx = nullptr;
    AVCodecContext* m_audio_ctx = nullptr;
    AVFrame* m_audio_frame = nullptr;

    //RingBuffer* m_audio_ring_buffer = nullptr;
    MultiChannelRingBuffer* m_audio_ring_buffer_planar = nullptr;

    AVFrame* m_frame_hw = nullptr;
    AVFrame* m_frame_sw = nullptr;
    int m_video_stream_idx = -1;
    int m_audio_stream_idx = -1;

    std::thread m_present_thread;
    std::thread m_decode_thread;        // Il thread che gestisce av_read_frame e la decodifica
    std::atomic<bool> m_running{false}; // Flag per controllare il ciclo di vita del thread

    // --- Sincronizzazione (Opzionale ma utile) ---

    std::queue<DecodedFrame> m_frame_queue;
    std::mutex m_frame_mutex;           // Per proteggere l'accesso ai frame decodificati
    std::condition_variable m_frame_cv; // Se vuoi svegliare il render solo quando c'è un frame nuovo
    static constexpr int MAX_QUEUE_SIZE = 8; // buffer massimo di frame

    CDEJATIMER m_masterclock;

    AudioResampler decoderResampler;

    int targetSamplerate = 0;
    int targetChannels = 0;

    int m_sourceChannels = 0;
    std::string m_currentfilename = "";
    void presentLoop();


    void ExtractMetadataFromContexts();


    // Packet queues
    std::queue<AVPacket*> m_video_packet_queue;
    std::queue<AVPacket*> m_audio_packet_queue;
    std::mutex m_video_pkt_mutex;
    std::mutex m_audio_pkt_mutex;
    std::condition_variable m_video_pkt_cv;
    std::condition_variable m_audio_pkt_cv;

    static constexpr int MAX_PACKET_QUEUE = 60;

    std::thread m_demux_thread;
    std::thread m_decode_video_thread;
    std::thread m_decode_audio_thread;

    void demuxLoop();
    void decodeVideoLoop();
    void decodeAudioLoop();

    bool InitComputeSync(YUVComputeResources& yuv);
    bool CreateYUVResources(YUVComputeResources * yuvcompute,uint32_t w, uint32_t h);

    void CleanupHWResources(YUVComputeResources* yuv);


    std::thread              m_loader_thread;
    std::mutex               m_loader_mutex;
    std::condition_variable  m_loader_cv;
    std::queue<std::string>  m_loader_queue;
    std::atomic<bool>        m_loader_exit{false};
    std::atomic<bool>        m_isLoading{false};

    void loaderThreadFn();

    AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE;
    uint32_t isNV12_flag = 0;

    AVBufferRef* hw_device_ctx = nullptr;

    std::atomic<bool> m_yuvFrameReady{false};

    AVBufferRef* m_fallback_hw_device_ctx = nullptr;

    std::shared_mutex m_lifecycleMutex;
};

#endif //DEJAVIS_APP_AV_DECODER_H