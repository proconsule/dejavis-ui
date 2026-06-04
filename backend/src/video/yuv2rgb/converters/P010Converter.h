#ifndef DEJAVIS_APP_P010_CONVERTER_H
#define DEJAVIS_APP_P010_CONVERTER_H

#include "../YUVConverter.h"

class P010Converter : public IYUVConverter {
public:
    const char* name() const override { return "P010LE"; }
    bool supports(int fmt) const override { return fmt == AV_PIX_FMT_P010LE; }

    bool init(VulkanContext* ctx) override;
    void shutdown() override;

    std::unique_ptr<IConverterSlot>
        createSlot(uint32_t w, uint32_t h, const OutputBinding& out) override;
    void destroySlot(IConverterSlot* slot) override;

    bool uploadFrame(IConverterSlot* slot, AVFrame* f) override;
    bool uploadNDIFrame(IConverterSlot* slot, const NDIlib_video_frame_v2_t& v) override;

    bool recordUploadFromVulkanImage(VkCommandBuffer cmd, IConverterSlot *slot, VkImage srcImg, VkImageLayout srcLayout,
                                     VkAccessFlags srcAccess, uint32_t w, uint32_t h) override;

    void recordDispatch(VkCommandBuffer cmd, IConverterSlot* slot,
                        const FrameMetadata& meta,
                        VkImageLayout& outCurrentLayout) override;

private:
    VulkanContext*        m_ctx            = nullptr;
    VkDescriptorSetLayout m_setLayout      = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_pipeline       = VK_NULL_HANDLE;
};

#endif