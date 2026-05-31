#include "ndi_receiver.h"
#include "logger.h"
#include <cstring>
#include "video/vulkan_utils.h"

CNDIReceiver::CNDIReceiver() {
    if (!NDIlib_initialize()) {
        DEJAVISUI_LOG_ERROR("[NDI] NDIlib_initialize fallita");
    }
}

CNDIReceiver::~CNDIReceiver() {
    Stop();                                  // join recv thread + cleanup NDI/YUV/resampler

    m_stopFinder = true;
    if (m_findThread.joinable()) m_findThread.join();

    if (m_ctx) {
        vkDeviceWaitIdle(m_ctx->device);     // GPU non sta più usando nulla

        destroyStaging();                    // mancante prima

        std::lock_guard<std::mutex> lock(m_destructionMutex);
        for (auto& pd : m_destructionQueue) {
            if (pd.sampler) vkDestroySampler(m_ctx->device, pd.sampler, nullptr);
            if (pd.view)    vkDestroyImageView(m_ctx->device, pd.view, nullptr);
            if (pd.image)   vkDestroyImage(m_ctx->device, pd.image, nullptr);
            if (pd.memory)  vkFreeMemory(m_ctx->device, pd.memory, nullptr);
        }
        m_destructionQueue.clear();
    }

    NDIlib_destroy();
}

void CNDIReceiver::Init_VideoAudio(VulkanContext* ctx,VulkanUniTexture *_tex,
                        MultiChannelRingBuffer* audioBuf,
                        int targetSampleRate,
                        int targetChannels) {
    m_ctx        = ctx;
    m_texture = _tex;
    m_audio_ring_planar = audioBuf;
    m_targetSR   = targetSampleRate;
    m_targetCh   = targetChannels;
    m_postProcessor = std::make_unique<CPostProcessor>(m_ctx);
    audioonly = false;
    if (!YUV2RGBPipeline::instance().isInitialized()) {
        if (!YUV2RGBPipeline::instance().init(m_ctx)) {
            DEJAVISUI_LOG_ERROR("[NDI] YUV2RGBPipeline::init fallita");
        }
    }
    StartFinder();
}

void CNDIReceiver::Init_AudioOnly(MultiChannelRingBuffer* audioBuf,
                        int targetSampleRate,
                        int targetChannels) {
    m_audio_ring_planar = audioBuf;
    m_targetSR   = targetSampleRate;
    m_targetCh   = targetChannels;
    audioonly    = true;
    StartFinder();
}

void CNDIReceiver::StartFinder() {
    m_stopFinder = false;
    m_findThread = std::thread(&CNDIReceiver::finderThreadLoop, this);
}

void CNDIReceiver::destroyVulkanTextureInternal() {
    if (!m_ctx || !m_texture) return;

    std::lock_guard<std::mutex> lock(m_destructionMutex);
    PendingDestroy pd;
    pd.frameIndex = m_currentFrameIndex;
    pd.image = m_texture->VkTexture.image;
    pd.view = m_texture->VkTexture.view;
    pd.sampler = m_texture->VkTexture.sampler;
    pd.memory = m_texture->VkTexture.memory;

    m_destructionQueue.push_back(pd);

    m_texture->VkTexture.image = VK_NULL_HANDLE;
    m_texture->VkTexture.view = VK_NULL_HANDLE;
    m_texture->VkTexture.sampler = VK_NULL_HANDLE;
    m_texture->VkTexture.memory = VK_NULL_HANDLE;

    m_textureVersion.fetch_add(1, std::memory_order_release);
}

void CNDIReceiver::destroyVulkanTexture() {
    if (!m_ctx || !m_texture) return;


    std::lock_guard<std::mutex> lock(m_destructionMutex);
    std::lock_guard<std::mutex> texLock(m_textureMutex);

    PendingDestroy pd;
    pd.frameIndex = m_currentFrameIndex;
    pd.image = m_texture->VkTexture.image;
    pd.view = m_texture->VkTexture.view;
    pd.sampler = m_texture->VkTexture.sampler;
    pd.memory = m_texture->VkTexture.memory;

    m_destructionQueue.push_back(pd);

    m_texture->VkTexture.image = VK_NULL_HANDLE;
    m_texture->VkTexture.view = VK_NULL_HANDLE;
    m_texture->VkTexture.sampler = VK_NULL_HANDLE;
    m_texture->VkTexture.memory = VK_NULL_HANDLE;

    m_textureVersion.fetch_add(1, std::memory_order_release);
}

