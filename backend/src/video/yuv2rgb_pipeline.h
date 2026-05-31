#ifndef DEJAVIS_APP_YUV2RGB_PIPELINE_H
#define DEJAVIS_APP_YUV2RGB_PIPELINE_H

#include <vulkan/vulkan.h>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <vector>

#include "render_globals.h"
#include "videofx_pipeline.h"   // per VideoFXSlotResources opzionale in submitAsync

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/hwcontext.h>
}


// =============================================================================
//  Slot resources — ogni decoder ne possiede uno
// =============================================================================
struct YUV2RGBSlotResources {
    // --- Output: texture RGBA pronta per FX/mixer ---
    VulkanUniTexture rgbaTexture{};
    VkDescriptorSet  mixerDescriptorSet = VK_NULL_HANDLE; // sampler set per il mixer

    // --- Buffer host-visible Y / U / V (path software) ---
    VkBuffer       buffers[3]        = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory bufferMemories[3] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    void*          mappedPtrs[3]     = { nullptr, nullptr, nullptr };
    VkDeviceSize   bufferSizes[3]    = { 0, 0, 0 };
    int            strides[3]        = { 0, 0, 0 };

    // --- Descriptor pool/set ---
    VkDescriptorPool descriptorPool   = VK_NULL_HANDLE;
    VkDescriptorSet  computeDescriptorSet = VK_NULL_HANDLE;

    // --- Sync (per istanza) ---
    VkCommandBuffer cmd               = VK_NULL_HANDLE;
    VkFence         fence             = VK_NULL_HANDLE;
    std::mutex      submitMutex;

    // --- Ring di semafori per evitare race "signaled by queue X, not yet waited on" ---
    // Il decoder firma sem[writeIdx] ad ogni submit, poi ruota.
    // Il renderer drena pendingSemaphores ad ogni frame e mette tutto in pWaitSemaphores.
    // Cosi' ogni binary semaphore viene firmato una sola volta e atteso una sola volta.
    static constexpr int kSemRing = 3;
    VkSemaphore semRing[kSemRing] = {};
    uint32_t    semWriteIdx = 0;

    std::mutex                pendingSemMutex;
    std::vector<VkSemaphore>  pendingSemaphores;

    // Compatibilita': punta sempre al "prossimo" semaforo che verra' firmato.
    // Cosi' chi chiama getComputeFinishedSemaphore() vede comunque un handle valido,
    // anche se in realta' ad ogni frame il signal cambia handle nel ring.
    VkSemaphore computeFinished = VK_NULL_HANDLE;

    // Vecchi flag — non piu' usati per la sync (il ring li rende inutili) ma li
    // teniamo per non rompere chiamanti esistenti che li leggono come "c'e' lavoro
    // pendente". Internamente impostiamo submitted=true al primo submit e basta.
    std::atomic<bool> submitted{false};
    std::atomic<bool> semaphoreSignaled{false};

    // --- Stato runtime ---
    uint32_t width     = 0;
    uint32_t height    = 0;
    bool     isNV12    = false;
    bool     isUYVY    = false;
    int      rangeFull = 0;        // 0 = TV (limited), 1 = full
    double   pts       = 0.0;
    bool     valid     = false;
};

class YUV2RGBPipeline {
public:
    static YUV2RGBPipeline& instance() {
        static YUV2RGBPipeline inst;
        return inst;
    }

    bool init(VulkanContext* ctx);
    void shutdown();
    bool isInitialized() const { return m_initialized; }

    // Crea uno slot dimensionato per width x height. Il `mixerSamplerLayout`
    // e' il set layout che il mixer/render usa per campionare la texture finale.
    bool createSlot(YUV2RGBSlotResources& slot,
                    uint32_t width, uint32_t height,
                    VkDescriptorSetLayout mixerSamplerLayout);

    void destroySlot(YUV2RGBSlotResources& slot);


    // Cambia le dimensioni di uno slot esistente. Internamente fa drain GPU
    // (vkDeviceWaitIdle) e poi destroy + create. No-op se le dimensioni non
    // cambiano. Se lo slot non e' ancora valido, equivale a createSlot.
    // ATTENZIONE: dopo il resize, view/sampler/mixerDescriptorSet sono nuovi
    // handle. Chi tiene riferimenti (es. uno slot FX agganciato alla rgbaTexture
    // o un descriptor cachato nel renderer) deve essere ricreato/riletto.
    bool resizeSlot(YUV2RGBSlotResources& slot,
                    uint32_t width, uint32_t height,
                    VkDescriptorSetLayout mixerSamplerLayout);

