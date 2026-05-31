#ifndef DEJAVIS_APP_YUV2RGB_PIPELINE_H
#define DEJAVIS_APP_YUV2RGB_PIPELINE_H

#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "../render_globals.h"
#include "YUVConverter.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

// =============================================================================
//  YUV2RGBSlotResources
//  Slot pubblico, mantiene l'output condiviso + risorse converter-specifiche.
// =============================================================================
struct YUV2RGBSlotResources {
    // --- Output: texture RGBA + descriptor mixer ---
    VulkanUniTexture rgbaTexture{};
    VkDescriptorSet  mixerDescriptorSet = VK_NULL_HANDLE;
    VkImageLayout    rgbaLayout         = VK_IMAGE_LAYOUT_UNDEFINED;

    // --- Identita' dello slot ---
    int      avPixFmt = -1;
    uint32_t width    = 0;
    uint32_t height   = 0;
    bool     valid    = false;

    // --- Risorse del converter (opache) ---
    IYUVConverter*                  converter = nullptr;  // non-owning
    std::unique_ptr<IConverterSlot> converterSlot;

    // --- Metadati di frame ---
    int    rangeFull = 0;      // 0 = BT.601 limited, 1 = full
    int    colorSpace = 1;
    double pts       = 0.0;

    bool            asyncEnabled = false;
    VkCommandPool   cmdPool      = VK_NULL_HANDLE;
    VkCommandBuffer cmd          = VK_NULL_HANDLE;
    VkFence         fence        = VK_NULL_HANDLE;
    std::mutex      submitMutex;

    // Ring di semafori binari. Il submit firma sem[writeIdx], poi ruota.
    // Il renderer chiama drainPendingSemaphores() per ottenere tutti gli
    // handle firmati e non ancora attesi.
    static constexpr int kSemRing = 3;
    VkSemaphore semRing[kSemRing] = {};
    uint32_t    semWriteIdx       = 0;

    std::mutex                pendingSemMutex;
    std::vector<VkSemaphore>  pendingSemaphores;

    // Handle "corrente" — alias del prossimo semaforo che verra' firmato.
    // Utile per codice legacy che leggeva getComputeFinishedSemaphore().
    VkSemaphore computeFinished = VK_NULL_HANDLE;
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

    void setMixerDescriptorLayout(VkDescriptorSetLayout l) { m_mixerLayout = l; }

    void registerConverter(std::unique_ptr<IYUVConverter> c);

    bool createSlot(YUV2RGBSlotResources& slot,
                    int avPixFmt, uint32_t w, uint32_t h,bool asyncEnabled = false);

    void destroySlot(YUV2RGBSlotResources& slot);

    bool submitAsync(YUV2RGBSlotResources& slot);

    static void drainPendingSemaphores(YUV2RGBSlotResources& slot,
                                       std::vector<VkSemaphore>& out);


    bool resizeSlot(YUV2RGBSlotResources& slot,
                    int avPixFmt, uint32_t w, uint32_t h);

    bool uploadFrame(YUV2RGBSlotResources& slot, AVFrame* f);
    bool uploadNDIFrame(YUV2RGBSlotResources& slot, const NDIlib_video_frame_v2_t& v);
    void recordDispatch(VkCommandBuffer cmd, YUV2RGBSlotResources& slot);

private:
    YUV2RGBPipeline() = default;
    ~YUV2RGBPipeline() = default;
    YUV2RGBPipeline(const YUV2RGBPipeline&) = delete;
    YUV2RGBPipeline& operator=(const YUV2RGBPipeline&) = delete;

    IYUVConverter* findConverter(int avPixFmt) const;

    bool createOutputTexture(YUV2RGBSlotResources& slot, uint32_t w, uint32_t h);
    void destroyOutputTexture(YUV2RGBSlotResources& slot);
    bool createMixerDescriptorSet(YUV2RGBSlotResources& slot);

    VulkanContext* m_ctx          = nullptr;
    bool           m_initialized  = false;
    VkDescriptorSetLayout m_mixerLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_mixerPool   = VK_NULL_HANDLE;

    std::vector<std::unique_ptr<IYUVConverter>> m_converters;
    std::mutex m_registryMutex;

    bool createSyncResources(YUV2RGBSlotResources& slot);
    void destroySyncResources(YUV2RGBSlotResources& slot);

    VkQueue  m_computeQueue       = VK_NULL_HANDLE;
    uint32_t m_computeQueueFamily = 0;
};

#endif // DEJAVIS_APP_YUV2RGB_PIPELINE_H