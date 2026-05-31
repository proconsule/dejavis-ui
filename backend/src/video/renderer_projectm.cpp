#include "renderer.h"

auto dispatchLoadProc(const char* name, void* userData) -> void* {
    return (void*)glfwGetProcAddress(name);
}

void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error (%d): %s\n", error, description);
}

void CRenderer::Init_ProjectM_Opengl(const std::string& presetPath) {
    glfwSetErrorCallback(glfw_error_callback);

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

    // --- FIX NVIDIA LINUX ---
#ifdef __linux__
    // Rimuovi il forced NATIVE_CONTEXT_API.
    // Lasciare che GLFW scelga il default è solitamente più sicuro.
    // Se fallisce, il fallback sotto proverà a cambiare strategia.
#elif defined(_WIN32)
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_NATIVE_CONTEXT_API);
#endif

    m_glContext = glfwCreateWindow(core_w, core_h, "ProjectM Hidden", nullptr, nullptr);

#ifndef __APPLE__
    if (!m_glContext) {
        DEJAVISUI_LOG_DEBUG("GL 4.6 failed, trying 4.5 with default API...");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        m_glContext = glfwCreateWindow(core_w, core_h, "ProjectM Hidden", nullptr, nullptr);
    }

    if (!m_glContext && sizeof(void*) == 8) { // Tentativo disperato per NVIDIA: Forza EGL
        DEJAVISUI_LOG_DEBUG("Trying EGL fallback for NVIDIA...");
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
        m_glContext = glfwCreateWindow(core_w, core_h, "ProjectM Hidden", nullptr, nullptr);
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



    // Log della versione effettivamente ottenuta
    const char* glVersion  = (const char*)glGetString(GL_VERSION);
    const char* glRenderer = (const char*)glGetString(GL_RENDERER);
    const char* glVendor   = (const char*)glGetString(GL_VENDOR);
    DEJAVISUI_LOG_INFO("[GL] %s | %s | %s",
                       glVersion  ? glVersion  : "?",
                       glRenderer ? glRenderer : "?",
                       glVendor   ? glVendor   : "?");

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

    _projectM = projectm_create_with_opengl_load_proc(&dispatchLoadProc, nullptr);

    if (_projectM && videoTextures[0].VkTexture.image) {
        _playlist = projectm_playlist_create(_projectM);
        projectm_set_window_size(_projectM, core_w, core_h);
        projectm_playlist_add_path(_playlist, presetPath.c_str(), true, false);

        glBindFramebuffer(GL_FRAMEBUFFER, m_glFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               videoTextures[0].glTexture, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            DEJAVISUI_LOG_ERROR("Errore nella creazione del framebuffer object interop");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        DEJAVISUI_LOG_DEBUG("OpenGL inizializzato con successo");
        m_audio->projectmHandle = _projectM;
    }
}

void CRenderer::InitInteropResource(uint32_t w, uint32_t h) {
    videoTextures[0].VkTexture.width = w;
    videoTextures[0].VkTexture.height = h;

    VkExternalMemoryImageCreateInfo extImageInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };

#ifdef _WIN32
    extImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
#elif defined(__linux__)
    extImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;


    if (!m_ctx.isNVIDIA) {
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
    imgInfo.extent = { w, h, 1 };
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

#ifndef __APPLE__
    imgInfo.pNext = &extImageInfo;
    #ifdef __linux__
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    #else
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    #endif
#else
    imgInfo.pNext = nullptr;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
#endif

    if (vkCreateImage(m_ctx.device, &imgInfo, nullptr, &videoTextures[0].VkTexture.image) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("Fallita creazione interop.image");
        return;
    }

    // --- 3. ALLOCAZIONE MEMORIA ---
#ifndef __APPLE__
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_ctx.device, videoTextures[0].VkTexture.image, &memReqs);
    videoTextures[0].allocationSize = memReqs.size;

    // Fondamentale per AMD: Dedicated Allocation
    VkMemoryDedicatedAllocateInfo dedicatedInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    dedicatedInfo.image = videoTextures[0].VkTexture.image;

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
    allocInfo.memoryTypeIndex = FindMemoryType(&m_ctx,memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(m_ctx.device, &allocInfo, nullptr, &videoTextures[0].VkTexture.memory);
    vkBindImageMemory(m_ctx.device, videoTextures[0].VkTexture.image, videoTextures[0].VkTexture.memory, 0);

#else
    VkResult res2;

    // A. Allochiamo memoria reale per l'immagine (Device Local)
    VkMemoryRequirements imgMemReqs;
    vkGetImageMemoryRequirements(m_ctx.device, videoTextures[0].VkTexture.image, &imgMemReqs);

    VkMemoryAllocateInfo imgAllocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    imgAllocInfo.allocationSize = imgMemReqs.size;
    imgAllocInfo.memoryTypeIndex = FindMemoryType(&m_ctx,imgMemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_ctx.device, &imgAllocInfo, nullptr, &videoTextures[0].VkTexture.memory) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("macOS: Fallita allocazione memoria immagine");
        return;
    }
    vkBindImageMemory(m_ctx.device, videoTextures[0].VkTexture.image, videoTextures[0].VkTexture.memory, 0);

    // B. Creazione Staging Buffer per ricevere i dati da OpenGL via PBO
    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = w * h * 4;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    res2 = vkCreateBuffer(m_ctx.device, &bufferInfo, nullptr, &videoTextures[0].stagingBuffer);

    VkMemoryRequirements bufMemReqs;
    vkGetBufferMemoryRequirements(m_ctx.device, videoTextures[0].stagingBuffer, &bufMemReqs);

    VkMemoryAllocateInfo bufAllocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    bufAllocInfo.allocationSize = bufMemReqs.size;
    // Deve essere visibile alla CPU per il memcpy
    bufAllocInfo.memoryTypeIndex = FindMemoryType(&m_ctx,bufMemReqs.memoryTypeBits,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    res2 = vkAllocateMemory(m_ctx.device, &bufAllocInfo, nullptr, &videoTextures[0].stagingMemory);
    vkBindBufferMemory(m_ctx.device, videoTextures[0].stagingBuffer, videoTextures[0].stagingMemory, 0);

    // Mappiamo il puntatore una volta sola
    res2 = vkMapMemory(m_ctx.device, videoTextures[0].stagingMemory, 0, bufferInfo.size, 0, &videoTextures[0].stagingPtr);

    DEJAVISUI_LOG_DEBUG("macOS: Risorse interop (Image + Staging) inizializzate.");
#endif

#ifndef __APPLE__
#ifdef _WIN32
    VkMemoryGetWin32HandleInfoKHR getHandleInfo = { VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR };
    getHandleInfo.memory = videoTextures[0].VkTexture.memory;
    getHandleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
    auto fpGetHandle = (PFN_vkGetMemoryWin32HandleKHR)vkGetDeviceProcAddr(m_ctx.device, "vkGetMemoryWin32HandleKHR");
    if(fpGetHandle) fpGetHandle(m_ctx.device, &getHandleInfo, &videoTextures[0].sharedMemoryHandle);
#else
    VkMemoryGetFdInfoKHR getFdInfo = { VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR };
    getFdInfo.memory = videoTextures[0].VkTexture.memory;
    getFdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    auto fpGetFd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(m_ctx.device, "vkGetMemoryFdKHR");
    if(fpGetFd) fpGetFd(m_ctx.device, &getFdInfo, &videoTextures[0].sharedMemoryFd);
#endif
#endif

    VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = videoTextures[0].VkTexture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;

    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;

    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkResult res = vkCreateImageView(m_ctx.device, &viewInfo, nullptr, &videoTextures[0].VkTexture.view);

    if (res != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("Errore Vulkan: vkCreateImageView fallito con codice %d", res);
    } else {
        DEJAVISUI_LOG_DEBUG("ImageView Interop creata correttamente: %p", (void*)videoTextures[0].VkTexture.view);
    }
#ifndef __APPLE__
    videoMixerTextures[0].y_flip = true;
#endif
    videoMixerTextures[0].inUse = true;
    videoMixerTextures[0].type = 0;
    videoMixerTextures[0].isVisible = true;
}

void CRenderer::InitInteropOpenGL() {
    // Svuota eventuali errori precedenti
    while (glGetError() != GL_NO_ERROR);

#ifndef __APPLE__


    glCreateMemoryObjectsEXT(1, &videoTextures[0].glMemoryObject);

#ifdef _WIN32
    glImportMemoryWin32HandleEXT(videoTextures[0].glMemoryObject,
                                 videoTextures[0].allocationSize,
                                 GL_HANDLE_TYPE_OPAQUE_WIN32_EXT,
                                 videoTextures[0].sharedMemoryHandle);
#elif defined(__linux__)
    glImportMemoryFdEXT(videoTextures[0].glMemoryObject,
                        videoTextures[0].allocationSize,
                        GL_HANDLE_TYPE_OPAQUE_FD_EXT,
                        videoTextures[0].sharedMemoryFd);
    videoTextures[0].sharedMemoryFd = -1;
#endif

    if (glGetError() != GL_NO_ERROR) {
        DEJAVISUI_LOG_ERROR("Errore durante l'importazione della memoria esterna!");
        return;
    }

#else

    glGenBuffers(1, &videoTextures[0].glPbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, videoTextures[0].glPbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,
                 videoTextures[0].VkTexture.width * videoTextures[0].VkTexture.height * 4,
                 nullptr, GL_DYNAMIC_DRAW); // Meglio DRAW per upload verso texture
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
#endif

    // 2. CREAZIONE TEXTURE
    glGenTextures(1, &videoTextures[0].glTexture);
    glBindTexture(GL_TEXTURE_2D, videoTextures[0].glTexture);

#ifdef __APPLE__
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 videoTextures[0].VkTexture.width, videoTextures[0].VkTexture.height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#else
    // NVIDIA richiede che i parametri della texture siano coerenti con l'immagine Vulkan
    glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8,
                         videoTextures[0].VkTexture.width, videoTextures[0].VkTexture.height,
                         videoTextures[0].glMemoryObject, 0);
#endif

    if (glGetError() != GL_NO_ERROR) {
        DEJAVISUI_LOG_ERROR("Errore durante il binding della memoria alla texture GL");
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (m_glFbo == 0) glGenFramebuffers(1, &m_glFbo);

    glBindFramebuffer(GL_FRAMEBUFFER, m_glFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, videoTextures[0].glTexture, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        DEJAVISUI_LOG_ERROR("FBO Incompleto: 0x%x", status);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}


void CRenderer::Execute_ProjectM() {
    if (m_glContext && _projectM) {
        if (m_shouldLoadPresetFile) {
            projectm_load_preset_file(_projectM, m_presetFileToLoad.c_str(), false);
            m_shouldLoadPresetFile = false;
        }

        if (m_shouldLoadPresetData) {
            projectm_load_preset_data(_projectM, m_presetDataToLoad.c_str(), false);
            m_shouldLoadPresetData = false;
        }

        glfwMakeContextCurrent(m_glContext);
        glBindFramebuffer(GL_FRAMEBUFFER, m_glFbo); // Questo FBO punta a interop.glTexture


        glViewport(0, 0, videoTextures[0].VkTexture.width, videoTextures[0].VkTexture.height);

        projectm_opengl_render_frame_fbo(_projectM, m_glFbo);

#ifdef __APPLE__


        glBindBuffer(GL_PIXEL_PACK_BUFFER, videoTextures[0].glPbo);
        glReadPixels(0, 0, videoTextures[0].VkTexture.width, videoTextures[0].VkTexture.height, GL_RGBA, GL_UNSIGNED_BYTE, 0);

        // Mappiamo il PBO per renderlo leggibile dalla CPU (e poi passarlo a Vulkan)
        void* ptr = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (ptr) {
            memcpy(videoTextures[0].stagingPtr, ptr, videoTextures[0].VkTexture.width * videoTextures[0].VkTexture.height * 4);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

#endif


        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glFlush(); // Sincronizza OpenGL prima di tornare a Vulkan
        glFinish();
    }
}