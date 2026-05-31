#pragma once

#include <cstddef>
#include <Processing.NDI.Lib.h>
#include <vulkan/vulkan.h>
#include <string>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <vector>
#include "video/render_globals.h" // Per VulkanContext e VulkanUniTexture
#include "ringbuffer.h"
#include "multichannel_ringbuffer.h"
#include "video/rgb2yuv_pipeline.h"
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

struct NDIPacket {
    void* pYData;   // Punta a slot.mappedPtrs[0]
    void* pUVData;  // Punta a slot.mappedPtrs[1]

    int strideY;
    int strideUV;

    std::vector<float> audioRawData;
    int samples;

    uint32_t width, height;

};

class CNDISender {
public:
    CNDISender();
    ~CNDISender();

    bool Init_VideoAudio(VulkanContext* ctx, std::string channelName, int w, int h, int audioSR = 48000, int audioCh = 2);
    bool Init_AudioOnly(std::string channelName);
    void TriggerGPUCopy(VkCommandBuffer cmd, VulkanTexture& masterTex);
    void TriggerGPUCopy_Optimized(VkCommandBuffer cmd, VulkanTexture& masterTex);
    void SendMuxedFrame(RGB2YUVSlotResources& slot);
    void SendMuxedFrame_Async();

    void PushAudio(const float * const * data, int numSamples);

    MultiChannelRingBuffer audio_rignbuffer_planar{2,4096};
    void Stop();

    void Stop_Audio();

    bool isRunning() {
        return m_running;
    }

    RGB2YUVSlotResources* acquireSlot();
    void submitSlot(RGB2YUVSlotResources* slot, int64_t pts_us);
    void releaseSlot(RGB2YUVSlotResources* slot);

    size_t getSlotCount() const { return m_slots.size(); }
    RGB2YUVSlotResources& getSlot(size_t i) { return *m_slots[i]; }

private:
    void createReadbackBuffer(uint32_t w, uint32_t h);
    void destroyResources();
    void NDIWorkerThread();

    void NDIWorkerThread_Audio();

    VulkanContext* m_ctx = nullptr;
    NDIlib_send_instance_t m_pNDI_send = nullptr;

    // Risorse per il Readback (GPU -> CPU)
    struct ReadbackBuffer {
        VkBuffer       buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize   size   = 0;
    };

    // Metadati
    uint32_t m_width = 0, m_height = 0;
    int m_audioSR = 0, m_audioCh = 0;
    ReadbackBuffer m_readback[3];

    std::mutex         m_audioMutex;

    std::vector<float> m_audioPlanarBuffer;

    uint32_t m_ndiBufferIdx = 0;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;

    std::thread m_workerThread;
    std::condition_variable m_cv;
    std::mutex m_queueMutex;
    std::queue<RGB2YUVSlotResources *> m_packetQueue;



    std::thread             m_audioWorkerThread;
    std::condition_variable m_audioCv;
    std::mutex              m_audioCvMutex;

    std::atomic<bool> m_running{false};   // era: bool m_running = false;


    std::vector<std::unique_ptr<RGB2YUVSlotResources>> m_slots;

    std::deque<RGB2YUVSlotResources*> m_free_slots;
    std::deque<RGB2YUVSlotResources*> m_ready_slots;
    std::mutex m_queue_mutex;
    std::condition_variable m_queue_cv;
    static constexpr size_t SLOT_COUNT = 3;
    bool allocateSlotPool();
    void releaseSlotPool();

    void TransitionImageLayout(VkCommandBuffer cmd,
                           VulkanTexture&  tex,
                           VkImageLayout   newLayout,
                           VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    bool audioonly = false;
};