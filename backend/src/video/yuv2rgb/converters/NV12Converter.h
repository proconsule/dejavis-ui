#ifndef DEJAVIS_APP_NV12_CONVERTER_H
#define DEJAVIS_APP_NV12_CONVERTER_H

#include "../YUVConverter.h"


class NV12Converter : public IYUVConverter {
public:
    const char* name() const override { return "NV12"; }
    bool supports(int fmt) const override { return fmt == AV_PIX_FMT_NV12; }

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
    bool recordUploadFromVulkanImage(VkCommandBuffer cmd, IConverterSlot* slot,
                                     VkImage srcImg, VkImageLayout srcLayout,
                                     VkAccessFlags srcAccess,
                                     uint32_t w, uint32_t h) override;

private:
    VulkanContext*        m_ctx            = nullptr;
    VkDescriptorSetLayout m_setLayout      = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_pipeline       = VK_NULL_HANDLE;
};

#endif