#include "renderer.h"

#include <iostream>

bool CRenderer::CreateFences(){


    VkFenceCreateInfo fInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };
    VkSemaphoreCreateInfo sInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    m_display.inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_display.imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateFence(m_ctx.device, &fInfo, nullptr, &m_display.inFlightFences[i]);
        vkCreateSemaphore(m_ctx.device, &sInfo, nullptr, &m_display.imageAvailableSemaphores[i]);
    }
    return true;
}


bool CRenderer::CreateSwapChainOnly() {
    if (m_ctx.device == VK_NULL_HANDLE || m_display.surface == VK_NULL_HANDLE) return false;

    // 1. Aspetta che il device sia libero prima di toccare la swapchain
    vkDeviceWaitIdle(m_ctx.device);

    // 2. Recupera le capacità della superficie
    VkSurfaceCapabilitiesKHR capabilities;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_ctx.physicalDevice, m_display.surface, &capabilities) != VK_SUCCESS) {
        return false;
    }

    // 3. Calcolo dell'Extent (Risoluzione della Swapchain)
    if (capabilities.currentExtent.width != UINT32_MAX) {
        m_display.swapchainExtent = capabilities.currentExtent;
    } else {
        int w, h;
        glfwGetFramebufferSize(glfw_window, &w, &h);
        m_display.swapchainExtent.width = std::clamp((uint32_t)w, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        m_display.swapchainExtent.height = std::clamp((uint32_t)h, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    // --- LOGICA ASPECT RATIO (16:9) ---
    uint32_t width = m_display.swapchainExtent.width;
    uint32_t height = m_display.swapchainExtent.height;
    float targetRatio = 16.0f / 9.0f; // Modifica qui per altri ratio (es. 21/9)
    float windowRatio = (float)width / (float)height;

    // Calcoliamo la Viewport centrata
    if (windowRatio > targetRatio) {
        // Finestra troppo larga: Pillarbox (bande nere ai lati)
        float viewWidth = (float)height * targetRatio;
        m_display.viewport.x = (width - viewWidth) / 2.0f;
        m_display.viewport.y = 0;
        m_display.viewport.width = viewWidth;
        m_display.viewport.height = (float)height;
    } else {
        // Finestra troppo alta: Letterbox (bande nere sopra/sotto)
        float viewHeight = (float)width / targetRatio;
        m_display.viewport.x = 0;
        m_display.viewport.y = (height - viewHeight) / 2.0f;
        m_display.viewport.width = (float)width;
        m_display.viewport.height = viewHeight;
    }

    m_display.viewport.minDepth = 0.0f;
    m_display.viewport.maxDepth = 1.0f;

    // Lo scissor deve combaciare con la viewport per tagliare i pixel fuori area
    m_display.scissor.offset = { (int32_t)m_display.viewport.x, (int32_t)m_display.viewport.y };
    m_display.scissor.extent = { (uint32_t)m_display.viewport.width, (uint32_t)m_display.viewport.height };
    // ----------------------------------

    // 4. Configurazione Swapchain
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchainInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    swapchainInfo.surface = m_display.surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = m_display.swapchainFormat;
    swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainInfo.imageExtent = m_display.swapchainExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = m_display.swapchain; // Fondamentale per performance al resize

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_ctx.physicalDevice, m_display.surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> availablePresentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_ctx.physicalDevice, m_display.surface, &presentModeCount, availablePresentModes.data());

    // 2. Helper per controllare se un modo esiste
    auto isSupported = [&](VkPresentModeKHR mode) {
        return std::find(availablePresentModes.begin(), availablePresentModes.end(), mode) != availablePresentModes.end();
    };

    // 3. Applica la tua logica con fallback sicuro
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;

    if (!vsync) {
        if (framelimiter && isSupported(VK_PRESENT_MODE_MAILBOX_KHR)) {
            swapchainInfo.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            DEJAVISUI_LOG_DEBUG("USING VK_PRESENT_MODE_MAILBOX_KHR");
        } else if (isSupported(VK_PRESENT_MODE_IMMEDIATE_KHR)) {
            swapchainInfo.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            DEJAVISUI_LOG_DEBUG("USING VK_PRESENT_MODE_IMMEDIATE_KHR");
        }
    }else {
        DEJAVISUI_LOG_DEBUG("USING VK_PRESENT_MODE_FIFO_KHR");
    }


    VkSwapchainKHR newSwapchain;
    if (vkCreateSwapchainKHR(m_ctx.device, &swapchainInfo, nullptr, &newSwapchain) != VK_SUCCESS) {
        return false;
    }

    // Pulizia vecchia swapchain e views
    if (m_display.swapchain != VK_NULL_HANDLE) {
        for (auto view : m_display.swapchainImageViews) vkDestroyImageView(m_ctx.device, view, nullptr);
        for (auto fb : m_display.framebuffers) vkDestroyFramebuffer(m_ctx.device, fb, nullptr);
        vkDestroySwapchainKHR(m_ctx.device, m_display.swapchain, nullptr);
    }
    m_display.swapchain = newSwapchain;

    // 5. Recupero nuove immagini e creazione Views
    vkGetSwapchainImagesKHR(m_ctx.device, m_display.swapchain, &imageCount, nullptr);
    m_display.swapchainImages.resize(imageCount);
    m_display.imagesInFlight.assign(imageCount, VK_NULL_HANDLE);
    vkGetSwapchainImagesKHR(m_ctx.device, m_display.swapchain, &imageCount, m_display.swapchainImages.data());

    for (VkSemaphore& sem : m_display.renderFinishedSemaphores) {
        if (sem != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_ctx.device, sem, nullptr);
            sem = VK_NULL_HANDLE;
        }
    }

    m_display.renderFinishedSemaphores.assign(imageCount, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo sInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (uint32_t i = 0; i < imageCount; ++i) {
        if (vkCreateSemaphore(m_ctx.device, &sInfo, nullptr, &m_display.renderFinishedSemaphores[i]) != VK_SUCCESS) {
            DEJAVISUI_LOG_ERROR("Fallita creazione renderFinishedSemaphore per swapchain image %u", i);
            return false;
        }
    }

    m_display.swapchainImageViews.resize(imageCount);
    m_display.imagesInFlight.resize(imageCount);


    for (size_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = m_display.swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_display.swapchainFormat;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(m_ctx.device, &viewInfo, nullptr, &m_display.swapchainImageViews[i]) != VK_SUCCESS) return false;
    }

    // 6. Creazione Framebuffers
    m_display.framebuffers.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++) {
        VkImageView attachments[] = { m_display.swapchainImageViews[i] };
        VkFramebufferCreateInfo fbInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fbInfo.renderPass = m_master_per_frame[0].renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = attachments;
        fbInfo.width = m_display.swapchainExtent.width;
        fbInfo.height = m_display.swapchainExtent.height;
        fbInfo.layers = 1;
        if (vkCreateFramebuffer(m_ctx.device, &fbInfo, nullptr, &m_display.framebuffers[i]) != VK_SUCCESS) return false;
    }



    return true;
}

