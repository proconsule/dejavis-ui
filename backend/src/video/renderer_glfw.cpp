#include "renderer.h"
#include <iostream>

bool CRenderer::Init_GLFW_Window(uint32_t _w, uint32_t _h) {
    // 1. Inizializzazione GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    // 2. Hint fondamentali per Vulkan
    // Diciamo a GLFW di NON creare un contesto OpenGL (fondamentale!)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // 3. Creazione Finestra
    glfw_window = glfwCreateWindow(_w, _h, "Dejavis UI", nullptr, nullptr);

    if (!glfw_window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    uint32_t count;
    const char** extensions = glfwGetRequiredInstanceExtensions(&count);
    for (uint32_t i = 0; i < count; i++) {
        std::cout << "Extension: " << extensions[i] << std::endl;
    }
    // 4. Creazione Superficie Vulkan (molto più semplice che in SDL2)
    // GLFW gestisce internamente la selezione delle estensioni (X11/Wayland/Win32)
    if (glfwCreateWindowSurface(m_ctx.instance, glfw_window, nullptr, &m_display.surface) != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan surface" << std::endl;
        return false;
    }

    // 5. Setup Swapchain (come facevi prima)
    if(!CreateSwapChainOnly()){
        printf("Unable to create swapchain\r\n");
        return false;
    }

    glfw_active = true;
    window_w = _w;
    window_h = _h;

    glfwGetWindowPos(glfw_window, &win_pos_x, &win_pos_y);

    // Impostiamo un puntatore alla classe per le callback (opzionale ma consigliato)
    glfwSetWindowUserPointer(glfw_window, this);

    return true;
}

void CRenderer::GLFW_Vulkan_Submit(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t syncIndex) {
    std::vector<VkSemaphore>          waitSemaphores;
    std::vector<VkPipelineStageFlags> waitStages;

    waitSemaphores.push_back(m_display.imageAvailableSemaphores[syncIndex]);
    waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    if (cmd == VK_NULL_HANDLE) {
        DEJAVISUI_LOG_ERROR("Command Buffer nullo!");
        return;
    }

    for (int i = 0; i < 10; i++) {
        if (!videoMixerTextures[i].inUse) continue;

        if (i == 0 && m_projectm_wrapper) {
            if (auto* pp = m_projectm_wrapper->getPostProcessor()) pp->processLifecycle(framecount);
        }
        if (auto* dec = videoMixerTextures[i].AV_DECODER) {
            if (auto* pp = dec->getPostProcessor()) pp->processLifecycle(framecount);
        }
        if (auto* nr = videoMixerTextures[i].ndi_receiver) {
            if (auto* pp = nr->getPostProcessor()) pp->processLifecycle(framecount);
        }
    }

    auto drainToWait = [&](std::vector<VkSemaphore>& semList) {
        for (VkSemaphore s : semList) {
            if (s != VK_NULL_HANDLE) {
                waitSemaphores.push_back(s);
                waitStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            }
        }
    };

    for (int i = 0; i < 10; i++) {
        //DEJAVISUI_LOG_DEBUG("[Drain] slot %d inUse=%d", i, (int)videoMixerTextures[i].inUse);

        if (!videoMixerTextures[i].inUse) continue;   // ← stesso check di sopra

        std::vector<VkSemaphore> drained;

        if (m_projectm_wrapper && i == 0) {
            if (auto* fx = m_projectm_wrapper->getPostProcessor()) fx->getWaitSemaphores(drained);
        }
        if (auto* dec = videoMixerTextures[i].AV_DECODER) {
            if (auto* fx = dec->getPostProcessor()) fx->getWaitSemaphores(drained);
        }
        if (auto* nr = videoMixerTextures[i].ndi_receiver) {
            if (auto* fx = nr->getPostProcessor()) fx->getWaitSemaphores(drained);
        }

        drainToWait(drained);
    }

    // === Setup signal: binario per swapchain + timeline per encoder (se attivo) ===
    VkSemaphore renderDoneSemaphore = m_display.renderFinishedSemaphores[imageIndex];

    uint64_t encoderTimelineValue = 0;
    if (m_pending_encoder_slot) {
        encoderTimelineValue = RGB2YUVPipeline::instance()
            .reserveMixerTimelineValue(*m_pending_encoder_slot);
    }

    std::vector<VkSemaphore> signalSemaphores;
    std::vector<uint64_t>    signalValues;
    signalSemaphores.push_back(renderDoneSemaphore);
    signalValues.push_back(0);   // ignorato per binari

    if (m_pending_encoder_slot) {
        signalSemaphores.push_back(
            RGB2YUVPipeline::instance().getMixerTimelineSemaphore(*m_pending_encoder_slot));
        signalValues.push_back(encoderTimelineValue);
    }

    // === Wait values: tutti zero (sono tutti binari) ===
    std::vector<uint64_t> waitValues(waitSemaphores.size(), 0);

    VkTimelineSemaphoreSubmitInfo timelineInfo{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    timelineInfo.waitSemaphoreValueCount   = static_cast<uint32_t>(waitValues.size());
    timelineInfo.pWaitSemaphoreValues      = waitValues.data();
    timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
    timelineInfo.pSignalSemaphoreValues    = signalValues.data();

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.pNext                = &timelineInfo;
    submitInfo.waitSemaphoreCount   = static_cast<uint32_t>(waitSemaphores.size());
    submitInfo.pWaitSemaphores      = waitSemaphores.data();
    submitInfo.pWaitDstStageMask    = waitStages.data();
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &cmd;
    submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
    submitInfo.pSignalSemaphores    = signalSemaphores.data();

    VkResult submitRes;
    {
        std::lock_guard<std::mutex> qlock(m_ctx.graphicsQueueMutex);
        submitRes = vkQueueSubmit(m_ctx.graphicsQueue, 1, &submitInfo,
                                  m_display.inFlightFences[syncIndex]);
    }

    if (submitRes != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("Queue Submit Failed: %d", submitRes);
        if (m_pending_encoder_slot) {
            AV_ENCODER->releaseSlot(m_pending_encoder_slot);
            m_pending_encoder_slot = nullptr;
        }
        return;
    }

    if (m_pending_encoder_slot) {
        RGB2YUVPipeline::instance().submitAsync(
            *m_pending_encoder_slot,
            encoderTimelineValue,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        AV_ENCODER->submitSlot(m_pending_encoder_slot, m_pending_encoder_pts);
        m_pending_encoder_slot = nullptr;
    }

    // === Present ===
    VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &renderDoneSemaphore;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &m_display.swapchain;
    presentInfo.pImageIndices      = &imageIndex;

    VkResult presentRes;
    {
        std::lock_guard<std::mutex> qlock(m_ctx.graphicsQueueMutex);
        presentRes = vkQueuePresentKHR(m_ctx.graphicsQueue, &presentInfo);
    }

    if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR) {
        RecreateSwapChain();
    } else if (presentRes != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("Queue Present Failed: %d", presentRes);
    }

    m_display.currentFrame = (syncIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    framecount++;
}