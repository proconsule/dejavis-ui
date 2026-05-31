#ifndef DEJAVIS_APP_YUV420P_CONVERTER_H
#define DEJAVIS_APP_YUV420P_CONVERTER_H

#include "../YUVConverter.h"

// YUV420P / I420: 3 plane (Y, U, V), 4:2:0 planar.
// NDI YV12 (che ha layout YVU rovesciato) NON e' supportato qui: chi instrada
// dovrebbe gia' aver mappato YV12 -> YUV420P invertendo data[1]/data[2].
class YUV420PConverter : public IYUVConverter {
public:
    const char* name() const override { return "YUV420P"; }
    bool supports(int fmt) const override { return fmt == AV_PIX_FMT_YUV420P; }

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

#endif