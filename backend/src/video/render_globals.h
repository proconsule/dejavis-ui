#ifndef DEJAVIS_APP_RENDER_GLOBALS_H
#define DEJAVIS_APP_RENDER_GLOBALS_H

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>
#ifdef WIN32
#include <windows.h>
#endif
#ifdef __APPLE__
    #include <OpenGL/gl.h>
#else
    #include <GL/gl.h>
#endif

#include <mutex>
#include <atomic>

#include "../logger.h"

inline const char* VkResultToString(VkResult result) {
    switch (result) {
        case VK_SUCCESS:                        return "VK_SUCCESS";
        case VK_NOT_READY:                      return "VK_NOT_READY";
        case VK_TIMEOUT:                        return "VK_TIMEOUT";
        case VK_EVENT_SET:                      return "VK_EVENT_SET";
        case VK_EVENT_RESET:                    return "VK_EVENT_RESET";
        case VK_INCOMPLETE:                     return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:       return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:     return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:    return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:              return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:        return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:        return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:    return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:      return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:      return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:         return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:     return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:          return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN:                  return "VK_ERROR_UNKNOWN";
        case VK_ERROR_OUT_OF_POOL_MEMORY:       return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:  return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_FRAGMENTATION:            return "VK_ERROR_FRAGMENTATION";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
        case VK_ERROR_SURFACE_LOST_KHR:         return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR:                 return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:          return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT:    return "VK_ERROR_VALIDATION_FAILED_EXT";
        case VK_ERROR_INVALID_SHADER_NV:        return "VK_ERROR_INVALID_SHADER_NV";
        case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT: return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
        default:                                return "VK_RESULT_UNKNOWN";
    }
}

#define VK_CHECK(expr)                                                              \
    do {                                                                            \
    VkResult _vk_result = (expr);                                               \
    if (_vk_result != VK_SUCCESS) {                                             \
    DEJAVISUI_LOG_DEBUG("[Vulkan] %s failed: %s (%d) at %s:%d",            \
    #expr,                                                              \
    VkResultToString(_vk_result),                                       \
    static_cast<int>(_vk_result),                                       \
    __FILE__, __LINE__);                                                \
    return false;                                                           \
    }                                                                           \
} while(0)

struct Vulkan_DisplayContext{
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    VkFormat swapchainFormat{VK_FORMAT_R8G8B8A8_UNORM};
    //VkFormat swapchainFormat{VK_FORMAT_R8G8B8A8_UNORM};
    VkExtent2D swapchainExtent{0, 0};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> framebuffers;
    //VkRenderPass renderPass{VK_NULL_HANDLE};
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;
    uint32_t currentFrame = 0;
    VkCommandPool commandPool{VK_NULL_HANDLE};

    VkViewport viewport;
    VkRect2D   scissor;

    int bustodisplay = -1;


};

enum class QueueType {
    Graphics,
    Compute,
    Transfer
};


struct KeyerPushConstants {
    float v0 = 0.0f;
    float v1 = 1.0f;
    float v2 = 0.0f;
    float threshold = 0.0f;
    float softness  = 0.1f;
    float spill     = 0.5f;
    float enabled   = 0.0f;
    float _pad      = 0.0f;

    // Helper per costruire una chroma key con nomi parlanti
    static KeyerPushConstants makeChroma(float r, float g, float b,
                                         float threshold, float softness,
                                         float spill, bool enabled) {
        KeyerPushConstants k;
        k.v0 = r; k.v1 = g; k.v2 = b;
        k.threshold = threshold;
        k.softness  = softness;
        k.spill     = spill;
        k.enabled   = enabled ? 1.0f : 0.0f;
        return k;
    }
};
static_assert(sizeof(KeyerPushConstants) == 32, "KeyerPushConstants must be 32 bytes");

struct ColorParams {
    float brightness = 0.0f;
    float contrast   = 1.0f;
    float saturation = 1.0f;
    float gamma      = 1.0f;
    float hueShift   = 0.0f;
    float blackLevel = 0.0f;
    float whiteLevel = 1.0f;
    float enabled    = 0.0f;  // <= 0 = passthrough
};

static VkAccessFlags accessMaskFor(VkImageLayout l) {
    switch (l) {
        case VK_IMAGE_LAYOUT_UNDEFINED:                        return 0;

            // Per il campionamento negli shader (NDI, Video Mixer)
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:         return VK_ACCESS_SHADER_READ_BIT;

            // Per il blit verso la swapchain
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:             return VK_ACCESS_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:             return VK_ACCESS_TRANSFER_WRITE_BIT;

            // Per il rendering ImGui o Video Pass
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            // Per i Compute Shader (Encoding RGB->YUV)
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:                  return 0;

        default:
            DEJAVISUI_LOG_WARN("Access mask non gestita per layout: %d", l);
            return 0;
    }
}

