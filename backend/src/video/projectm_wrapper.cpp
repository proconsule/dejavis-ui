#include "projectm_wrapper.h"

auto projectm_dispatchLoadProc(const char* name, void* userData) -> void* {
    return (void*)glfwGetProcAddress(name);
}

void projectm_glfw_error_callback(int error, const char* description) {
    DEJAVISUI_LOG_ERROR("GLFW Error (%d): %s", error, description);
}

void cprojectm_wrapper::Init(VulkanContext *_ctx,VulkanUniTexture * _tex,uint32_t _width, uint32_t _height) {
    pjm_width = _width;
    pjm_height = _height;
    outText = _tex;
    m_ctx = _ctx;
    InitInteropResource();
    Init_ProjectM_Opengl();
}

void cprojectm_wrapper::PostProcessInit() {
    m_postProcessor = std::make_unique<CPostProcessor>(m_ctx);
    m_postProcessor->initRgbaInput(*outText,outText->VkTexture.width,outText->VkTexture.height);

}

void cprojectm_wrapper::Init_ProjectM_Opengl() {
    glfwSetErrorCallback(projectm_glfw_error_callback);


    if (!glfwInit()) {
        DEJAVISUI_LOG_ERROR("GLFW Init Failed!");
        return;
    }

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_VISIBLE,    GLFW_FALSE);

    // Gestione Versioni
#ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif

#ifdef __linux__
    // Rimuovi il forced NATIVE_CONTEXT_API.
    // Lasciare che GLFW scelga il default è solitamente più sicuro.
    // Se fallisce, il fallback sotto proverà a cambiare strategia.
#elif defined(_WIN32)
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_NATIVE_CONTEXT_API);
#endif

    m_glContext = glfwCreateWindow(pjm_width, pjm_height, "ProjectM Hidden", nullptr, nullptr);

#ifndef __APPLE__
    if (!m_glContext) {
        DEJAVISUI_LOG_DEBUG("GL 4.6 failed, trying 4.5 with default API...");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        m_glContext = glfwCreateWindow(pjm_width, pjm_height, "ProjectM Hidden", nullptr, nullptr);
    }

    if (!m_glContext && sizeof(void*) == 8) {
        DEJAVISUI_LOG_DEBUG("Trying EGL fallback for NVIDIA...");
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
        m_glContext = glfwCreateWindow(pjm_width, pjm_height, "ProjectM Hidden", nullptr, nullptr);
    }
#endif

    if (!m_glContext) {
        DEJAVISUI_LOG_ERROR("Impossibile creare il contesto OpenGL");
        return;
    }

    glfwMakeContextCurrent(m_glContext);

    glfwSwapInterval(0);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        DEJAVISUI_LOG_ERROR("GLAD Init Failed!");
        return;
    }

    const char* glVersion  = (const char*)glGetString(GL_VERSION);
    const char* glRenderer = (const char*)glGetString(GL_RENDERER);
    const char* glVendor   = (const char*)glGetString(GL_VENDOR);
    DEJAVISUI_LOG_INFO("[GL] %s | %s | %s",
                       glVersion  ? glVersion  : "?",
                       glRenderer ? glRenderer : "?",
                       glVendor   ? glVendor   : "?");


    // --- 5. INTEROP CHECK ---
#ifdef _WIN32
    bool interopSupported = GLAD_GL_EXT_memory_object_win32;
#elif defined(__linux__)
    bool interopSupported = GLAD_GL_EXT_memory_object_fd;
#else
    bool interopSupported = false;  // macOS: niente GL/Vulkan interop
#endif

#ifndef __APPLE__
    if (!interopSupported) {
        DEJAVISUI_LOG_ERROR("Estensioni Interop Memoria NON supportate dal driver");
    }
#endif

    InitInteropOpenGL();

    // --- 6. PROJECTM ---
    _projectM = projectm_create_with_opengl_load_proc(&projectm_dispatchLoadProc, nullptr);

    if (_projectM && outText->VkTexture.image) {
        projectm_set_window_size(_projectM, pjm_width, pjm_height);

        glBindFramebuffer(GL_FRAMEBUFFER, m_glFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               outText->glTexture, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            DEJAVISUI_LOG_ERROR("Errore nella creazione del framebuffer object interop");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        DEJAVISUI_LOG_DEBUG("OpenGL inizializzato con successo");
    }
}

void cprojectm_wrapper::InitInteropResource() {
    outText->VkTexture.width = pjm_width;
    outText->VkTexture.height = pjm_height;

    VkExternalMemoryImageCreateInfo extImageInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };

#ifdef _WIN32
    extImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