void CNDIReceiver::recreateVulkanTexture(uint32_t width, uint32_t height) {

    std::lock_guard<std::mutex> texLock(m_textureMutex);

    destroyVulkanTextureInternal();


    DEJAVISUI_LOG_INFO("[NDI] Creazione texture interna: %ux%u", width, height);

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM; // Formato nativo NDI (BGRA)
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(m_ctx->device, &imageInfo, nullptr, &m_texture->VkTexture.image) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[NDI] Impossibile creare VkImage");
        return;
    }


    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_ctx->device, m_texture->VkTexture.image, &memReqs);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(m_ctx, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_ctx->device, &allocInfo, nullptr, &m_texture->VkTexture.memory) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[NDI] Impossibile allocare memoria texture");
        return;
    }
    vkBindImageMemory(m_ctx->device, m_texture->VkTexture.image, m_texture->VkTexture.memory, 0);


    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_texture->VkTexture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_ctx->device, &viewInfo, nullptr, &m_texture->VkTexture.view) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[NDI] Impossibile creare VkImageView");
        return;
    }

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(m_ctx->device, &samplerInfo, nullptr, &m_texture->VkTexture.sampler) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[NDI] Impossibile creare Sampler");
    }

    if (m_texture->VkTexture.descriptorSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo dsAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dsAllocInfo.descriptorPool = m_ctx->descriptorPool; // Assicurati di averlo passato nell'Init
        dsAllocInfo.descriptorSetCount = 1;
        dsAllocInfo.pSetLayouts = &m_ctx->m_mixerDescriptorLayout; // Assicurati di averlo passato nell'Init

        if (vkAllocateDescriptorSets(m_ctx->device, &dsAllocInfo, &m_texture->VkTexture.descriptorSet) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[NDI] Errore allocazione Descriptor Set");
        }
    }

    VkDescriptorImageInfo imageDescriptor{};
    imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageDescriptor.imageView = m_texture->VkTexture.view;
    imageDescriptor.sampler = m_texture->VkTexture.sampler;

    VkWriteDescriptorSet descriptorWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    descriptorWrite.dstSet = m_texture->VkTexture.descriptorSet;
    descriptorWrite.dstBinding = 0; // Il binding definito nel tuo shader (es: layout(binding = 0) uniform sampler2D...)
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageDescriptor;

    vkUpdateDescriptorSets(m_ctx->device, 1, &descriptorWrite, 0, nullptr);

    m_texture->VkTexture.width = width;
    m_texture->VkTexture.height = height;
    m_texture->VkTexture.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    m_textureVersion.fetch_add(1, std::memory_order_release);

    DEJAVISUI_LOG_DEBUG("[NDI] Texture Creata", width, height);

}

Json::Value CNDIReceiver::listSources() {

    Json::Value result;
    std::vector<NDISourceInfo> tmpsource = getAvailableSources();
    for (uint32_t i = 0; i < tmpsource.size(); i++) {
        Json::Value src;
        src["name"]    = tmpsource[i].name;
        src["address"] = tmpsource[i].url_address;
        result.append(src);
    }
     return result;
}

std::vector<NDISourceInfo> CNDIReceiver::getAvailableSources() {
    std::lock_guard<std::mutex> lock(m_sourcesMutex);
    return m_discoveredSources;
}

void CNDIReceiver::finderThreadLoop() {
    NDIlib_find_instance_t finder = NDIlib_find_create_v2();
    if (!finder) return;

    std::vector<NDISourceInternal> activeSources;

    while (!m_stopFinder) {
        // Aspetta sorgenti per 1000ms
        NDIlib_find_wait_for_sources(finder, 1000);

        uint32_t count = 0;
        const NDIlib_source_t* currentSources = NDIlib_find_get_current_sources(finder, &count);

        for (auto& s : activeSources) s.keepAlive--;

        for (uint32_t i = 0; i < count; i++) {
            std::string name = currentSources[i].p_ndi_name ? currentSources[i].p_ndi_name : "";

            auto it = std::find_if(activeSources.begin(), activeSources.end(),
                [&](const NDISourceInternal& s) { return s.info.name == name; });

            if (it != activeSources.end()) {
                it->keepAlive = 5; // Reset se ancora presente
            } else {
                NDISourceInternal newSrc;
                newSrc.info.name = name;
                newSrc.info.url_address = currentSources[i].p_url_address ? currentSources[i].p_url_address : "";
                newSrc.keepAlive = 5;
                activeSources.push_back(newSrc);
            }
        }

        activeSources.erase(std::remove_if(activeSources.begin(), activeSources.end(),
            [](const NDISourceInternal& s) { return s.keepAlive <= 0; }), activeSources.end());

        {
            std::lock_guard<std::mutex> lock(m_sourcesMutex);
            m_discoveredSources.clear();
            for (const auto& s : activeSources) {
                m_discoveredSources.push_back(s.info);
            }
        }
    }

    NDIlib_find_destroy(finder);
}