static VkPipelineStageFlags stageMaskFor(VkImageLayout l) {
    switch (l) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

            // Trasferimenti (Blit, Copy, NDI readback)
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;

            // Campionamento negli Shader (Video Mixer, Post-FX)
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

            // Rendering diretto (ImGui, Master Pass)
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

            // Compute Shader (Encoding RGB->YUV)
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

            // Presentazione allo schermo
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        default:
            return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
}

struct QueueFamilyDesc {
    uint32_t idx;
    uint32_t num;
    VkQueueFlags flags;
    VkVideoCodecOperationFlagsKHR video_caps;
};

struct VulkanContext {
    VkInstance instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debugMessenger{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};

    // Queue families
    uint32_t graphicsQueueFamily{std::numeric_limits<uint32_t>::max()};
    uint32_t computeQueueFamily{std::numeric_limits<uint32_t>::max()};
    uint32_t transferQueueFamily{std::numeric_limits<uint32_t>::max()};
    uint32_t decodeQueueFamily{std::numeric_limits<uint32_t>::max()};

    std::vector<QueueFamilyDesc> queueFamilies; // popolato dedupplicato al device-creation time


    // Queue handles
    VkQueue graphicsQueue{VK_NULL_HANDLE};
    VkQueue computeQueue{VK_NULL_HANDLE};
    VkQueue transferQueue{VK_NULL_HANDLE};
    VkQueue decodeQueue{VK_NULL_HANDLE};

    // Command pools per queue — ognuna ha il suo pool
    VkCommandPool graphicsCommandPool{VK_NULL_HANDLE};
    VkCommandPool computeCommandPool{VK_NULL_HANDLE};
    VkCommandPool transferCommandPool{VK_NULL_HANDLE};
    VkCommandPool decodeCommandPool{VK_NULL_HANDLE};

    uint32_t   encodeQueueFamily { std::numeric_limits<uint32_t>::max() };
    VkQueue    encodeQueue       { VK_NULL_HANDLE };

    VkCommandBuffer master_commandBuffers;
    std::vector<VkCommandBuffer> commandBuffers;
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    VkSampler defaultSampler = VK_NULL_HANDLE;
    VkSampler cubicSampler = VK_NULL_HANDLE;


    //Mixer
    VkDescriptorSetLayout m_mixerDescriptorLayout;

    // Yuv2RGB
    VkDescriptorSetLayout m_yuvToRgbaDescriptorSetLayout;
    VkPipelineLayout m_yuvtorgbComputePipelineLayout;
    VkPipeline m_yuvtorgbComputePipeline;

    /*
    std::mutex graphicsQueueMutex;
    std::mutex computeQueueMutex;
    std::mutex transferQueueMutex;
    std::mutex decodeQueueMutex;
    std::mutex encodeQueueMutex;

    */

    std::array<std::mutex, 32> _queueMutexByFamily;

    std::mutex& queueMutex(uint32_t family) {
        return _queueMutexByFamily[(family < 32u) ? family : 0u];
    }

    // Sostituti drop-in dei vecchi membri (cambiano solo da .xMutex a .xMutexRef()).
    std::mutex& graphicsQueueMutexRef() { return queueMutex(graphicsQueueFamily); }
    std::mutex& computeQueueMutexRef()  { return queueMutex(computeQueueFamily);  }
    std::mutex& transferQueueMutexRef() { return queueMutex(transferQueueFamily); }
    std::mutex& decodeQueueMutexRef()   { return queueMutex(decodeQueueFamily);   }
    std::mutex& encodeQueueMutexRef()   { return queueMutex(encodeQueueFamily);   }

    std::vector<const char*> devExt;

    VkPhysicalDeviceFeatures2 deviceFeatures2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    // Corretto: 8BIT tutto attaccato
    VkPhysicalDevice8BitStorageFeatures eightBitFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES };

    // Anche per le altre, assicurati che i nomi siano esatti:
    VkPhysicalDeviceHostQueryResetFeatures hostQueryResetFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES };
    VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan11Features features11{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceSynchronization2Features sync2Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES };

    VkPhysicalDeviceVideoMaintenance1FeaturesKHR videoMaint1{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_1_FEATURES_KHR };

    // Helper
    bool hasDecodeQueue() const { return decodeQueue != VK_NULL_HANDLE; }
    bool hasDedicatedCompute() const { return computeQueueFamily != graphicsQueueFamily; }
    bool hasDedicatedTransfer() const { return transferQueueFamily != graphicsQueueFamily; }
    bool hasDedicatedDecode() const { return decodeQueueFamily != graphicsQueueFamily; }
    bool hasEncodeQueue() const { return encodeQueue != VK_NULL_HANDLE; }

    bool isNVIDIA = false;
};

