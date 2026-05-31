#ifndef DEJAVIS_APP_VIDEOFX_PIPELINE_H
#define DEJAVIS_APP_VIDEOFX_PIPELINE_H

#include <vulkan/vulkan.h>
#include <cstdint>
#include <atomic>
#include <mutex>
#include "render_globals.h"
#include "../logger.h"

// =============================================================================
//  Modalita' del primo stadio della FX chain
// =============================================================================
enum class FxKeyerMode : uint32_t {
    None    = 0,   // bypass: stage1 = passthrough dell'input
    Chroma  = 1,   // chroma key (color distance)
    Luma    = 2    // luma key (brightness threshold)
};

// =============================================================================
//  Push constants per il LUMA shader.
//  Layout lineare std430: 5 float consecutivi, 20 byte totali.
//  NON condivide layout col chroma — pipeline e push range separati.
// =============================================================================
struct LumaPushConstants {
    float lower    = 0.0f;
    float upper    = 1.0f;
    float invert   = 0.0f;
    float softness = 0.05f;
    float enabled  = 0.0f;
};
static_assert(sizeof(LumaPushConstants) == 20, "LumaPushConstants must be 20 bytes");

// =============================================================================
//  Parametri "user-friendly" del luma key (uguale a LumaPushConstants ma
//  separato semanticamente: questo e' l'API utente, l'altra e' l'ABI shader).
// =============================================================================
struct LumaKeyParams {
    float lower    = 0.0f;
    float upper    = 1.0f;
    float invert   = 0.0f;
    float softness = 0.05f;
    float enabled  = 0.0f;
};

// =============================================================================
//  Slot resources — Modello A (catena fissa: keyer -> color -> mixer)
// =============================================================================
struct VideoFXSlotResources {
    // Stage 1: output del keyer (chroma o luma), input del color
    VkImage         stage1Image  = VK_NULL_HANDLE;
    VkDeviceMemory  stage1Memory = VK_NULL_HANDLE;
    VkImageView     stage1View   = VK_NULL_HANDLE;
    VkImageLayout   stage1Layout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Final: output del color, mostrato al mixer
    VkImage         finalImage   = VK_NULL_HANDLE;
    VkDeviceMemory  finalMemory  = VK_NULL_HANDLE;
    VkImageView     finalView    = VK_NULL_HANDLE;
    VkSampler       finalSampler = VK_NULL_HANDLE;
    VkImageLayout   finalLayout  = VK_IMAGE_LAYOUT_UNDEFINED;

    // Descriptor set per il mixer (sampler + final view)
    VkDescriptorSet finalMixerDescriptorSet = VK_NULL_HANDLE;

    // Descriptor set delle 2 passate compute
    // chromaDescriptorSet serve sia per chroma che per luma (stesso descriptor
    // set layout: sampler + storage image). Cambia solo il pipeline layout.
    VkDescriptorSet chromaDescriptorSet = VK_NULL_HANDLE; // keyer stage (chroma+luma)
    VkDescriptorSet colorDescriptorSet  = VK_NULL_HANDLE;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    // Stato del primo stadio + parametri di entrambi i keyer
    std::atomic<FxKeyerMode> keyerMode{FxKeyerMode::Chroma};
    KeyerPushConstants chromaParams; // 32 byte (vedi render_globals.h)
    LumaKeyParams      lumaParams;   // 20 byte logici, packati in LumaPushConstants
    ColorParams        colorParams;

    uint32_t width = 0, height = 0;
    bool     valid = false;

    VkFence     fence             = VK_NULL_HANDLE;
    VkSemaphore signalSemaphore   = VK_NULL_HANDLE; // Segnala al Mixer che FX è pronto

    // Per gestire i semafori pendenti (come in RGB2YUV)
    std::vector<VkSemaphore> pendingSemaphores;
    std::mutex               pendingSemMutex;

    VkCommandBuffer commandBuffer;

    mutable std::mutex paramsMutex;

    void reset() {
        stage1Image  = VK_NULL_HANDLE;
        stage1Memory = VK_NULL_HANDLE;
        stage1View   = VK_NULL_HANDLE;
        stage1Layout = VK_IMAGE_LAYOUT_UNDEFINED;

        finalImage   = VK_NULL_HANDLE;
        finalMemory  = VK_NULL_HANDLE;
        finalView    = VK_NULL_HANDLE;
        finalSampler = VK_NULL_HANDLE;
        finalLayout  = VK_IMAGE_LAYOUT_UNDEFINED;

        finalMixerDescriptorSet = VK_NULL_HANDLE;
        chromaDescriptorSet     = VK_NULL_HANDLE;
        colorDescriptorSet      = VK_NULL_HANDLE;
        descriptorPool          = VK_NULL_HANDLE;

        keyerMode.store(FxKeyerMode::Chroma, std::memory_order_relaxed);
        chromaParams = KeyerPushConstants{};
        lumaParams   = LumaKeyParams{};
        colorParams  = ColorParams{};

        width  = 0;
        height = 0;
        valid  = false;



    }
};

