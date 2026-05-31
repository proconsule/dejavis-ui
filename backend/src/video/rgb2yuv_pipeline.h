#ifndef DEJAVIS_APP_RGB2YUV_PIPELINE_H
#define DEJAVIS_APP_RGB2YUV_PIPELINE_H

#include <vulkan/vulkan.h>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <vector>

#include "render_globals.h"

// Formato di output YUV scelto a runtime per lo slot.
enum class RGB2YUVFormat : uint32_t {
    NV12     = 0,   // 2 piani: Y, UV interleaved
    YUV420P  = 1    // 3 piani: Y, U, V separati
};

// =============================================================================
//  Slot resources — ogni encoder ne possiede uno
// =============================================================================
struct RGB2YUVSlotResources {
    // --- Buffer host-visible Y / U / V (output letti dall'encoder) ---
    // NV12:    [0]=Y,  [1]=UV interleaved, [2]=unused
    // YUV420P: [0]=Y,  [1]=U,             [2]=V
    VkBuffer       buffers[3]        = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory bufferMemories[3] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    void*          mappedPtrs[3]     = { nullptr, nullptr, nullptr };
    VkDeviceSize   bufferSizes[3]    = { 0, 0, 0 };
    int            strides[3]        = { 0, 0, 0 };  // riempiti in createSlot

    // --- Descriptor pool/set ---
    VkDescriptorPool descriptorPool       = VK_NULL_HANDLE;
    VkDescriptorSet  computeDescriptorSet = VK_NULL_HANDLE;

    // --- Sync (per istanza) ---
    VkCommandBuffer cmd               = VK_NULL_HANDLE;
    VkFence         fence             = VK_NULL_HANDLE;
    std::mutex      submitMutex;

    // --- Ring di semafori (stessa logica del YUV2RGB) ---
    static constexpr int kSemRing = 4;
    VkSemaphore semRing[kSemRing] = {};
    uint32_t    semWriteIdx = 0;

    std::mutex                pendingSemMutex;
    std::vector<VkSemaphore>  pendingSemaphores;

    VkSemaphore computeFinished = VK_NULL_HANDLE;  // alias del ring

    // Semaforo che il PRODUCER (renderer) firma nel suo submit grafico per
    // segnalare "render del mixer finito, texture RGBA pronta da leggere".
    // submitAsync lo passa come waitSemaphore del proprio submit RGB2YUV.
    // Vita: creato in createSlot, distrutto in destroySlot. Riusabile ad ogni
    // frame perche' il submit RGB2YUV lo consuma.
    VkSemaphore mixerDoneSemaphore = VK_NULL_HANDLE;

    std::atomic<bool> submitted{false};
    std::atomic<bool> semaphoreSignaled{false};

    // --- Stato runtime ---
    uint32_t width     = 0;
    uint32_t height    = 0;
    RGB2YUVFormat format    = RGB2YUVFormat::NV12;
    int           rangeFull = 0;        // 0 = TV (limited), 1 = full
    bool          valid     = false;

    // PTS associato al frame contenuto. Settato dal producer (encoder), letto
    // dal videoLoop prima di passare i buffer all'AVFrame.
    int64_t       pts_us    = 0;

    // Riferimento all'input — non posseduto dallo slot, deve restare valido
    // tra createSlot e destroySlot. Tipicamente e' una texture RGBA gia' in
    // SHADER_READ_ONLY_OPTIMAL (output del mixer/render).
    VkImageView  inputView   = VK_NULL_HANDLE;

    VkSemaphore           mixerTimeline      = VK_NULL_HANDLE;
    std::atomic<uint64_t> mixerTimelineValue{0};
};

class RGB2YUVPipeline {
public:
    static RGB2YUVPipeline& instance() {
        static RGB2YUVPipeline inst;
        return inst;
    }


    RGB2YUVPipeline() = default;
    ~RGB2YUVPipeline() = default;
    RGB2YUVPipeline(const RGB2YUVPipeline&) = delete;
    RGB2YUVPipeline& operator=(const RGB2YUVPipeline&) = delete;

    bool init(VulkanContext* ctx);
    void shutdown();
    bool isInitialized() const { return m_initialized; }

