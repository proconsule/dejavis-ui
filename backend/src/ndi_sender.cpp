#include "ndi_sender.h"
#include "logger.h"
#include <cstring>

#include "video/vulkan_utils.h"

CNDISender::CNDISender() {
    NDIlib_initialize();
}

CNDISender::~CNDISender() {
    Stop();
    m_cv.notify_all(); // Lo svegliamo se sta dormendo sulla wait

    if (m_workerThread.joinable()) {
        m_workerThread.join(); // Aspettiamo che il thread finisca l'ultimo invio
    }

    if (m_pNDI_send) {
        NDIlib_send_destroy(m_pNDI_send);
    }


    DEJAVISUI_LOG_INFO("NDI Worker Thread arrestato correttamente");
}

bool CNDISender::allocateSlotPool() {
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

void CNDISender::releaseSlotPool() {
    // I buffer Vulkan vengono liberati dal renderer prima di distruggere
    // l'encoder (simmetria con l'allocazione). Qui svuotiamo solo le code.
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    m_free_slots.clear();
    m_ready_slots.clear();
    m_slots.clear();
}

RGB2YUVSlotResources* CNDISender::acquireSlot() {
    //if (!m_running.load(std::memory_order_relaxed)) return nullptr;

    std::lock_guard<std::mutex> lock(m_queue_mutex);
    if (m_free_slots.empty()) {
        return nullptr;
    }
    RGB2YUVSlotResources* slot = m_free_slots.front();
    m_free_slots.pop_front();
    return slot;
}

void CNDISender::submitSlot(RGB2YUVSlotResources* slot, int64_t pts_us) {
    if (!slot) return;
    slot->pts_us = pts_us;

    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_ready_slots.push_back(slot);
    }
    m_queue_cv.notify_one();
}

/*

static uint32_t ndiFindMemoryType(VulkanContext* ctx, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    // 1. Interroga la GPU per ottenere l'elenco di tutti i tipi di memoria disponibili
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(ctx->physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        // 2. Controlla se il bit i-esimo è impostato nel typeFilter
        // (il filtro arriva da vkGetBufferMemoryRequirements)
        if ((typeFilter & (1 << i)) &&
            // 3. Controlla se quel tipo di memoria possiede TUTTE le proprietà richieste
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
            }
    }

    // Se non troviamo nulla, restituiamo un valore sentinella
    return UINT32_MAX;
}

*/

