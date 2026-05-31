#include "image_viewer.h"


cimage_viewer::cimage_viewer() {

}

cimage_viewer::~cimage_viewer() {
    if (!m_ctx) return;
    vkDeviceWaitIdle(m_ctx->device);

    if (m_texture) {
        CleanupTexture(m_texture->VkTexture);
    }
}

void cimage_viewer::Init(VulkanContext *_ctx) {
    m_ctx = _ctx;
    m_postProcessor = std::make_unique<CPostProcessor>(m_ctx);
    //m_postProcessor->initRgbaInput(m_texture);
}

bool cimage_viewer::CreateRGBAResources(VulkanUniTexture& outTexture,uint32_t _w,uint32_t _h)
{

    if (outTexture.VkTexture.image) {
        vkDeviceWaitIdle(m_ctx->device);
        CleanupTexture(outTexture.VkTexture);
    }
    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent        = { _w, _h, 1 };
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT          // compute scrive
                          | VK_IMAGE_USAGE_SAMPLED_BIT          // fragment legge
                          | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;    // per eventuale readback
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VK_CHECK(vkCreateImage(m_ctx->device, &imgInfo, nullptr, &outTexture.VkTexture.image));

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(m_ctx->device, outTexture.VkTexture.image, &memReq);
    outTexture.allocationSize = memReq.size;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);



    VK_CHECK(vkAllocateMemory(m_ctx->device, &allocInfo, nullptr, &outTexture.VkTexture.memory));
    VK_CHECK(vkBindImageMemory(m_ctx->device, outTexture.VkTexture.image,
                               outTexture.VkTexture.memory, 0));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image            = outTexture.VkTexture.image;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(m_ctx->device, &viewInfo, nullptr, &outTexture.VkTexture.view));

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter    = VK_FILTER_LINEAR;
    samplerInfo.minFilter    = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(m_ctx->device, &samplerInfo, nullptr, &outTexture.VkTexture.sampler));

    outTexture.VkTexture.width  = _w;
    outTexture.VkTexture.height = _h;


    DEJAVISUI_LOG_DEBUG("CreateYUVToRGBAResources: compute ds=%p sampler ds=%p",
        (void*)outTexture.VkTexture.descriptorSet,(void*)outTexture.VkTexture.sampler);

    return true;
}

VkImageMemoryBarrier cimage_viewer::get_value1() {
    VkImageMemoryBarrier barrier{};
    return barrier;
}