#elif defined(__linux__)
    extImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;


    if (!m_ctx->isNVIDIA) {
        VkImageDrmFormatModifierListCreateInfoEXT modInfo = { VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT };
        uint64_t modifiers[] = { 0x0 };
        modInfo.drmFormatModifierCount = 1;
        modInfo.pDrmFormatModifiers = modifiers;
        extImageInfo.pNext = &modInfo;
    }
#endif

    // --- 2. CREAZIONE IMMAGINE ---
    VkImageCreateInfo imgInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent = { pjm_width, pjm_height, 1 };
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

#ifndef __APPLE__
    imgInfo.pNext = &extImageInfo;
    #ifdef __linux__
        // Se AMD, usiamo DRM. Se NVIDIA, OPTIMAL standard (NVIDIA ignora modInfo se presente)
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    #else
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    #endif
#else
    imgInfo.pNext = nullptr;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
#endif

    if (vkCreateImage(m_ctx->device, &imgInfo, nullptr, &outText->VkTexture.image) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("Fallita creazione interop.image");
        return;
    }

    // --- 3. ALLOCAZIONE MEMORIA ---
#ifndef __APPLE__
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_ctx->device, outText->VkTexture.image, &memReqs);
    outText->allocationSize = memReqs.size;

    VkMemoryDedicatedAllocateInfo dedicatedInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    dedicatedInfo.image = outText->VkTexture.image;

    VkExportMemoryAllocateInfo exportInfo = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
#ifdef _WIN32
    exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
#else
    exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
    dedicatedInfo.pNext = &exportInfo;

    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.pNext = &dedicatedInfo;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(m_ctx,memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(m_ctx->device, &allocInfo, nullptr, &outText->VkTexture.memory);
    vkBindImageMemory(m_ctx->device, outText->VkTexture.image, outText->VkTexture.memory, 0);

#else
    VkResult res2;

    // A. Allochiamo memoria reale per l'immagine (Device Local)
    VkMemoryRequirements imgMemReqs;
    vkGetImageMemoryRequirements(m_ctx->device, outText->VkTexture.image, &imgMemReqs);

    VkMemoryAllocateInfo imgAllocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    imgAllocInfo.allocationSize = imgMemReqs.size;
    imgAllocInfo.memoryTypeIndex = FindMemoryType(m_ctx,imgMemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_ctx->device, &imgAllocInfo, nullptr, &outText->VkTexture.memory) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("macOS: Fallita allocazione memoria immagine");
        return;
    }
    vkBindImageMemory(m_ctx->device, outText->VkTexture.image, outText->VkTexture.memory, 0);

    // B. Creazione Staging Buffer per ricevere i dati da OpenGL via PBO
    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = pjm_width * pjm_height * 4;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    res2 = vkCreateBuffer(m_ctx->device, &bufferInfo, nullptr, &outText->stagingBuffer);

    VkMemoryRequirements bufMemReqs;
    vkGetBufferMemoryRequirements(m_ctx->device, outText->stagingBuffer, &bufMemReqs);

    VkMemoryAllocateInfo bufAllocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    bufAllocInfo.allocationSize = bufMemReqs.size;
    // Deve essere visibile alla CPU per il memcpy
    bufAllocInfo.memoryTypeIndex = FindMemoryType(m_ctx,bufMemReqs.memoryTypeBits,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    res2 = vkAllocateMemory(m_ctx->device, &bufAllocInfo, nullptr, &outText->stagingMemory);
    vkBindBufferMemory(m_ctx->device, outText->stagingBuffer, outText->stagingMemory, 0);

    // Mappiamo il puntatore una volta sola
    res2 = vkMapMemory(m_ctx->device, outText->stagingMemory, 0, bufferInfo.size, 0, &outText->stagingPtr);

    DEJAVISUI_LOG_DEBUG("macOS: Risorse interop (Image + Staging) inizializzate.");
#endif

    // --- 4. ESTRAZIONE HANDLE (Solo Win/Linux) ---
#ifndef __APPLE__
#ifdef _WIN32
    VkMemoryGetWin32HandleInfoKHR getHandleInfo = { VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR };
    getHandleInfo.memory = outText->VkTexture.memory;
    getHandleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
    auto fpGetHandle = (PFN_vkGetMemoryWin32HandleKHR)vkGetDeviceProcAddr(m_ctx->device, "vkGetMemoryWin32HandleKHR");
    if(fpGetHandle) fpGetHandle(m_ctx->device, &getHandleInfo, &outText->sharedMemoryHandle);
#else
    VkMemoryGetFdInfoKHR getFdInfo = { VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR };
    getFdInfo.memory = outText->VkTexture.memory;
    getFdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    auto fpGetFd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(m_ctx->device, "vkGetMemoryFdKHR");
    if(fpGetFd) fpGetFd(m_ctx->device, &getFdInfo, &outText->sharedMemoryFd);
#endif
#endif

    VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = outText->VkTexture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;

    // CAMBIA QUESTO: Usa R8G8B8A8 invece di R8G8B8
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;

    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    // ASSEGNA IL RISULTATO A interop.view
    VkResult res = vkCreateImageView(m_ctx->device, &viewInfo, nullptr, &outText->VkTexture.view);

    if (res != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("Errore Vulkan: vkCreateImageView fallito con codice %d", res);
    } else {
        DEJAVISUI_LOG_DEBUG("ImageView Interop creata correttamente: %p", (void*)outText->VkTexture.view);
    }

}