bool CNDISender::Init_VideoAudio(VulkanContext* ctx, std::string channelName, int w, int h, int audioSR, int audioCh) {
    m_ctx = ctx;
    m_width = w;
    m_height = h;
    m_audioSR = audioSR;
    m_audioCh = audioCh;
    audioonly = false;

    // 1. Crea istanza Sender
    NDIlib_send_create_t desc;
    desc.p_ndi_name = channelName.c_str();
    desc.p_groups = nullptr;
    desc.clock_video = false;
    desc.clock_audio = false;


    m_pNDI_send = NDIlib_send_create(&desc);
    if (!m_pNDI_send) return false;

    // 2. Prepara buffer di lettura
    createReadbackBuffer(w, h);

    m_video_running = true;
    m_audio_running = true;
    m_workerThread = std::thread(&CNDISender::NDIWorkerThread, this);
    m_audioWorkerThread = std::thread(&CNDISender::NDIWorkerThread_Audio, this);
#ifdef _WIN32
    HANDLE hThread = (HANDLE)m_workerThread.native_handle();
    if (hThread) {

        if (SetThreadPriority(hThread, THREAD_PRIORITY_TIME_CRITICAL) == 0) {
            DEJAVISUI_LOG_WARN("[NDI] Impossibile impostare priorità alta");
        }

        DWORD_PTR affinityMask = 2;
        if (SetThreadAffinityMask(hThread, affinityMask) == 0) {
            DEJAVISUI_LOG_WARN("[NDI] Impossibile impostare affinità core");
        }

        DEJAVISUI_LOG_INFO("[NDI] Video Thread: THREAD_PRIORITY_TIME_CRITICAL, Core 1");
    }
    HANDLE haudioThread = (HANDLE)m_audioWorkerThread.native_handle();
    if (haudioThread) {

        if (SetThreadPriority(haudioThread, THREAD_PRIORITY_TIME_CRITICAL) == 0) {
            DEJAVISUI_LOG_WARN("[NDI] Impossibile impostare priorità alta");
        }

        DWORD_PTR affinityMask = 4;
        if (SetThreadAffinityMask(haudioThread, affinityMask) == 0) {
            DEJAVISUI_LOG_WARN("[NDI] Impossibile impostare affinità core");
        }

        DEJAVISUI_LOG_INFO("[NDI] Audio Thread: THREAD_PRIORITY_TIME_CRITICAL, Core 2");
    }
#endif
#ifdef __linux__
    pthread_t thread_id = m_workerThread.native_handle();
    if (thread_id) {

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(1, &cpuset);

        if (pthread_setaffinity_np(thread_id, sizeof(cpu_set_t), &cpuset) != 0) {
            DEJAVISUI_LOG_WARN("[NDI] Impossibile impostare affinità core");
        }

        struct sched_param param;
        param.sched_priority = sched_get_priority_max(SCHED_RR);

        if (pthread_setschedparam(thread_id, SCHED_FIFO, &param) != 0) {

        }else {
            DEJAVISUI_LOG_INFO("[NDI] Video Thread: SCHED_FIFO , Core 1");
        }
    }
    pthread_t threadaudio_id = m_audioWorkerThread.native_handle();
    if (threadaudio_id) {

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(2, &cpuset);

        if (pthread_setaffinity_np(threadaudio_id, sizeof(cpu_set_t), &cpuset) != 0) {
            DEJAVISUI_LOG_WARN("[NDI] Impossibile impostare affinità core");
        }

        struct sched_param param;
        param.sched_priority = sched_get_priority_max(SCHED_RR);


        if (pthread_setschedparam(threadaudio_id,SCHED_FIFO, &param) != 0) {
            DEJAVISUI_LOG_WARN("[NDI] Impossibile impostare priorità Real-time (richiesti permessi sudo/CAP_SYS_NICE)");
        } else {
            DEJAVISUI_LOG_INFO("[NDI] Video Thread: SCHED_FIFO , Core 2");
        }
    }
#endif


    if (!allocateSlotPool()) {
        DEJAVISUI_LOG_ERROR("[INIT] Allocazione pool YUV fallita");
        return false;
    }else {
        DEJAVISUI_LOG_DEBUG("Created %d slots",m_slots.size());
    }


    DEJAVISUI_LOG_INFO("[NDI-Sender] '%s' inizializzato (%dx%d)", channelName.c_str(), w, h);
    return true;
}

bool CNDISender::Init_AudioOnly(std::string channelName) {
    if (!NDIlib_initialize()) {
        return false;
    }


    audioonly = true;
    // 1. Crea istanza Sender
    NDIlib_send_create_t desc;
    desc.p_ndi_name = channelName.c_str();
    desc.clock_audio = true;
    m_audioCh = 2;
    m_audioSR = 48000;
    m_pNDI_send = NDIlib_send_create(&desc);
    if (!m_pNDI_send) return false;

    m_audio_running = true;
    m_workerThread = std::thread(&CNDISender::NDIWorkerThread_Audio, this);
    DEJAVISUI_LOG_DEBUG("Created NDI AudioOnly Output");

    return true;
}

void CNDISender::releaseSlot(RGB2YUVSlotResources* slot) {
    if (!slot) return;
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    m_free_slots.push_back(slot);
}

