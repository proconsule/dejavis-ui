#ifndef DEJAVIS_UI_IMAGE_VIEWER_H
#define DEJAVIS_UI_IMAGE_VIEWER_H

#include <condition_variable>
#include <queue>
#include <thread>

#include "video/render_globals.h"
#include "video/videofx_pipeline.h"
#include "logger.h"
#include <json/json.h>

#include "video/postprocess.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

namespace {
    struct MemReadCtx {
        const uint8_t* data;
        size_t         size;
        size_t         pos;
    };

    int memRead(void* opaque, uint8_t* buf, int buf_size) {
        auto* m = static_cast<MemReadCtx*>(opaque);
        size_t remaining = m->size - m->pos;
        if (remaining == 0) return AVERROR_EOF;
        int n = (int)((size_t)buf_size < remaining ? (size_t)buf_size : remaining);
        memcpy(buf, m->data + m->pos, n);
        m->pos += n;
        return n;
    }

    int64_t memSeek(void* opaque, int64_t offset, int whence) {
        auto* m = static_cast<MemReadCtx*>(opaque);
        switch (whence) {
            case SEEK_SET:     m->pos = (size_t)offset;            break;
            case SEEK_CUR:     m->pos += (size_t)offset;           break;
            case SEEK_END:     m->pos = (size_t)(m->size + offset); break;
            case AVSEEK_SIZE:  return (int64_t)m->size;
            default:           return -1;
        }
        return (int64_t)m->pos;
    }
} // namespace

class cimage_viewer {
public:
    cimage_viewer();
    ~cimage_viewer();
    void Init(VulkanContext * _ctx);
    void Vulkan_LoadTexture_FromMemory(VulkanUniTexture * texture,const unsigned char* img_data,int img_size,bool isHDR);

    bool CreateRGBAResources(VulkanUniTexture& outTexture,uint32_t _w,uint32_t _h);

    VkImageMemoryBarrier get_value1();

    Json::Value getJsonStatus();

    void RecordFXConversionCommand(VkCommandBuffer cmd,VulkanUniTexture& texture);

    void LoadFileAsync(const std::string &_path);

    void loaderThreadFn();

    bool LoadFile(const std::string &_path);

    VulkanUniTexture *m_texture;

    bool needUpdate = false;

    VkDescriptorSet getMixerDescriptorSet() const {
        return m_postProcessor->getOutputDescriptorSet();
    }

    void setKeyer(FxKeyerMode _keyer) {
        if (m_postProcessor) {

            m_postProcessor->setEnabled(true);
            m_postProcessor->setKeyerMode(_keyer);
            needUpdate = true;
        }
    }

    void setLumaKey(LumaKeyParams &myparams) {
        if (m_postProcessor) {
            m_postProcessor->setEnabled(true);
            m_postProcessor->setLumaParams(myparams);
            needUpdate = true;
        }
    }

    void setChromaKey(KeyerPushConstants &_mycroma) {
        if (m_postProcessor) {
            m_postProcessor->setEnabled(true);
            m_postProcessor->setChromaParams(_mycroma);
            needUpdate = true;
        }
    }

    void setColor(ColorParams &col) {
        if (m_postProcessor) {
            m_postProcessor->setEnabled(true);
            m_postProcessor->setColorParams(col);
            needUpdate = true;
        }
    }

    CPostProcessor* getPostProcessor() { return m_postProcessor.get(); }

    uint32_t getOutputWidth() const {
        return m_postProcessor->getWidth();
    }
    uint32_t getOutputHeight() const {
        return m_postProcessor->getHeight();
    }


private:


    std::unique_ptr<CPostProcessor> m_postProcessor;

    VulkanContext *m_ctx = nullptr;
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void Vulkan_CreateBuffer(VulkanContext* ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    VkCommandBuffer BeginSingleTimeCommands(VulkanContext* _ctx, QueueType type = QueueType::Graphics);
    void EndSingleTimeCommands(VulkanContext* _ctx, VkCommandBuffer commandBuffer, QueueType type = QueueType::Graphics);
    void TransitionImageLayout_SingleCMD(VulkanContext* ctx, VkImage image, VkFormat format,
                                  VkImageLayout oldLayout, VkImageLayout newLayout);
    void Vulkan_CopyBufferToImage(VulkanContext* ctx, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    void CleanupTexture(VulkanTexture& tex);

    AVFormatContext* m_fmt_ctx = nullptr;
    AVCodecContext* m_video_ctx = nullptr;

    int m_video_stream_idx = -1;

    std::string curr_filename;
    int img_width = 0;
    int img_height = 0;

    AVFrame* m_frame_sw = nullptr;

    std::thread              m_loader_thread;
    std::mutex               m_loader_mutex;
    std::condition_variable  m_loader_cv;
    std::queue<std::string>  m_loader_queue;
    std::atomic<bool>        m_loader_exit{false};
    std::atomic<bool>        m_isLoading{false};

};


#endif //DEJAVIS_UI_IMAGE_VIEWER_H