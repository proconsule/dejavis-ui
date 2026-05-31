#ifndef DEJAVIS_APP_UYVY_CONVERTER_H
#define DEJAVIS_APP_UYVY_CONVERTER_H

#include "../YUVConverter.h"

// UYVY 4:2:2 packed. Single plane: U0 Y0 V0 Y1 (2 byte per pixel).
class UYVYConverter : public IYUVConverter {
public:
    const char* name() const override { return "UYVY422"; }
    bool supports(int fmt) const override { return fmt == AV_PIX_FMT_UYVY422; }

    bool init(VulkanContext* ctx) override;
    void shutdown() override;

    std::unique_ptr<IConverterSlot>
        createSlot(uint32_t w, uint32_t h, const OutputBinding& out) override;
    void destroySlot(IConverterSlot* slot) override;

    bool uploadFrame(IConverterSlot* slot, AVFrame* f) override;
    bool uploadNDIFrame(IConverterSlot* slot, const NDIlib_video_frame_v2_t& v) override;

    void recordDispatch(VkCommandBuffer cmd, IConverterSlot* slot,
                                   const FrameMetadata& meta,
                                   VkImageLayout& outCurrentLayout) override;

private:
    VulkanContext*        m_ctx            = nullptr;
    VkDescriptorSetLayout m_setLayout      = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_pipeline       = VK_NULL_HANDLE;
};

#endif // DEJAVIS_APP_UYVY_CONVERTER_H