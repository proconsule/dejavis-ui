#pragma once
#include <shared_mutex>

#include "videofx_pipeline.h"
#include "yuv2rgb/YUV2RGBPipeline.h"
#include <Processing.NDI.Lib.h>
#include <vector>

class CPostProcessor {
public:
    CPostProcessor(VulkanContext* ctx);
    ~CPostProcessor();

    bool initYuvInput();
    bool initRgbaInput(const VulkanUniTexture& externalTex,
                       uint32_t w, uint32_t h);
    bool uploadYuvFrame(AVFrame* frame);
    bool uploadYuvFrame(const NDIlib_video_frame_v2_t& v);
    bool uploadYuvFrameVulkan(AVFrame* frame);

    bool notifyRgbaInputChanged(const VulkanUniTexture& newTex,
                                uint32_t w, uint32_t h);

    bool submit(VkSemaphore externalWait = VK_NULL_HANDLE);

    VkDescriptorSet getOutputDescriptorSet() const;
    void getWaitSemaphores(std::vector<VkSemaphore>& out);

    void processLifecycle(uint64_t currentFrameIdx);

    // === FX CONTROLS (thread-safe, parametri sopravvivono ai rebuild) ===
    void setEnabled(bool e);
    void setKeyerMode(FxKeyerMode);
    void setChromaParams(const KeyerPushConstants&);
    void setLumaParams  (const LumaKeyParams&);
    void setColorParams (const ColorParams&);

    void cleanup();


    bool setup(uint32_t w, uint32_t h,
           VkDescriptorSetLayout mixerLayout,
           VkImageView inputView,
           VkSampler inputSampler);

    uint32_t getWidth() const;

    uint32_t getHeight() const;

    bool getDimensions(uint32_t &w, uint32_t &h) const;

    struct ProcessingSlot {
        enum class InputKind { Yuv, ExternalRgba };

        uint32_t  width  = 0;
        uint32_t  height = 0;
        InputKind kind   = InputKind::Yuv;

        YUV2RGBSlotResources yuv;


        VkImageView externalView    = VK_NULL_HANDLE;
        VkSampler   externalSampler = VK_NULL_HANDLE;

        VideoFXSlotResources fx;

        bool valid = false;

        VkImageView fxInputView() const {
            return (kind == InputKind::Yuv) ? yuv.rgbaTexture.VkTexture.view
                                            : externalView;
        }
        VkSampler fxInputSampler() const {
            return (kind == InputKind::Yuv) ? yuv.rgbaTexture.VkTexture.sampler
                                            : externalSampler;
        }

        int yuvPixFmt = AV_PIX_FMT_NONE;
        VkDescriptorSet bypassMixerDescriptor = VK_NULL_HANDLE;
        VkDescriptorPool bypassPool           = VK_NULL_HANDLE;
    };



private:


    std::unique_ptr<ProcessingSlot> buildSlot(uint32_t w, uint32_t h,
                                          ProcessingSlot::InputKind kind,
                                          const VulkanUniTexture* externalRgba,
                                          int avPixFmt = AV_PIX_FMT_NONE);
    void retireSlot(ProcessingSlot* old);

    void destroySlotResources(ProcessingSlot& s);


    VulkanContext* m_ctx;
    VkDescriptorSetLayout m_mixerLayout;

    std::atomic<ProcessingSlot*> m_activeSlot{nullptr};

    struct RetiredSlot {
        std::unique_ptr<ProcessingSlot> slot;
        uint64_t retiredAtFrame;
    };

    std::mutex                m_retiredMutex;
    std::vector<RetiredSlot>  m_retiredSlots;
    std::atomic<uint64_t>     m_currentFrame{0};
    static constexpr uint64_t kDestroyDelayFrames = 3;

    std::atomic<bool>     m_enabled{false};
    std::atomic<bool>     m_shuttingDown{false};
    std::mutex            m_paramsMutex;
    FxKeyerMode           m_keyerMode = FxKeyerMode::Chroma;
    KeyerPushConstants    m_chromaParams;
    LumaKeyParams         m_lumaParams;
    ColorParams           m_colorParams;

};