bool CRenderer::RecreateSwapChain() {
    if (m_ctx.device == VK_NULL_HANDLE) return false;
    int w = 0, h = 0;
    glfwGetFramebufferSize(glfw_window, &w, &h);
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(glfw_window, &w, &h);


        if (!running) return false;
    }

    //window_w = w;
    //window_h = h;
    vkDeviceWaitIdle(m_ctx.device);

    CleanupSwapChain();

    if (!CreateSwapChainOnly()) {
        DEJAVISUI_LOG_ERROR("Errore critico nella ricreazione della SwapChain!");
        vulkanReady = false;
        return false; // Ritorno in caso di errore
    }
    return true; // Ritorno in caso di successo
}

void CRenderer::CleanupSwapChain() {
    if (m_ctx.device == VK_NULL_HANDLE) return;

    // Aspettiamo che la GPU sia ferma prima di distruggere i buffer in uso
    vkDeviceWaitIdle(m_ctx.device);

    // 1. Distruggiamo i Framebuffer della swapchain
    for (auto framebuffer : m_display.framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_ctx.device, framebuffer, nullptr);
        }
    }
    m_display.framebuffers.clear();

    // 2. Distruggiamo le Image Views
    for (auto imageView : m_display.swapchainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_ctx.device, imageView, nullptr);
        }
    }
    m_display.swapchainImageViews.clear();

    // 3. La Swapchain stessa
    if (m_display.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_ctx.device, m_display.swapchain, nullptr);
        m_display.swapchain = VK_NULL_HANDLE;
    }

}


void CRenderer::SetFullScreen(bool _val, bool _reschange) {
    if (glfw_active) {
        if (!_val) {

            glfwSetWindowMonitor(glfw_window, nullptr, win_pos_x, win_pos_y, window_w, window_h, 0);
        } else {

            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);

            if (_reschange) {
                glfwSetWindowMonitor(glfw_window, monitor, 0, 0, window_w, window_h, mode->refreshRate);
            } else {
                glfwSetWindowMonitor(glfw_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            }
        }
        fullscreen = _val;
    }
}