void CNDISender::createReadbackBuffer(uint32_t w, uint32_t h) {
    VkDeviceSize imageSize = w * h * 4;

    for (int i = 0; i < 3; i++) {
        m_readback[i].size = imageSize;

        // 1. Creazione Buffer
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = imageSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_ctx->device, &bufferInfo, nullptr, &m_readback[i].buffer) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[NDI-Sender] Fallita creazione buffer %d", i);
            continue;
        }

        // 2. Requisiti di memoria
        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(m_ctx->device, m_readback[i].buffer, &memReqs);

        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReqs.size;

        allocInfo.memoryTypeIndex = FindMemoryType(m_ctx, memReqs.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_ctx->device, &allocInfo, nullptr, &m_readback[i].memory) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[NDI-Sender] Fallita allocazione memoria buffer %d", i);
            continue;
        }

        // 4. Binding e Mapping persistente
        vkBindBufferMemory(m_ctx->device, m_readback[i].buffer, m_readback[i].memory, 0);

        // Mappiamo una volta sola e teniamo il puntatore (Persistent Mapping)
        if (vkMapMemory(m_ctx->device, m_readback[i].memory, 0, imageSize, 0, &m_readback[i].mapped) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[NDI-Sender] Fallito mapping memoria buffer %d", i);
        }
    }

    m_width = w;
    m_height = h;
    DEJAVISUI_LOG_INFO("[NDI-Sender] Creati 2 staging buffers per readback (%ux%u)", w, h);
}

void CNDISender::TriggerGPUCopy_Optimized(VkCommandBuffer cmd, VulkanTexture& masterTex) {

    uint32_t writeIdx = m_ndiBufferIdx % 3;

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { masterTex.width, masterTex.height, 1 };

    vkCmdCopyImageToBuffer(cmd, masterTex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m_readback[writeIdx].buffer, 1, &region);

    VkBufferMemoryBarrier bufferBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    bufferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bufferBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    bufferBarrier.buffer = m_readback[writeIdx].buffer;
    bufferBarrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, nullptr, 1, &bufferBarrier, 0, nullptr);
}


void CNDISender::SendMuxedFrame(RGB2YUVSlotResources& slot) {
    if (!m_pNDI_send) return;

    // 1. Controllo validità puntatori
    if (!slot.mappedPtrs[0] || !slot.mappedPtrs[1]) return;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_packetQueue.size() >= 2) {
            // La coda è già satura.
            // Droppiamo il frame ATTUALE per non sovraccaricare NDI
            // e restituiamo subito lo slot al pool dei free.
            releaseSlot(&slot);
            DEJAVISUI_LOG_WARN("[NDI] Coda satura, frame attuale droppato");
            return;
        }

        // Se c'è spazio, mettiamo lo slot in coda.
        // Lo slot non verrà rilasciato finché NDIWorkerThread non avrà finito.
        m_packetQueue.push(&slot);
        m_cv.notify_one();
    }
}


void CNDISender::NDIWorkerThread() {

    std::vector<uint8_t> nv12_buffer;
    while (m_video_running) {
        RGB2YUVSlotResources* slot = nullptr;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_cv.wait(lock, [this] { return !m_packetQueue.empty() || !m_video_running; });
            if (!m_video_running && m_packetQueue.empty()) break;

            slot = m_packetQueue.front();
            m_packetQueue.pop();
        }

        if (!slot) continue;

        // --- INVIO VIDEO (NV12) ---
        NDIlib_video_frame_v2_t v_frame;
        v_frame.xres = slot->width;
        v_frame.yres = slot->height;
        v_frame.FourCC = NDIlib_FourCC_type_NV12;
        v_frame.frame_rate_N        = 60000;
        v_frame.frame_rate_D        = 1000;
        v_frame.frame_format_type = NDIlib_frame_format_type_progressive;
        v_frame.timecode = NDIlib_send_timecode_synthesize;

        v_frame.line_stride_in_bytes = slot->strides[0];

        size_t y_size = (size_t)slot->strides[0] * slot->height;
        size_t uv_size = (size_t)slot->strides[1] * (slot->height / 2);
        size_t total_size = y_size + uv_size;

        if (nv12_buffer.size() < total_size) {
            nv12_buffer.resize(total_size);
        }

        // Copia i dati dallo slot (ora siamo sicuri che lo slot sia ancora "proprietà" di questo thread)
        std::memcpy(nv12_buffer.data(), slot->mappedPtrs[0], y_size);
        std::memcpy(nv12_buffer.data() + y_size, slot->mappedPtrs[1], uv_size);

        v_frame.p_data = nv12_buffer.data();

        //NDIlib_send_send_video_v2(m_pNDI_send, &v_frame);
        NDIlib_send_send_video_async_v2(m_pNDI_send, &v_frame);
        releaseSlot(slot);

    }

}