bool CNDIReceiver::Start(const std::string& sourceName) {
    if (!m_ctx) {
        DEJAVISUI_LOG_ERROR("[NDI] Init non chiamato prima di Start");
        return false;
    }

    if (m_running.load()) {
        Stop();
        DEJAVISUI_LOG_DEBUG("[NDI] gia' running, ignoro Start");
    }
    m_currentSource = sourceName;
    m_shouldExit.store(false);

    // reset video-timeout state
    m_lastVideoMs.store(0);
    m_videoSignalLost.store(false);
    m_pendingTextureDestroy.store(false);

    m_shouldExit.store(false);
    m_running.store(true);
    m_thread = std::thread(&CNDIReceiver::receiverThread, this);
    return true;
}

void CNDIReceiver::Stop() {
    if (!m_running.load() && !m_thread.joinable()) return;

    m_shouldExit.store(true);
    m_running.store(false);

    if (m_thread.joinable()) m_thread.join();

    if (m_pNDI_recv) {
        NDIlib_recv_destroy(m_pNDI_recv);
        m_pNDI_recv = nullptr;
    }
    m_resampler.cleanup();
    destroyStaging();
    m_currentSource.clear();
}

bool CNDIReceiver::ensureStagingForFrame(uint32_t w, uint32_t h) {
    std::lock_guard<std::mutex> lk(m_stagingMutex);

    const VkDeviceSize need = (VkDeviceSize)w * (VkDeviceSize)h * 4; // BGRA

    if (m_staging[0].size >= need && m_staging[1].size >= need &&
        m_staging[0].width == w && m_staging[0].height == h) {
        return true;
    }

    destroyStaging();

    for (int i = 0; i < 2; i++) {
        VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bi.size        = need;
        bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_ctx->device, &bi, nullptr, &m_staging[i].buffer) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[NDI] vkCreateBuffer staging[%d] fallita", i);
            return false;
        }

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_ctx->device, m_staging[i].buffer, &req);

        VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = FindMemoryType(m_ctx, req.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_ctx->device, &ai, nullptr, &m_staging[i].memory) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("[NDI] alloc staging memory fallita");
            return false;
        }
        vkBindBufferMemory(m_ctx->device, m_staging[i].buffer, m_staging[i].memory, 0);
        vkMapMemory(m_ctx->device, m_staging[i].memory, 0, VK_WHOLE_SIZE, 0, &m_staging[i].mapped);

        m_staging[i].size   = req.size;
        m_staging[i].width  = w;
        m_staging[i].height = h;
    }
    DEJAVISUI_LOG_DEBUG("[NDI] staging ricreati: %ux%u (%zu bytes ognuno)", w, h, (size_t)need);
    return true;
}

void CNDIReceiver::destroyStaging() {
    if (!m_ctx) return;
    for (int i = 0; i < 2; i++) {
        if (m_staging[i].mapped) {
            vkUnmapMemory(m_ctx->device, m_staging[i].memory);
            m_staging[i].mapped = nullptr;
        }
        if (m_staging[i].buffer) vkDestroyBuffer(m_ctx->device, m_staging[i].buffer, nullptr);
        if (m_staging[i].memory) vkFreeMemory(m_ctx->device, m_staging[i].memory, nullptr);
        m_staging[i] = {};
    }
}