void cprojectm_wrapper::InitInteropOpenGL() {
    while (glGetError() != GL_NO_ERROR);

#ifndef __APPLE__


    glCreateMemoryObjectsEXT(1, &outText->glMemoryObject);

#ifdef _WIN32
    glImportMemoryWin32HandleEXT(outText->glMemoryObject,
                                 outText->allocationSize,
                                 GL_HANDLE_TYPE_OPAQUE_WIN32_EXT,
                                 outText->sharedMemoryHandle);
#elif defined(__linux__)
    // Nota: Il driver NVIDIA chiuderà il FD internamente dopo l'import
    glImportMemoryFdEXT(outText->glMemoryObject,
                        outText->allocationSize,
                        GL_HANDLE_TYPE_OPAQUE_FD_EXT,
                        outText->sharedMemoryFd);
    outText->sharedMemoryFd = -1;
#endif

    if (glGetError() != GL_NO_ERROR) {
        DEJAVISUI_LOG_ERROR("Errore durante l'importazione della memoria esterna!");
        return;
    }

#else
    // macOS: PBO Fallback
    glGenBuffers(1, &outText->glPbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, outText->glPbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,
                 outText->VkTexture.width * outText->VkTexture.height * 4,
                 nullptr, GL_DYNAMIC_DRAW); // Meglio DRAW per upload verso texture
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
#endif

    // 2. CREAZIONE TEXTURE
    glGenTextures(1, &outText->glTexture);
    glBindTexture(GL_TEXTURE_2D, outText->glTexture);

#ifdef __APPLE__
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 outText->VkTexture.width, outText->VkTexture.height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#else
    glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8,
                         outText->VkTexture.width, outText->VkTexture.height,
                         outText->glMemoryObject, 0);
#endif

    if (glGetError() != GL_NO_ERROR) {
        DEJAVISUI_LOG_ERROR("Errore durante il binding della memoria alla texture GL");
    }

    // Parametri di campionamento
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (m_glFbo == 0) glGenFramebuffers(1, &m_glFbo);

    glBindFramebuffer(GL_FRAMEBUFFER, m_glFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outText->glTexture, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        DEJAVISUI_LOG_ERROR("FBO Incompleto: 0x%x", status);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void cprojectm_wrapper::Execute_ProjectM() {

    if (m_shouldLoadPresetData) {
        projectm_load_preset_data(_projectM, m_presetDataToLoad.c_str(), false);
        preset_status.name = m_presetDataToLoadOrigFile;
        preset_status.id = m_shouldLoadPresetID;
        m_shouldLoadPresetData = false;
    }

    if (m_glContext && _projectM) {

        glfwMakeContextCurrent(m_glContext);
        glBindFramebuffer(GL_FRAMEBUFFER, m_glFbo); // Questo FBO punta a interop.glTexture


        glViewport(0, 0, outText->VkTexture.width, outText->VkTexture.height);

        projectm_opengl_render_frame_fbo(_projectM, m_glFbo);

#ifdef __APPLE__
        glBindBuffer(GL_PIXEL_PACK_BUFFER, outText->glPbo);
        glReadPixels(0, 0, outText->VkTexture.width, outText->VkTexture.height, GL_RGBA, GL_UNSIGNED_BYTE, 0);

        void* ptr = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (ptr) {
            memcpy(outText->stagingPtr, ptr, outText->VkTexture.width * outText->VkTexture.height * 4);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
#endif

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glFlush();
        glFinish();
    }
}

void cprojectm_wrapper::PushAudio(const float *const *input, int in_samples) {
    if (!_projectM)return;
    const int channels = 2;

    std::vector<float> interleaved(in_samples * channels);

    for (int s = 0; s < in_samples; ++s) {
        for (int c = 0; c < channels; ++c) {
            interleaved[s * channels + c] = input[c][s];
        }
    }

    projectm_pcm_add_float(_projectM, interleaved.data(), in_samples, PROJECTM_STEREO);
}

Json::Value cprojectm_wrapper::getStatusJson() {
    Json::Value status;

    status["current_preset"] = preset_status.name;
    status["fps"] = 0;
    Json::Value sett ;
    sett["fps_limit"] = 60;
    sett["soft_cut_duration"] = 5;
    sett["beat_sensitivity"] = 1.0;
    status["settings"] = sett;
    return status;
}