void CNDISender::PushAudio(const float * const * data, int numSamples) {
    std::lock_guard<std::mutex> lock(m_audioMutex);

    audio_rignbuffer_planar.write(data, numSamples);

}

void CNDISender::NDIWorkerThread_Audio() {

    constexpr int SAMPLES_PER_CH = 1920;

    // Buffer planari pre-allocati (NIENTE allocazioni nel loop)
    std::array<std::vector<float>, 2> tmpplanar_buffer;
    tmpplanar_buffer[0].resize(SAMPLES_PER_CH);
    tmpplanar_buffer[1].resize(SAMPLES_PER_CH);

    // Buffer contiguo per NDI: [ch0 ...][ch1 ...]
    std::vector<float> ndi_buffer(SAMPLES_PER_CH * 2);

    while (m_audio_running) {
        // Aspetta fino a quando c'è almeno un blocco o stiamo chiudendo.
        // wait_for con timeout = safety net contro CV mancata (raro ma possibile).
        {
            std::unique_lock<std::mutex> lock(m_audioCvMutex);
            m_audioCv.wait_for(lock, std::chrono::milliseconds(5), [this] {
                return !m_audio_running
                    || audio_rignbuffer_planar.getAvailableRead() >= (size_t)SAMPLES_PER_CH;
            });
        }
        if (!m_audio_running) break;
        if (!m_pNDI_send) continue;

        while (m_audio_running &&
               audio_rignbuffer_planar.getAvailableRead() >= (size_t)SAMPLES_PER_CH)
        {
            float* tempIn[2] = {
                tmpplanar_buffer[0].data(),
                tmpplanar_buffer[1].data()
            };
            audio_rignbuffer_planar.read(tempIn, SAMPLES_PER_CH);

            // Compatta in buffer contiguo per NDI
            std::memcpy(ndi_buffer.data(),
                        tempIn[0], SAMPLES_PER_CH * sizeof(float));
            std::memcpy(ndi_buffer.data() + SAMPLES_PER_CH,
                        tempIn[1], SAMPLES_PER_CH * sizeof(float));

            NDIlib_audio_frame_v2_t a_frame{};
            a_frame.sample_rate             = m_audioSR;
            a_frame.no_channels             = m_audioCh;            // 2
            a_frame.no_samples              = SAMPLES_PER_CH;
            a_frame.channel_stride_in_bytes = SAMPLES_PER_CH * sizeof(float);
            a_frame.timecode                = NDIlib_send_timecode_synthesize;
            a_frame.p_data                  = ndi_buffer.data();

            NDIlib_send_send_audio_v2(m_pNDI_send, &a_frame);

        }
    }
}

void CNDISender::Stop() {
    m_video_running = false;
    m_audio_running = false;
    m_cv.notify_all();         // sveglia video
    m_audioCv.notify_all();    // sveglia audio

    if (m_workerThread.joinable())      m_workerThread.join();
    if (m_audioWorkerThread.joinable()) m_audioWorkerThread.join();
    if (m_pNDI_send) {
        NDIlib_send_destroy(m_pNDI_send);
        m_pNDI_send = nullptr;
    }
    destroyResources();
}

void CNDISender::Stop_Audio() {
    m_audio_running = false;
    m_audioCv.notify_all();
    if (m_audioWorkerThread.joinable()) m_audioWorkerThread.join();
}

void CNDISender::destroyResources() {
    for (int i = 0; i < 3; i++) {
        if (m_readback[i].memory != VK_NULL_HANDLE) {
            vkUnmapMemory(m_ctx->device, m_readback[i].memory);
            vkFreeMemory(m_ctx->device, m_readback[i].memory, nullptr);
            m_readback[i].memory = VK_NULL_HANDLE;
        }
        if (m_readback[i].buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_ctx->device, m_readback[i].buffer, nullptr);
            m_readback[i].buffer = VK_NULL_HANDLE;
        }
        m_readback[i].mapped = nullptr;
    }
}