    // Crea uno slot di conversione. inputView punta a una VkImage RGBA che lo
    // shader legge come storage image (deve essere stata creata con
    // VK_IMAGE_USAGE_STORAGE_BIT). Il layout atteso al momento del dispatch
    // e' GENERAL — il chiamante deve transitarla prima del submit, oppure
    // usare l'input come SHADER_READ_ONLY (vedi nota sotto).
    //
    // Nota: per leggere la texture come SAMPLED si potrebbe aggiungere un
    // sampler, ma per conversione 1:1 RGBA->YUV non serve filtraggio. Storage
    // image e' la scelta minimale.
    bool createSlot(RGB2YUVSlotResources& slot,
                    uint32_t width, uint32_t height,
                    RGB2YUVFormat format,
                    VkImageView inputView);

    void destroySlot(RGB2YUVSlotResources& slot);

    // Cambia dimensioni / formato. Drain GPU + destroy + create.
    bool resizeSlot(RGB2YUVSlotResources& slot,
                    uint32_t width, uint32_t height,
                    RGB2YUVFormat format,
                    VkImageView inputView);

    // Registra il dispatch nel command buffer fornito.
    // PRECONDIZIONE: l'immagine `inputView` e' in VK_IMAGE_LAYOUT_GENERAL.
    // POSTCONDIZIONE: i buffer Y/U/V sono pronti per essere letti via host map
    //                 (barriera SHADER_WRITE -> HOST_READ inserita dalla funzione).
    void recordDispatch(VkCommandBuffer cmd, RGB2YUVSlotResources& slot);

    // Submit asincrono che firma uno dei semafori del ring.
    // Se waitSemaphore != VK_NULL_HANDLE, il submit aspetta quel semaforo
    // prima di partire (tipicamente lo slot.mixerDoneSemaphore firmato dal
    // submit grafico del renderer).
    /*
    void submitAsync(RGB2YUVSlotResources& slot,
                     VkSemaphore waitSemaphore = VK_NULL_HANDLE,
                     VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    */

    void submitAsync(RGB2YUVSlotResources& slot,
                 uint64_t              waitTimelineValue = 0,
                 VkPipelineStageFlags  waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // Cambia la VkImageView sorgente per il prossimo dispatch. Pensata per
    // chiamanti che usano N "master image" (una per frame in volo) e devono
    // far puntare lo slot a quella corretta ogni frame. submitAsync rifa'
    // l'update del descriptor binding 0 internamente prima del dispatch,
    // quindi e' sicuro chiamare questa funzione anche se il fence dello slot
    // e' ancora in volo: l'update effettivo avviene dopo il wait del fence.
    void setInputView(RGB2YUVSlotResources& slot, VkImageView newView);

    // Drena i semafori pendenti (analoga al YUV2RGB).
    static void drainPendingSemaphores(RGB2YUVSlotResources& slot,
                                       std::vector<VkSemaphore>& out);

    uint64_t reserveMixerTimelineValue(RGB2YUVSlotResources& slot);
    VkSemaphore getMixerTimelineSemaphore(const RGB2YUVSlotResources& slot) const;
private:


    bool createDescriptorLayout();
    bool createPipelines();
    VkShaderModule createNV12Shader();
    VkShaderModule createYUV420PShader();
    VkShaderModule loadShader(const char* glslSource, const char* name);
    VkShaderModule createShaderModule(const uint32_t* code, size_t sizeInBytes);

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool createBuffers(RGB2YUVSlotResources& slot);
    bool createSync(RGB2YUVSlotResources& slot);
    bool createDescriptors(RGB2YUVSlotResources& slot);
    void writeDescriptors(RGB2YUVSlotResources& slot);

    VulkanContext* m_ctx = nullptr;
    bool m_initialized = false;

    // Descriptor set layout condiviso tra le due pipeline:
    //   binding 0 = storage image RGBA (input)
    //   binding 1 = storage buffer Y       (output)
    //   binding 2 = storage buffer U/UV    (output)
    //   binding 3 = storage buffer V       (output, dummy in NV12)
    VkDescriptorSetLayout m_setLayout      = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;

    VkPipeline m_pipelineNV12    = VK_NULL_HANDLE;
    VkPipeline m_pipelineYUV420P = VK_NULL_HANDLE;

};

// Push constants letti dagli shader RGB2YUV.
struct RGB2YUVPushConstants {
    int strideY;        // byte per riga del piano Y
    int strideU;        // byte per riga del piano U (YUV420P) o UV (NV12)
    int strideV;        // byte per riga del piano V (YUV420P), 0 in NV12
    int rangeFull;      // 0 = TV (BT.601 limited), 1 = full
    int width;
    int height;
    int padding0;
    int padding1;
};

#endif // DEJAVIS_APP_RGB2YUV_PIPELINE_H