    // Carica un AVFrame software (NV12, YUV420P, P010LE) nei buffer dello slot.
    // Aggiorna stride/isNV12 e fa l'update del descriptor set se lo slot non
    // era ancora stato scritto (idempotente nei frame successivi finche' i
    // buffer non cambiano handle).
    // Ritorna false se il formato non e' supportato.
    bool uploadSoftwareFrame(YUV2RGBSlotResources& slot, AVFrame* swFrame);
    bool uploadNDIFrame(YUV2RGBSlotResources& slot, const NDIlib_video_frame_v2_t& v);

    bool uploadHardwareFrame(YUV2RGBSlotResources& slot, AVFrame* frame);

    // Registra il dispatch nel command buffer fornito.
    // PRECONDIZIONE: uploadSoftwareFrame chiamato in questo frame.
    // POSTCONDIZIONE: slot.rgbaTexture in SHADER_READ_ONLY_OPTIMAL,
    //                 con dstStage = COMPUTE_SHADER_BIT (compatibile sia con
    //                 una FX compute successiva, sia con un acquire grafico
    //                 dopo wait sul semaforo).
    void recordDispatch(VkCommandBuffer cmd, YUV2RGBSlotResources& slot);

    // Submit asincrono che firma uno dei semafori del ring. Gestisce wait/reset
    // del fence internamente. Usa il command buffer interno dello slot.
    //
    // Se `fxSlot` e' non-null e valido, dopo aver registrato il dispatch YUV->RGB
    // viene anche registrato il dispatch FX nello stesso command buffer. Cosi':
    //   - una sola sottomissione sulla coda compute
    //   - un solo semaforo del ring da aspettare lato graphics
    //   - finalImage della FX gia' pronta in SHADER_READ_ONLY_OPTIMAL al wait
    void submitAsync(YUV2RGBSlotResources& slot,
                     VideoFXSlotResources* fxSlot = nullptr);

    // Drena la lista dei semafori firmati e non ancora consumati. Va chiamata
    // dal renderer una volta per frame: i semafori vanno aggiunti come wait
    // del submit grafico. Dopo la drain, lo slot non ha piu' semafori "in volo"
    // dal punto di vista del producer.
    static void drainPendingSemaphores(YUV2RGBSlotResources& slot,
                                       std::vector<VkSemaphore>& out);

private:
    YUV2RGBPipeline() = default;
    ~YUV2RGBPipeline() = default;
    YUV2RGBPipeline(const YUV2RGBPipeline&) = delete;
    YUV2RGBPipeline& operator=(const YUV2RGBPipeline&) = delete;

    bool createDescriptorLayout();
    bool createPipeline();
    VkShaderModule createShader();
    VkShaderModule loadShader(const char* glslSource, const char* name);
    VkShaderModule createShaderModule(const uint32_t* code, size_t sizeInBytes);

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool createOutputTexture(YUV2RGBSlotResources& slot, uint32_t w, uint32_t h);
    bool createBuffers(YUV2RGBSlotResources& slot, uint32_t w, uint32_t h);
    bool createSync(YUV2RGBSlotResources& slot);
    bool createDescriptors(YUV2RGBSlotResources& slot,
                           VkDescriptorSetLayout mixerSamplerLayout);
    void writeDescriptors(YUV2RGBSlotResources& slot);

    VulkanContext* m_ctx = nullptr;
    bool m_initialized = false;

    // binding 0 = storage image (output RGBA)
    // binding 1 = storage buffer Y         (o UYVY packed se isUYVY=1)
    // binding 2 = storage buffer U         (o UV per NV12; dummy per UYVY)
    // binding 3 = storage buffer V         (dummy per NV12/UYVY)
    VkDescriptorSetLayout m_setLayout      = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_pipeline       = VK_NULL_HANDLE;
};

// Push constants letti dallo shader. Stessa struttura che usavi nel decoder.
struct YUV2RGBPushConstants {
    int strideY;
    int strideU;
    int strideV;
    int isNV12;
    int rangeFull;
    int width;
    int height;
    int isUYVY;
};

#endif // DEJAVIS_APP_YUV2RGB_PIPELINE_H