struct VulkanTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkImageLayout currentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct MasterResources {
    VulkanTexture image;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
};

struct BusInput {
    VulkanTexture* texture = nullptr; // La texture da disegnare
    float alpha = 1.0f;               // Opacità di questa sorgente nel bus
    float posX = 0, posY = 0;         // Posizione (se vuoi fare cropping/positioning)
    float scaleX = 1.0f, scaleY = 1.0f;
    bool active = true;
};

struct VideoBusResources {
    std::vector<MasterResources> m_master_per_frame;
    std::string busName;
    uint32_t busId;

    std::vector<BusInput> inputs;
};



struct VulkanUniTexture{

    VulkanTexture VkTexture;
#ifdef _WIN32
    HANDLE sharedMemoryHandle = NULL;
#else
    int sharedMemoryFd = -1;
#endif

#ifdef __APPLE__
    GLuint glPbo = 0;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* stagingPtr = nullptr;
#endif


    GLuint glMemoryObject = 0;
    GLuint glTexture = 0;
    VkSemaphore vulkanReadySemaphore = VK_NULL_HANDLE;
    GLuint glSemaphore = 0;
    VkDeviceSize allocationSize = 0;
    uint64_t drmModifier = 0;

};


struct SharedInteropResources {

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

#ifdef _WIN32
    HANDLE sharedMemoryHandle = NULL;
#else
    int sharedMemoryFd = -1;
#endif

#ifdef __APPLE__
    GLuint glPbo = 0;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* stagingPtr = nullptr;
#endif


    GLuint glMemoryObject = 0;
    GLuint glTexture = 0;
    VkSemaphore vulkanReadySemaphore = VK_NULL_HANDLE;
    GLuint glSemaphore = 0;

    uint32_t width = 1920;
    uint32_t height = 1080;
    VkDeviceSize allocationSize = 0;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

};

struct ComputeSync {
    // --- Risorse per la Conversione (Esistenti) ---
    VkSemaphore computeFinished = VK_NULL_HANDLE;
    VkFence     fence           = VK_NULL_HANDLE;
    VkCommandBuffer cmd         = VK_NULL_HANDLE;

    // --- Risorse per la Copia DRM (Nuove) ---
    VkCommandBuffer copyCmd     = VK_NULL_HANDLE;
    VkFence         copyFence   = VK_NULL_HANDLE; // Per sapere quando la copia GPU è finita

    // --- Stato e Sincronizzazione ---
    std::atomic<bool> submitted{false};
    std::atomic<bool> semaphoreSignaled{false};

    // Mutex per proteggere l'intero workflow (Copia + Conversione)
    std::mutex      submitMutex;
};

struct YUVPushConstants {
    uint32_t strideY;
    uint32_t strideU;
    uint32_t strideV;
    uint32_t isNV12;
    uint32_t rangeFull;
    uint32_t width;
    uint32_t height;
    uint32_t padding = 0; // Inizializzato a 0
};

struct YUVComputeResources {

    VkDescriptorSet m_yuvToRgbaDescriptorSet;
    VkDescriptorPool m_yuvToRgbaDescriptorPool;

    VkDescriptorSet m_yuvWriteDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorPool m_yuvWriteDescriptorPool = VK_NULL_HANDLE;

    // --- RAM/Software Path (Buffering) ---
    // Usati quando il decoder è in fallback (es. MPEG-2 Profile 4)
    VkBuffer buffers[3] = { VK_NULL_HANDLE };
    VkDeviceMemory bufferMemories[3] = { VK_NULL_HANDLE };
    void* mappedPtrs[3] = { nullptr };

    // --- GPU/Hardware Path (Zero-Copy) ---
    // Usati per importare FD (Linux) o NT Handles (Windows)
    VkImage hwImages[3] = { VK_NULL_HANDLE };
    VkImageView hwViews[3] = { VK_NULL_HANDLE };
    VkDeviceMemory hwMemories[3] = { VK_NULL_HANDLE };
    uint32_t strides[3] = { 0 };
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t pts = 0;

    int last_imported_fd = -1;
    ComputeSync sync;

    VkImage drmImportImage[2];
    VkDeviceMemory drmImportMemory;


};

struct YUVVideoResources {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    size_t allocationSize = 0;
    uint32_t width = 0, height = 0;
};


#endif //DEJAVIS_APP_RENDER_GLOBALS_H
