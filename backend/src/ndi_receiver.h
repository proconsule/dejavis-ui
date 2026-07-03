#pragma once

#include <cstddef>
#include <Processing.NDI.Lib.h>
#include <vulkan/vulkan.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <functional>
#include <vector>
#include <json/json.h>

#include "video/render_globals.h"
#include "ringbuffer.h"
#include "audio/audio_resampler.h"
#include "video/postprocess.h"

#include <chrono>
#include "multichannel_ringbuffer.h"

#include "video/yuv2rgb/YUV2RGBPipeline.h"
#include "video//vulkan_utils.h"


namespace {
    inline int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

struct NDISourceInfo {
    std::string name;
    std::string url_address;
};

struct NDISourceInternal {
    NDISourceInfo info;
    int keepAlive = 5;
};

class CNDIReceiver {
public:
    CNDIReceiver();
    ~CNDIReceiver();

    void Init_VideoAudio(VulkanContext* ctx,
              VulkanUniTexture *_tex,MultiChannelRingBuffer* audioBuf,
              int targetSampleRate,
              int targetChannels);

    void Init_AudioOnly(MultiChannelRingBuffer* audioBuf,
                        int targetSampleRate,
                        int targetChannels);

    void StartFinder();

    void destroyVulkanTextureInternal();

    void StopFinder();

    const VulkanUniTexture * GetTexture() const { return m_texture; }

    Json::Value listSources();

    bool Start(const std::string& sourceName = "");

    void Stop();



    bool isRunning() const { return m_running.load(); }


    void handleYUVFrame(const NDIlib_video_frame_v2_t &v);

    Json::Value getJsonStatus() const;


    std::function<void(const NDIlib_video_frame_v2_t&)> OnVideoFrame;
    std::function<void(const NDIlib_audio_frame_v2_t&)> OnAudioFrame;


    void updateTextureFromStaging(VkCommandBuffer cmd);

    int m_pendingWidth = 0;
    int m_pendingHeight = 0;
    std::atomic<bool> m_needsRecreate{false};


    void setKeyer(FxKeyerMode _keyer) {
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

    int getOutputWidth()  { return m_postProcessor->getWidth(); }
    int getOutputHeight() { return m_postProcessor->getHeight(); }


    VkDescriptorSet getMixerDescriptorSet() const {
        return m_postProcessor->getOutputDescriptorSet();
    }

    CPostProcessor* getPostProcessor() { return m_postProcessor.get(); }

    std::vector<NDISourceInfo> getAvailableSources();

    void SetVideoTimeoutMs(int ms)   { m_videoTimeoutMs = std::max(100, ms); }
    bool isVideoSignalLost() const   { return m_videoSignalLost.load(); }
    void processTextureLifecycle();



private:

    struct PendingDestroy {
        uint64_t frameIndex;
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    std::vector<PendingDestroy> m_destructionQueue;
    std::mutex m_destructionMutex;
    uint64_t m_currentFrameIndex = 0;

    std::atomic<uint64_t> m_textureVersion{0};
    std::mutex m_textureMutex; // Protegge l'accesso agli handle in m_texture


    void receiverThread();
    bool ensureStagingForFrame(uint32_t w, uint32_t h);
    void destroyStaging();

    /*
    YUV2RGBSlotResources m_yuvSlot;

    bool                m_yuvSlotCreated = false;
    std::atomic<bool>   m_yuvFrameReady{false};
    std::mutex          m_yuvUploadMutex;

    void handleYUVFrame(const NDIlib_video_frame_v2_t& v);
    void destroyYUVSlot();
*/
    std::unique_ptr<CPostProcessor> m_postProcessor;
    bool m_fxEnabled = false;

    // === Risorse esterne (non-owning) ===
    VulkanContext*    m_ctx           = nullptr;
    VulkanUniTexture* m_texture       = nullptr;
    //RingBuffer*       m_audio_ring    = nullptr;
    MultiChannelRingBuffer* m_audio_ring_planar    = nullptr;
    int               m_targetSR      = 48000;
    int               m_targetCh      = 2;

    // === NDI ===
    NDIlib_recv_instance_t m_pNDI_recv = nullptr;

    void recreateVulkanTexture(uint32_t width, uint32_t height);
    void destroyVulkanTexture();

    struct StagingBuffer {
        VkBuffer       buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void*          mapped = nullptr;
        VkDeviceSize   size   = 0;
        uint32_t       width  = 0;
        uint32_t       height = 0;
    };
    StagingBuffer    m_staging[2];
    std::atomic<int> m_stagingReadIdx{0};   // quale e' "pieno e pronto"
    std::atomic<bool> m_frameReady{false};  // c'e' un frame da copiare
    std::mutex       m_stagingMutex;        // protegge size/format change

    AudioResampler   m_resampler;
    int              m_sourceSR = 0;
    int              m_sourceCh = 0;

    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_shouldExit{false};

    std::string m_currentSource;
    std::atomic<uint32_t> m_videoFrameCount{0};
    std::atomic<uint32_t> m_audioFrameCount{0};
    std::atomic<uint32_t> m_videoWidth{0};
    std::atomic<uint32_t> m_videoHeight{0};
    std::atomic<float>    m_videoFps{0.0f};


    bool              m_textureNeedsDescriptorUpdate = false;

    void TransitionImageLayout(VkCommandBuffer cmd,
                           VulkanTexture&  tex,
                           VkImageLayout   newLayout,
                           VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);



    std::thread m_findThread;
    std::atomic<bool> m_stopFinder{false};
    std::mutex m_sourcesMutex;
    std::vector<NDISourceInfo> m_discoveredSources;

    void finderThreadLoop();

    std::atomic<int64_t> m_lastVideoMs{0};            // ms da steady_clock, 0 = mai
    std::atomic<bool>    m_videoSignalLost{false};
    std::atomic<bool>    m_pendingTextureDestroy{false};
    int                  m_videoTimeoutMs = 1500;

    bool audioonly = false;

};