class VideoFXPipeline {
public:
    static VideoFXPipeline& instance() {
        static VideoFXPipeline inst;
        return inst;
    }

    bool init(VulkanContext* ctx);
    void shutdown();
    bool isInitialized() const { return m_initialized; }

    // Crea slot di FX. La risoluzione output = risoluzione input (no scaling).
    bool createSlot(VideoFXSlotResources& slot,
                    uint32_t width, uint32_t height,
                    VkImageView srcView, VkSampler srcSampler,
                    VkDescriptorSetLayout mixerSamplerLayout);

    void destroySlot(VideoFXSlotResources& slot);

    void setChromaKeyParams(VideoFXSlotResources& slot, const KeyerPushConstants & p) {
        slot.chromaParams = p;
    }
    void setLumaKeyParams(VideoFXSlotResources& slot, const LumaKeyParams& p) {
        std::lock_guard<std::mutex> lk(slot.paramsMutex);
        slot.lumaParams = p;
    }
    void setColorParams(VideoFXSlotResources& slot, const ColorParams& p) {
        std::lock_guard<std::mutex> lk(slot.paramsMutex);
        slot.colorParams = p;
    }
    void setKeyerMode(VideoFXSlotResources& slot, FxKeyerMode m) {
        std::lock_guard<std::mutex> lk(slot.paramsMutex);
        slot.keyerMode.store(m, std::memory_order_release);
        DEJAVISUI_LOG_DEBUG("CHANGED FX MODE TO %d",m);
    }
    FxKeyerMode getKeyerMode(const VideoFXSlotResources& slot) const {
        return slot.keyerMode.load(std::memory_order_acquire);
    }

    void recordDispatch(VkCommandBuffer cmd, VideoFXSlotResources& slot);

    void submitAsync(VideoFXSlotResources& slot,
                                  VkImageView srcView, VkSampler srcSampler,
                                  VkSemaphore waitSemaphore = VK_NULL_HANDLE);

    // Per recuperare i semafori da dare al Mixer
    static void drainPendingSemaphores(VideoFXSlotResources& slot, std::vector<VkSemaphore>& out);


    void updateDescriptors(VideoFXSlotResources& slot, VkImageView srcView, VkSampler srcSampler);



private:
    bool createSync(VideoFXSlotResources& slot);
    VideoFXPipeline() = default;
    ~VideoFXPipeline() = default;
    VideoFXPipeline(const VideoFXPipeline&) = delete;
    VideoFXPipeline& operator=(const VideoFXPipeline&) = delete;

    bool createDescriptorLayouts();
    bool createPipelines();
    VkShaderModule createChromaShader();
    VkShaderModule createLumaShader();
    VkShaderModule createColorShader();
    VkShaderModule loadShader(const char* glslSource, const char* name);
    VkShaderModule CreateShaderModule(const uint32_t* code, size_t sizeInBytes);
    bool createTargetImage(uint32_t w, uint32_t h, VkImageUsageFlags usage,
                           VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView);


    VulkanContext* m_ctx = nullptr;
    bool m_initialized = false;

    // Layout keyer:  sampler2D + storage image  (condiviso tra chroma e luma
    // come DESCRIPTOR set layout — cambia solo il pipeline layout per via dei
    // push constants di dimensioni diverse)
    VkDescriptorSetLayout m_chromaLayout = VK_NULL_HANDLE;
    // Layout color:   storage image (read) + storage image (write)
    VkDescriptorSetLayout m_colorLayout  = VK_NULL_HANDLE;

    // Tre pipeline layout separati: chroma (32 byte push), luma (20 byte push),
    // color (32 byte push). Cosi' niente "abuso" di layout condiviso che
    // produrrebbe mismatch std430 tra C++ e shader.
    VkPipelineLayout m_chromaPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_lumaPipelineLayout   = VK_NULL_HANDLE;
    VkPipelineLayout m_colorPipelineLayout  = VK_NULL_HANDLE;

    VkPipeline m_chromaPipeline = VK_NULL_HANDLE;
    VkPipeline m_lumaPipeline   = VK_NULL_HANDLE;
    VkPipeline m_colorPipeline  = VK_NULL_HANDLE;
    VkCommandPool m_computeCommandPool = VK_NULL_HANDLE;

    void updateDescriptorsInternal(VideoFXSlotResources &slot, VkImageView srcView, VkSampler srcSampler);

};

#endif // DEJAVIS_APP_VIDEOFX_PIPELINE_HH