void cimage_viewer::Vulkan_LoadTexture_FromMemory(VulkanUniTexture* texture,
                                                  const unsigned char* img_data,
                                                  int img_size, bool isHDR) {
    m_texture = texture;

    if (!img_data || img_size <= 0) {
        DEJAVISUI_LOG_ERROR("Vulkan_LoadTexture_FromMemory: dati nulli");
        return;
    }

    uint8_t*         avioBuf    = nullptr;
    AVIOContext*     avioCtx    = nullptr;
    AVFormatContext* fmtCtx     = nullptr;
    AVCodecContext*  codecCtx   = nullptr;
    AVFrame*         frame      = nullptr;
    AVPacket*        pkt        = nullptr;
    SwsContext*      sws        = nullptr;
    uint8_t*         dstData[4] = { nullptr };
    int              dstLinesize[4] = { 0 };

    auto cleanupFF = [&]() {
        if (sws)      sws_freeContext(sws);
        if (dstData[0]) av_freep(&dstData[0]);
        if (frame)    av_frame_free(&frame);
        if (pkt)      av_packet_free(&pkt);
        if (codecCtx) avcodec_free_context(&codecCtx);
        if (fmtCtx)   avformat_close_input(&fmtCtx); // con CUSTOM_IO non tocca pb
        if (avioCtx) {                                // quindi liberiamo noi l'IO
            av_freep(&avioCtx->buffer);               // il buffer può essere stato realloc'ato
            avio_context_free(&avioCtx);
        }
    };

    const int kAvioBufSize = 1 << 12; // 4 KB
    MemReadCtx memCtx { img_data, (size_t)img_size, 0 };

    avioBuf = (uint8_t*)av_malloc(kAvioBufSize);
    if (!avioBuf) return;

    avioCtx = avio_alloc_context(avioBuf, kAvioBufSize, 0 /*write=no*/,
                                 &memCtx, &memRead, nullptr, &memSeek);
    if (!avioCtx) { av_free(avioBuf); return; }

    fmtCtx = avformat_alloc_context();
    if (!fmtCtx) { cleanupFF(); return; }
    fmtCtx->pb     = avioCtx;
    fmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

    if (avformat_open_input(&fmtCtx, nullptr, nullptr, nullptr) < 0) {
        DEJAVISUI_LOG_ERROR("avformat_open_input (memoria) fallito");
        cleanupFF();
        return;
    }
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) { cleanupFF(); return; }

    int streamIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIdx < 0) { cleanupFF(); return; }

    AVStream* st       = fmtCtx->streams[streamIdx];
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) { cleanupFF(); return; }

    codecCtx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(codecCtx, st->codecpar);
    if (avcodec_open2(codecCtx, dec, nullptr) < 0) { cleanupFF(); return; }

    DEJAVISUI_LOG_INFO("[IMAGE] codec=%s %dx%d pix_fmt=%s",
        avcodec_get_name(codecCtx->codec_id),
        codecCtx->width, codecCtx->height,
        av_get_pix_fmt_name(codecCtx->pix_fmt));

    frame = av_frame_alloc();
    pkt   = av_packet_alloc();

    bool gotFrame = false;
    while (!gotFrame && av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index == streamIdx) {
            if (avcodec_send_packet(codecCtx, pkt) >= 0 &&
                avcodec_receive_frame(codecCtx, frame) >= 0) {
                gotFrame = true;
            }
        }
        av_packet_unref(pkt);
    }
    if (!gotFrame) {
        avcodec_send_packet(codecCtx, nullptr);
        if (avcodec_receive_frame(codecCtx, frame) >= 0) gotFrame = true;
    }
    if (!gotFrame) { cleanupFF(); return; }

    int width  = frame->width;
    int height = frame->height;

    if (av_image_alloc(dstData, dstLinesize, width, height, AV_PIX_FMT_RGBA, 1) < 0) {
        cleanupFF(); return;
    }
    sws = sws_getContext(width, height, (AVPixelFormat)frame->format,
                         width, height, AV_PIX_FMT_RGBA,
                         SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) { cleanupFF(); return; }

    sws_scale(sws, frame->data, frame->linesize, 0, height, dstData, dstLinesize);

    void*        pixels    = dstData[0];
    VkDeviceSize imageSize = (VkDeviceSize)width * height * 4; // RGBA8

    img_width  = width;
    img_height = height;

    VkBuffer       stagingBuffer;
    VkDeviceMemory stagingMemory;
    Vulkan_CreateBuffer(m_ctx, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        stagingBuffer, stagingMemory);

    void* mapped;
    vkMapMemory(m_ctx->device, stagingMemory, 0, imageSize, 0, &mapped);
    memcpy(mapped, pixels, (size_t)imageSize);
    vkUnmapMemory(m_ctx->device, stagingMemory);

    CreateRGBAResources(*texture, width, height);

    TransitionImageLayout_SingleCMD(m_ctx, texture->VkTexture.image, VK_FORMAT_R8G8B8A8_UNORM,
                                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    Vulkan_CopyBufferToImage(m_ctx, stagingBuffer, texture->VkTexture.image, width, height);

    VkCommandBuffer transitionCmd = BeginSingleTimeCommands(m_ctx);
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT;
    barrier.image            = texture->VkTexture.image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(transitionCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndSingleTimeCommands(m_ctx, transitionCmd);

    m_postProcessor->initRgbaInput(*m_texture, width, height);
    m_postProcessor->setEnabled(true);
    m_postProcessor->submit();

    // ---- 7. Cleanup ----
    vkDestroyBuffer(m_ctx->device, stagingBuffer, nullptr);
    vkFreeMemory(m_ctx->device, stagingMemory, nullptr);
    cleanupFF();
}

VkCommandBuffer cimage_viewer::BeginSingleTimeCommands(VulkanContext* _ctx, QueueType type) {
    VkCommandPool pool;
    switch (type) {
        case QueueType::Compute:  pool = _ctx->computeCommandPool;  break;
        case QueueType::Transfer: pool = _ctx->transferCommandPool; break;
        default:                  pool = _ctx->graphicsCommandPool; break;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(_ctx->device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    return cmd;
}

void cimage_viewer::EndSingleTimeCommands(VulkanContext* _ctx, VkCommandBuffer cmd, QueueType type) {
    vkEndCommandBuffer(cmd);

    VkQueue queue;
    VkCommandPool pool;
    switch (type) {
        case QueueType::Compute:
            queue = _ctx->computeQueue;
            pool  = _ctx->computeCommandPool;
            break;
        case QueueType::Transfer:
            queue = _ctx->transferQueue;
            pool  = _ctx->transferCommandPool;
            break;
        default:
            queue = _ctx->graphicsQueue;
            pool  = _ctx->graphicsCommandPool;
            break;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(_ctx->device, pool, 1, &cmd);
}

void cimage_viewer::Vulkan_CreateBuffer(VulkanContext* ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(ctx->device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Fallita la creazione del buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(ctx->device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(ctx->device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Fallita l'allocazione della memoria del buffer!");
    }

    vkBindBufferMemory(ctx->device, buffer, bufferMemory, 0);
}

uint32_t cimage_viewer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_ctx->physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

void cimage_viewer::Vulkan_CopyBufferToImage(VulkanContext* ctx, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands(ctx);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    EndSingleTimeCommands(ctx, commandBuffer);
}

void cimage_viewer::TransitionImageLayout_SingleCMD(VulkanContext* ctx, VkImage image, VkFormat format,
                                  VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands(ctx);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
        // CASO 2: Da TRANSFER_DST a SHADER_READ (Dopo aver caricato i dati)
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
        // CASO 3: Da UNDEFINED a SHADER_READ (Usato per texture vuote o fallback)
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::invalid_argument("Layout transition not supported");
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    EndSingleTimeCommands(ctx, commandBuffer);
}

void cimage_viewer::CleanupTexture(VulkanTexture& tex) {

    vkDeviceWaitIdle(m_ctx->device);

    if (tex.descriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(m_ctx->device, m_ctx->descriptorPool, 1, &tex.descriptorSet);
    }

    if (tex.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_ctx->device, tex.sampler, nullptr);
    }

    if (tex.view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_ctx->device, tex.view, nullptr);
    }

    if (tex.image != VK_NULL_HANDLE) {
        vkDestroyImage(m_ctx->device, tex.image, nullptr);
    }

    if (tex.memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_ctx->device, tex.memory, nullptr);
    }
    tex = VulkanTexture{};
}


void cimage_viewer::LoadFileAsync(const std::string &_path) {
    {
        std::lock_guard<std::mutex> lk(m_loader_mutex);

        std::queue<std::string> empty;
        std::swap(m_loader_queue, empty);
        m_loader_queue.push(_path);
    }
    m_isLoading.store(true);
    m_loader_cv.notify_one();
}

void cimage_viewer::loaderThreadFn() {
    while (true) {
        std::string path;
        {
            std::unique_lock<std::mutex> lk(m_loader_mutex);
            m_loader_cv.wait(lk, [&]{
                return m_loader_exit.load() || !m_loader_queue.empty();
            });
            if (m_loader_exit.load()) return;
            path = std::move(m_loader_queue.front());
            m_loader_queue.pop();
        }


        bool ok = LoadFile(path);
        if (!ok) {
            DEJAVISUI_LOG_ERROR("[DECODER] LoadFileAsync fallito: %s", path.c_str());
        }
        m_isLoading.store(false);
    }
}

bool cimage_viewer::LoadFile(const std::string &_path) {

    if (m_fmt_ctx != nullptr) {
        DEJAVISUI_LOG_DEBUG("LoadFile: cleanup del file precedente");
        //cleanupCurrentFile();
    }



    if (avformat_find_stream_info(m_fmt_ctx, nullptr) < 0) {
        return false;
    }

    AVBufferRef* vk_device_ctx = nullptr;

    // --- Setup video ---
    m_video_stream_idx = av_find_best_stream(m_fmt_ctx, AVMEDIA_TYPE_VIDEO,
                                              -1, -1, nullptr, 0);
    if (m_video_stream_idx >= 0) {
        AVStream* v_stream = m_fmt_ctx->streams[m_video_stream_idx];
        const AVCodec* v_codec = avcodec_find_decoder(v_stream->codecpar->codec_id);

        if (v_codec) {
            m_video_ctx = avcodec_alloc_context3(v_codec);
            avcodec_parameters_to_context(m_video_ctx, v_stream->codecpar);



            if (avcodec_open2(m_video_ctx, v_codec, nullptr) >= 0) {
                m_frame_sw = av_frame_alloc();

            }
        }
    }

    DEJAVISUI_LOG_INFO("[IMAGE] : codec=%s profile=%d (%s) level=%d %dx%d pix_fmt=%s",
    avcodec_get_name(m_video_ctx->codec_id),
    m_video_ctx->profile,
    avcodec_profile_name(m_video_ctx->codec_id, m_video_ctx->profile),
    m_video_ctx->level,
    m_video_ctx->width, m_video_ctx->height,
    av_get_pix_fmt_name(m_video_ctx->pix_fmt));


    if (!m_video_ctx) return false;


    int w = m_video_ctx->width;
    int h = m_video_ctx->height;

    //ExtractMetadataFromContexts();

    return true;
}


Json::Value cimage_viewer::getJsonStatus() {

    Json::Value root;
    root["width"] = img_width;
    root["height"] = img_height;
    root["filename"] = curr_filename;
    return root;
}