void CNDIReceiver::receiverThread() {
    NDIlib_find_instance_t finder = NDIlib_find_create_v2();
    if (!finder) {
        DEJAVISUI_LOG_ERROR("[NDI] find_create_v2 fallita");
        m_running.store(false);
        return;
    }

    NDIlib_source_t selected{};
    bool found = false;

    while (m_running.load() && !found) {
        NDIlib_find_wait_for_sources(finder, 1000);
        uint32_t count = 0;
        const NDIlib_source_t* sources = NDIlib_find_get_current_sources(finder, &count);

        for (uint32_t i = 0; i < count; i++) {
            if (m_currentSource.empty() || (sources[i].p_ndi_name && m_currentSource == sources[i].p_ndi_name)) {
                selected = sources[i];
                found = true;
                break;
            }
        }
    }

    if (!found || !m_running.load()) {
        NDIlib_find_destroy(finder); // Qui va bene se usciamo
        m_running.store(false);
        return;
    }

   DEJAVISUI_LOG_INFO("[NDI] Connessione in corso a: %s", selected.p_ndi_name);

    NDIlib_recv_create_v3_t recv_desc{};
    recv_desc.source_to_connect_to = selected; // Usa i puntatori ancora validi
    recv_desc.color_format = NDIlib_recv_color_format_fastest;

    if (audioonly) {
        recv_desc.bandwidth = NDIlib_recv_bandwidth_audio_only;
    }else {
        recv_desc.bandwidth            = NDIlib_recv_bandwidth_highest;
    }
    recv_desc.allow_video_fields   = false;


    m_pNDI_recv = NDIlib_recv_create_v3(&recv_desc);

    NDIlib_find_destroy(finder);

    if (!m_pNDI_recv) {
        DEJAVISUI_LOG_ERROR("[NDI] recv_create_v3 fallita");
        m_running.store(false);
        return;
    }

    DEJAVISUI_LOG_INFO("[NDI] Ricevitore creato con successo.");
    int timoutcnt=0;
    while (m_running.load() && !m_shouldExit.load()) {
        NDIlib_video_frame_v2_t v{};
        NDIlib_audio_frame_v2_t a{};

        auto t = NDIlib_recv_capture_v2(m_pNDI_recv, &v, &a, nullptr, 500);

        auto checkVideoTimeout = [&]() {
            if (audioonly) return;                       // niente texture, niente da fare
            int64_t last = m_lastVideoMs.load();
            if (last == 0) return;                       // mai avuto video, niente da scadere
            if (m_videoSignalLost.load()) return;        // già perso, non rifare il log
            if (now_ms() - last > m_videoTimeoutMs) {
                DEJAVISUI_LOG_INFO("[NDI] Video assente da >%d ms, drop texture in arrivo",
                                   m_videoTimeoutMs);
                m_videoSignalLost.store(true);
                m_frameReady.store(false);
                m_pendingTextureDestroy.store(true);     // il render thread distrugge
                m_videoFrameCount.store(0);
                m_videoWidth.store(0);
                m_videoHeight.store(0);
                m_videoFps.store(0.0f);
            }
        };

        switch (t) {
            case NDIlib_frame_type_video: {
                if (!audioonly) {
                    if (v.p_data && v.xres > 0 && v.yres > 0) {
                        handleYUVFrame(v);

                        m_videoWidth.store(v.xres);
                        m_videoHeight.store(v.yres);
                        if (v.frame_rate_D > 0) {
                            m_videoFps.store((float)v.frame_rate_N / (float)v.frame_rate_D);
                        }
                        m_videoFrameCount.fetch_add(1);
                        m_lastVideoMs.store(now_ms());
                        if (m_videoSignalLost.load()) {
                            DEJAVISUI_LOG_INFO("[NDI] Video signal ripristinato");
                            m_videoSignalLost.store(false);
                        }

                        if (OnVideoFrame) OnVideoFrame(v);
                    }
                    NDIlib_recv_free_video_v2(m_pNDI_recv, &v);
                }
                break;
            }
            case NDIlib_frame_type_audio: {
                if (a.p_data && a.no_samples > 0) {
                    if (m_audio_ring_planar) {
                        if (m_sourceSR != a.sample_rate || m_sourceCh != a.no_channels) {
                            DEJAVISUI_LOG_INFO("[NDI] Audio Format Change: %dHz %dch -> %dHz %dch", a.sample_rate, a.no_channels,m_targetSR,m_targetCh);
                            m_sourceSR = a.sample_rate;
                            m_sourceCh = a.no_channels;
                            m_resampler.cleanup();
                            m_resampler.init(a.sample_rate, m_targetSR, a.no_channels , m_targetCh,AV_SAMPLE_FMT_FLTP);
                        }
                        AVFrame* out = m_resampler.processFromNDI(&a);
                        if (out) {
                            if (!m_audio_ring_planar->write(reinterpret_cast<const float* const*>(out->data),
                                                      out->nb_samples)) {

                                                      }
                        }
                    }
                    m_audioFrameCount.fetch_add(1);
                    if (OnAudioFrame) OnAudioFrame(a);
                }
                NDIlib_recv_free_audio_v2(m_pNDI_recv, &a);
                checkVideoTimeout();
                break;
            }

            case NDIlib_frame_type_error: {
                DEJAVISUI_LOG_ERROR("[NDI] Ricevuto NDIlib_frame_type_error");
                break;
                checkVideoTimeout();
            }

            case NDIlib_frame_type_none:
                DEJAVISUI_LOG_DEBUG("TIMEOUT OR NONE");
                if (timoutcnt > 5) {
                    // Segnala solo che il segnale è perso, non killare il thread!
                    m_videoFrameCount = 0;
                    m_frameReady.store(false);
                }
                checkVideoTimeout();
                timoutcnt++;

                break;
        }
    }

    DEJAVISUI_LOG_INFO("[NDI] Chiusura thread di ricezione.");
    if (m_pNDI_recv) {
        NDIlib_recv_destroy(m_pNDI_recv);
        m_pNDI_recv = nullptr;
    }
}

void CNDIReceiver::TransitionImageLayout(VkCommandBuffer cmd,
                           VulkanTexture&  tex,
                           VkImageLayout   newLayout,
                           VkPipelineStageFlags srcStage,
                           VkPipelineStageFlags dstStage)
{
    if (tex.currentLayout == newLayout) return;  // niente da fare
    if (tex.image == VK_NULL_HANDLE)    return;  // safety

    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout           = tex.currentLayout;
    b.newLayout           = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = tex.image;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.srcAccessMask       = accessMaskFor(tex.currentLayout);
    b.dstAccessMask       = accessMaskFor(newLayout);

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                         0, nullptr, 0, nullptr,
                         1, &b);

    tex.currentLayout = newLayout;
}


void CNDIReceiver::processTextureLifecycle() {
    if (audioonly) return;
    if (!m_pendingTextureDestroy.exchange(false)) {

    }

    if (!m_ctx) return;

    std::lock_guard<std::mutex> lock(m_destructionMutex);
    m_currentFrameIndex++;

    auto it = m_destructionQueue.begin();
    while (it != m_destructionQueue.end()) {
        // Aspetta 3 frame prima di distruggere (safe per triple buffering)
        if (m_currentFrameIndex - it->frameIndex >= 3) {
            if (it->sampler) vkDestroySampler(m_ctx->device, it->sampler, nullptr);
            if (it->view)    vkDestroyImageView(m_ctx->device, it->view, nullptr);
            if (it->image)   vkDestroyImage(m_ctx->device, it->image, nullptr);
            if (it->memory)   vkFreeMemory(m_ctx->device, it->memory, nullptr);

            it = m_destructionQueue.erase(it);
        } else {
            ++it;
        }
    }
}


static int ndiFourCCtoAVPixFmt(int fourCC) {
    switch (fourCC) {
        case NDIlib_FourCC_video_type_YV12: return AV_PIX_FMT_YUV420P;
        case NDIlib_FourCC_video_type_NV12: return AV_PIX_FMT_NV12;
        case NDIlib_FourCC_video_type_UYVY: return AV_PIX_FMT_UYVY422;
        default: return -1;
    }
}

void CNDIReceiver::handleYUVFrame(const NDIlib_video_frame_v2_t& v) {
    m_postProcessor->uploadYuvFrame(v);

}

Json::Value CNDIReceiver::getJsonStatus() const {
    Json::Value s;
    s["running"]       = m_running.load();
    s["source"]        = m_currentSource;
    s["video"]["width"]       = m_videoWidth.load();
    s["video"]["height"]      = m_videoHeight.load();
    s["video"]["fps"]         = m_videoFps.load();
    s["video"]["frameCount"]  = m_videoFrameCount.load();
    s["audio"]["sourceRate"]  = m_sourceSR;
    s["audio"]["sourceCh"]    = m_sourceCh;
    s["audio"]["targetRate"]  = m_targetSR;
    s["audio"]["targetCh"]    = m_targetCh;
    s["audio"]["frameCount"]  = m_audioFrameCount.load();
    return s;
}