#include "renderer.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"

#define VENDOR_ID_NVIDIA 0x10DE
#define VENDOR_ID_AMD    0x1002
#define VENDOR_ID_INTEL  0x8086

const char* yuv_shader_source = R"(
#version 450
#extension GL_EXT_shader_8bit_storage : require

layout (local_size_x = 16, local_size_y = 16) in;

layout (binding = 0, rgba8) uniform readonly image2D masterImage;

layout (std430, binding = 1) writeonly buffer PlaneY { uint8_t y_data[]; };
layout (std430, binding = 2) writeonly buffer PlaneU { uint8_t u_data[]; };
layout (std430, binding = 3) writeonly buffer PlaneV { uint8_t v_data[]; };

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(masterImage);

    if (pos.x >= size.x || pos.y >= size.y) return;

    vec4 rgba = imageLoad(masterImage, pos);

    // Conversione BT.709
    float y = 0.2126 * rgba.r + 0.7152 * rgba.g + 0.0722 * rgba.b;


    uint y_idx = uint(pos.y * size.x + pos.x);
    y_data[y_idx] = uint8_t(uint(clamp(y * 255.0, 0.0, 255.0)));

    if ((pos.x % 2 == 0) && (pos.y % 2 == 0)) {
        float u = -0.1146 * rgba.r - 0.3854 * rgba.g + 0.5000 * rgba.b + 0.5;
        float v = 0.5000 * rgba.r - 0.4187 * rgba.g - 0.0813 * rgba.b + 0.5;

        uint uv_idx = uint((pos.y / 2) * (size.x / 2) + (pos.x / 2));
        u_data[uv_idx] = uint8_t(uint(clamp(u * 255.0, 0.0, 255.0)));
        v_data[uv_idx] = uint8_t(uint(clamp(v * 255.0, 0.0, 255.0)));
    }
}
)";

const char* yuv_to_rgba_shader_source = R"(
#version 450
#extension GL_EXT_shader_8bit_storage : require

layout (local_size_x = 16, local_size_y = 16) in;

// Binding 0: Output RGBA
layout (binding = 0, rgba8) uniform writeonly image2D masterImage;

// --- PATH SOFTWARE (Buffers RAM) ---
layout (std430, binding = 1) readonly buffer Plane0 { uint8_t data0[]; };
layout (std430, binding = 2) readonly buffer Plane1 { uint8_t data1[]; };
layout (std430, binding = 3) readonly buffer Plane2 { uint8_t data2[]; };

layout(push_constant) uniform PushConstants {
    uint strideY;
    uint strideU;
    uint strideV;
    uint isNV12;
    uint rangeFull;
    uint width;
    uint height;
    uint padding; // Porta la struct a 32 byte
} pc;

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

    if (pos.x >= pc.width || pos.y >= pc.height) {
        return;
    }

    float y, u, v;

    // --- LOGICA SOFTWARE (BUFFER RAM) ---
    uint y_idx = pos.y * pc.strideY + pos.x;
    y = float(uint(data0[y_idx])) / 255.0;

    if (pc.isNV12 == 1) {
        // Logica per NV12: U e V sono alternati nel secondo buffer (Plane 1)
        uint uv_row = pos.y >> 1;
        uint uv_col = (pos.x >> 1) << 1;
        uint uv_idx = uv_row * pc.strideU + uv_col;

        u = float(uint(data1[uv_idx])) / 255.0 - 0.5;
        v = float(uint(data1[uv_idx + 1])) / 255.0 - 0.5;
    } else {
        // Logica per YUV420P: 3 buffer separati
        uint u_idx = (pos.y >> 1) * pc.strideU + (pos.x >> 1);
        uint v_idx = (pos.y >> 1) * pc.strideV + (pos.x >> 1);
        u = float(uint(data1[u_idx])) / 255.0 - 0.5;
        v = float(uint(data2[v_idx])) / 255.0 - 0.5;
    }

    // Conversione Limited Range (MPEG) a Full Range se necessario
    if (pc.rangeFull == 0) {
        y = clamp((y - (16.0 / 255.0)) * (255.0 / 219.0), 0.0, 1.0);
        u = clamp(u * (255.0 / 224.0), -0.5, 0.5);
        v = clamp(v * (255.0 / 224.0), -0.5, 0.5);
    }

    // Matrice BT.601 (Standard per SD/HD generico)
    float r = y                  + 1.5748 * v;
    float g = y - 0.1873 * u    - 0.4681 * v;
    float b = y + 1.8556 * u;

    imageStore(masterImage, pos, vec4(clamp(r, 0.0, 1.0),
                                      clamp(g, 0.0, 1.0),
                                      clamp(b, 0.0, 1.0),
                                      1.0));
}

)";


CRenderer::CRenderer() {
    vsync = true;
}

void CRenderer::FetchGPUList(bool _vulkandebug){

    std::vector<const char*> extensions;
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);


#ifdef __APPLE__
    // OBBLIGATORIE per MoltenVK / macOS
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    extensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);

#endif

#ifdef __linux__

    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME); // O XCB se usi X11

    // CAPABILITIES (Instance Extensions)
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    extensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
    extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME); // Aggiungi questa!
#endif


#ifdef _WIN32
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
#endif

    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);



    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.apiVersion = VK_API_VERSION_1_3;
    appInfo.pApplicationName = "Dejavis UI";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Dejavis Engine";

    VkInstanceCreateInfo instInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instInfo.pApplicationInfo = &appInfo;
#ifdef __APPLE__
    // Senza questo flag, vkCreateInstance o glfwCreateWindowSurface falliranno su Mac
    instInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    instInfo.enabledExtensionCount = (uint32_t)extensions.size();
    instInfo.ppEnabledExtensionNames = extensions.data();
//#define NDEBUG
if (_vulkandebug) {
    const char* validationLayers[] = {"VK_LAYER_KHRONOS_validation"};
    instInfo.enabledLayerCount = 1;
    instInfo.ppEnabledLayerNames = validationLayers;
}else {
    instInfo.enabledLayerCount = 0;
}

    /*
#ifdef NDEBUG
    instInfo.enabledLayerCount = 0;
#else
    const char* validationLayers[] = {"VK_LAYER_KHRONOS_validation"};
    instInfo.enabledLayerCount = 1;
    instInfo.ppEnabledLayerNames = validationLayers;
#endif
    */
    bool success = vkCreateInstance(&instInfo, nullptr, &m_ctx.instance) == VK_SUCCESS;
    if(!success){
        DEJAVISUI_LOG_ERROR("Unable to create instance");
    }
    if (_vulkandebug) {
        VulkanLog::Init(m_ctx.instance, m_ctx.device,"DEJAVISUI");
    }
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_ctx.instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        DEJAVISUI_LOG_ERROR("Nessuna GPU con supporto Vulkan trovata!");
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_ctx.instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        gpulist_struct info;
        info.physicalDevice = device;
        // 1. Proprietà base (Nome, ID, Limiti)
        vkGetPhysicalDeviceProperties(device, &info.dev_prop);

        // 2. Proprietà memoria (Calcolo VRAM)
        vkGetPhysicalDeviceMemoryProperties(device, &info.mem_prop);

        VkDeviceSize total_vram = 0;
        for (uint32_t i = 0; i < info.mem_prop.memoryHeapCount; i++) {
            // Cerchiamo l'heap che ha il flag DEVICE_LOCAL (la vera VRAM della GPU)
            if (info.mem_prop.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                total_vram += info.mem_prop.memoryHeaps[i].size;
            }
        }
        info.vramGB = static_cast<double>(total_vram) / (1024.0 * 1024.0 * 1024.0);

        // 3. Info aggiuntive per filtri UI
        info.is_discrete = (info.dev_prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
        info.displayName = info.dev_prop.deviceName;

        // Decodifica versione API (es. 1.2.189)
        info.api_major = VK_VERSION_MAJOR(info.dev_prop.apiVersion);
        info.api_minor = VK_VERSION_MINOR(info.dev_prop.apiVersion);

        gpu_list.push_back(info);
    }

    //vkDestroyInstance(instance, nullptr);
}

void CRenderer::Cleanup_Core() {
    vkDeviceWaitIdle(m_ctx.device);


    for (auto &mv : m_videoBusResources) {
        for (auto& mr : mv.m_master_per_frame) {
            DestroyMasterResources(&mr);
        }
        mv.m_master_per_frame.clear();
    }

    // 2. Pipeline e Layout del Video Mixer
    if (m_mixerPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_ctx.device, m_mixerPipeline, nullptr);
        m_mixerPipeline = VK_NULL_HANDLE;
    }
    if (m_mixerPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_ctx.device, m_mixerPipelineLayout, nullptr);
        m_mixerPipelineLayout = VK_NULL_HANDLE;
    }

    CleanYUV2RGB();
    CleanVideoFX();
    CleanRGB2YUV();

    // 3. Descriptor Pool e Sampler di default
    if (m_ctx.descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_ctx.device, m_ctx.descriptorPool, nullptr);
        m_ctx.descriptorPool = VK_NULL_HANDLE;
    }
    if (m_ctx.defaultSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_ctx.device, m_ctx.defaultSampler, nullptr);
        m_ctx.defaultSampler = VK_NULL_HANDLE;
    }

    if (m_stagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_ctx.device, m_stagingBuffer, nullptr);
        m_stagingBuffer = VK_NULL_HANDLE;
    }
    if (m_stagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_ctx.device, m_stagingMemory, nullptr);
        m_stagingMemory = VK_NULL_HANDLE;
    }

    if (m_ctx.device != VK_NULL_HANDLE) {
        // LIBERAZIONE ESPLICITA DEI BUFFER prima dei pool
        if (!m_ctx.commandBuffers.empty()) {
            vkFreeCommandBuffers(m_ctx.device, m_ctx.graphicsCommandPool,
                                static_cast<uint32_t>(m_ctx.commandBuffers.size()),
                                m_ctx.commandBuffers.data());
            m_ctx.commandBuffers.clear();
        }

        if (m_ctx.graphicsCommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_ctx.device, m_ctx.graphicsCommandPool, nullptr);
            m_ctx.graphicsCommandPool = VK_NULL_HANDLE;
        }
        if (m_ctx.computeCommandPool != VK_NULL_HANDLE && m_ctx.computeCommandPool != m_ctx.graphicsCommandPool) {
            vkDestroyCommandPool(m_ctx.device, m_ctx.computeCommandPool, nullptr);
            m_ctx.computeCommandPool = VK_NULL_HANDLE;
        }
        if (m_ctx.transferCommandPool != VK_NULL_HANDLE && m_ctx.transferCommandPool != m_ctx.graphicsCommandPool) {
            vkDestroyCommandPool(m_ctx.device, m_ctx.transferCommandPool, nullptr);
            m_ctx.transferCommandPool = VK_NULL_HANDLE;
        }
    }

    if (m_ctx.device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_ctx.device, nullptr);
        m_ctx.device = VK_NULL_HANDLE;
    }
    if (m_ctx.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_ctx.instance, nullptr);
        m_ctx.instance = VK_NULL_HANDLE;
    }

    gpu_active = false;
}

bool CRenderer::Init_Core(uint32_t gpuidx, uint32_t _core_w, uint32_t _core_h) {
    core_w = _core_w;
    core_h = _core_h;

    for (int i = 0; i < 10; i++) {
        videoMixerTextures[i].originalIdx = i;
    }
    videoMixerTextures[0].alpha = 0.0f; //FOR WELCOME FADE
    m_ctx.physicalDevice = gpu_list[gpuidx].physicalDevice;

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(m_ctx.physicalDevice, &properties);
    m_ctx.isNVIDIA = (properties.vendorID == VENDOR_ID_NVIDIA);



    DEJAVISUI_LOG_INFO("[CORE] GPU Detected (Vendor ID: 0x%04X): %s",
                       properties.vendorID, properties.deviceName);

    activeGPUname = properties.deviceName;
    if (!m_ctx.physicalDevice) return false;

    // --- 1. Trova queue families ---
    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_ctx.physicalDevice, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> qProps(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_ctx.physicalDevice, &qCount, qProps.data());

    m_ctx.graphicsQueueFamily = std::numeric_limits<uint32_t>::max();
    m_ctx.computeQueueFamily  = std::numeric_limits<uint32_t>::max();
    m_ctx.transferQueueFamily = std::numeric_limits<uint32_t>::max();
    m_ctx.decodeQueueFamily   = std::numeric_limits<uint32_t>::max();
    m_ctx.encodeQueueFamily   = std::numeric_limits<uint32_t>::max();

    for (uint32_t i = 0; i < qCount; i++) {
        VkQueueFlags flags = qProps[i].queueFlags;

        if ((flags & VK_QUEUE_GRAPHICS_BIT) && m_ctx.graphicsQueueFamily == UINT32_MAX)
            m_ctx.graphicsQueueFamily = i;

        if (flags & VK_QUEUE_COMPUTE_BIT) {
            if (!(flags & VK_QUEUE_GRAPHICS_BIT))
                m_ctx.computeQueueFamily = i;
            else if (m_ctx.computeQueueFamily == UINT32_MAX)
                m_ctx.computeQueueFamily = i;
        }

        if (flags & VK_QUEUE_TRANSFER_BIT) {
            if (!(flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)))
                m_ctx.transferQueueFamily = i;
            else if (m_ctx.transferQueueFamily == UINT32_MAX)
                m_ctx.transferQueueFamily = i;
        }

        if ((flags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) && m_ctx.decodeQueueFamily == UINT32_MAX)
            m_ctx.decodeQueueFamily = i;

        if ((flags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) && m_ctx.encodeQueueFamily == UINT32_MAX)
            m_ctx.encodeQueueFamily = i;
    }

    if (m_ctx.graphicsQueueFamily == UINT32_MAX) return false;
    if (m_ctx.computeQueueFamily  == UINT32_MAX) m_ctx.computeQueueFamily  = m_ctx.graphicsQueueFamily;
    if (m_ctx.transferQueueFamily == UINT32_MAX) m_ctx.transferQueueFamily = m_ctx.graphicsQueueFamily;
    DEJAVISUI_LOG_DEBUG("Queue families: graphics=%u compute=%u transfer=%u decode=%u encode0%u",
        m_ctx.graphicsQueueFamily, m_ctx.computeQueueFamily,
        m_ctx.transferQueueFamily, m_ctx.decodeQueueFamily,m_ctx.encodeQueueFamily);

    // --- 2. Queue create infos (solo family uniche) ---
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    std::set<uint32_t> uniqueFamilies = {
        m_ctx.graphicsQueueFamily,
        m_ctx.computeQueueFamily,
        m_ctx.transferQueueFamily
    };
    if (m_ctx.decodeQueueFamily != UINT32_MAX)
        uniqueFamilies.insert(m_ctx.decodeQueueFamily);
    if (m_ctx.encodeQueueFamily != UINT32_MAX)
        uniqueFamilies.insert(m_ctx.encodeQueueFamily);


    float priority = 1.0f;
    float queuePriorities[2] = { 1.0f, 1.0f };
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo qi{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qi.queueFamilyIndex = family;
        qi.queueCount       = 1;
        qi.pQueuePriorities = queuePriorities;
        queueInfos.push_back(qi);
    }

    // --- 3. Estensioni device ---
    // Controlla disponibilità prima di aggiungerle
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(m_ctx.physicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availableExts(extCount);
    vkEnumerateDeviceExtensionProperties(m_ctx.physicalDevice, nullptr, &extCount, availableExts.data());

    auto hasExt = [&](const char* name) {
        return std::any_of(availableExts.begin(), availableExts.end(),
            [name](const VkExtensionProperties& e) {
                return strcmp(e.extensionName, name) == 0;
            });
    };


    VkFormatProperties formatProps;
    vkGetPhysicalDeviceFormatProperties(m_ctx.physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProps);

    m_ctx.devExt.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    m_ctx.devExt.push_back(VK_KHR_8BIT_STORAGE_EXTENSION_NAME);
    m_ctx.devExt.push_back(VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME);
    m_ctx.devExt.push_back(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);


#ifdef _WIN32


    m_ctx.devExt.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
    m_ctx.devExt.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
    m_ctx.devExt.push_back(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
    m_ctx.devExt.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
#endif
#ifdef __linux__
    m_ctx.devExt.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
    m_ctx.devExt.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    m_ctx.devExt.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
    m_ctx.devExt.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
    m_ctx.devExt.push_back(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);

#endif

    const bool haveVideoQueue = hasExt(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);

    // Video decode — solo se disponibili
    bool videoDecodeOk = haveVideoQueue
                      && hasExt(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME)
                      && m_ctx.decodeQueueFamily != UINT32_MAX;

    bool videoEncodeOk = haveVideoQueue
                      && hasExt(VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME)
                      && m_ctx.encodeQueueFamily != UINT32_MAX;

    if (videoDecodeOk) {
        m_ctx.devExt.push_back(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);
        m_ctx.devExt.push_back(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
        if (hasExt(VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME)) {
            m_ctx.devExt.push_back(VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME);
        }
        if (hasExt(VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME)) {
            m_ctx.devExt.push_back(VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME);
        }
        DEJAVISUI_LOG_INFO("[CORE] Vulkan video decode available");
    } else {
        m_ctx.decodeQueueFamily = UINT32_MAX; // forza fallback
        DEJAVISUI_LOG_ERROR("[CORE] Vulkan video decode NOT available, trying other HW API");
    }

    bool encMaint1 = false;
    if (videoEncodeOk) {
        m_ctx.devExt.push_back(VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME);
        if (hasExt(VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME)) {
            DEJAVISUI_LOG_DEBUG("[CORE] Vulkan H264 video encode available");
            m_ctx.devExt.push_back(VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME);
        }
        if (hasExt(VK_KHR_VIDEO_ENCODE_H265_EXTENSION_NAME)) {
            DEJAVISUI_LOG_DEBUG("[CORE] Vulkan HEVC video encode available");
            m_ctx.devExt.push_back(VK_KHR_VIDEO_ENCODE_H265_EXTENSION_NAME);
        }
        if (hasExt(VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME)) {
            m_ctx.devExt.push_back(VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME);
            encMaint1 = true;
        }
        DEJAVISUI_LOG_INFO("[CORE] Vulkan video encode available");
    } else {
        m_ctx.encodeQueueFamily = UINT32_MAX;
        DEJAVISUI_LOG_ERROR("[CORE] Vulkan video encode NOT available, trying other HW API");
    }

    m_ctx.features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    m_ctx.features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    m_ctx.sync2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;

    m_ctx.deviceFeatures2.pNext = &m_ctx.features11;
    m_ctx.features11.pNext = &m_ctx.features12;
    m_ctx.features12.pNext = &m_ctx.sync2Features;
    if (encMaint1) {
        m_ctx.videoMaint1.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_1_FEATURES_KHR;
        m_ctx.videoMaint1.pNext = nullptr;
        m_ctx.sync2Features.pNext = &m_ctx.videoMaint1;
    } else {
        m_ctx.sync2Features.pNext = nullptr;
    }

    // 2. Interroga le capacità del driver
    vkGetPhysicalDeviceFeatures2(m_ctx.physicalDevice, &m_ctx.deviceFeatures2);

    // 3. Abilita esplicitamente ciò che serve dentro le struct corrette
    m_ctx.features12.hostQueryReset = VK_TRUE;
    m_ctx.features12.storageBuffer8BitAccess = VK_TRUE;
    m_ctx.features12.uniformAndStorageBuffer8BitAccess = VK_TRUE;
    m_ctx.features12.timelineSemaphore = VK_TRUE;

    m_ctx.sync2Features.synchronization2 = VK_TRUE;
    m_ctx.deviceFeatures2.features.samplerAnisotropy = VK_TRUE;
    if (encMaint1)
        m_ctx.videoMaint1.videoMaintenance1 = VK_TRUE;

    VkDeviceCreateInfo dInfo{};
    dInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dInfo.pNext                   = &m_ctx.deviceFeatures2;
    dInfo.queueCreateInfoCount    = static_cast<uint32_t>(queueInfos.size());
    dInfo.pQueueCreateInfos       = queueInfos.data();
    dInfo.enabledExtensionCount   = static_cast<uint32_t>(m_ctx.devExt.size());
    dInfo.ppEnabledExtensionNames = m_ctx.devExt.data();

    if (vkCreateDevice(m_ctx.physicalDevice, &dInfo, nullptr, &m_ctx.device) != VK_SUCCESS)
        return false;

    // --- 6. Recupera queue handles ---
    vkGetDeviceQueue(m_ctx.device, m_ctx.graphicsQueueFamily, 0, &m_ctx.graphicsQueue);
    vkGetDeviceQueue(m_ctx.device, m_ctx.computeQueueFamily,  0, &m_ctx.computeQueue);
    vkGetDeviceQueue(m_ctx.device, m_ctx.transferQueueFamily, 0, &m_ctx.transferQueue);
    if (m_ctx.decodeQueueFamily != UINT32_MAX)
        vkGetDeviceQueue(m_ctx.device, m_ctx.decodeQueueFamily, 0, &m_ctx.decodeQueue);
    if (m_ctx.encodeQueueFamily != UINT32_MAX)
        vkGetDeviceQueue(m_ctx.device, m_ctx.encodeQueueFamily, 0, &m_ctx.encodeQueue);

    DEJAVISUI_LOG_DEBUG("Queue families: graphics=%u compute=%u transfer=%u decode=%u encode=%u",
        m_ctx.graphicsQueueFamily, m_ctx.computeQueueFamily, m_ctx.transferQueueFamily,
        m_ctx.decodeQueueFamily, m_ctx.encodeQueueFamily);


#ifdef _WIN32
    fpGetMemoryWin32HandleKHR = (PFN_vkGetMemoryWin32HandleKHR)
        vkGetDeviceProcAddr(m_ctx.device, "vkGetMemoryWin32HandleKHR");
    if (!fpGetMemoryWin32HandleKHR)
        DEJAVISUI_LOG_ERROR("Impossibile caricare vkGetMemoryWin32HandleKHR");
#endif

    // --- 7. Command pools ---
    auto createPool = [&](uint32_t family) -> VkCommandPool {
        VkCommandPool pool;
        VkCommandPoolCreateInfo pi{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pi.queueFamilyIndex = family;
        vkCreateCommandPool(m_ctx.device, &pi, nullptr, &pool);
        return pool;
    };

    m_ctx.graphicsCommandPool = createPool(m_ctx.graphicsQueueFamily);
    m_ctx.computeCommandPool  = m_ctx.hasDedicatedCompute()
                              ? createPool(m_ctx.computeQueueFamily)
                              : m_ctx.graphicsCommandPool;
    m_ctx.transferCommandPool = m_ctx.hasDedicatedTransfer()
                              ? createPool(m_ctx.transferQueueFamily)
                              : m_ctx.graphicsCommandPool;

    // --- 8. Command buffers ---
    m_ctx.commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo cbAlloc{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbAlloc.commandPool        = m_ctx.graphicsCommandPool;
    cbAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = static_cast<uint32_t>(m_ctx.commandBuffers.size());
    vkAllocateCommandBuffers(m_ctx.device, &cbAlloc, m_ctx.commandBuffers.data());


    // NEW BUS CREATION
    m_videoBusResources.resize(2); // Two for now A & B
    m_videoBusResources[0].busId = 0;
    m_videoBusResources[0].busName = "Bus A";
    m_videoBusResources[1].busId = 1;
    m_videoBusResources[1].busName = "Bus B";




    for (auto& mv : m_videoBusResources) {
        DEJAVISUI_LOG_DEBUG("Creating Bus %s Resources",mv.busName.c_str());
        mv.m_master_per_frame.resize(MAX_FRAMES_IN_FLIGHT);
        for (auto& mr : mv.m_master_per_frame) {
            if (!CreateMasterResources(&mr, core_w, core_h)) {
                DEJAVISUI_LOG_ERROR("[RENDERER] Master resource creation failed");
                return false;
            }
        }
    }

    if (!CreateDescriptorPool())                return false;
    if (!CreateDefaultSampler())                return false;


    CreateStagingResources(core_w, core_h);


    InitYUV2RGB();
    InitVideoFX();

    if (!initVideoMixer()) {
        DEJAVISUI_LOG_ERROR("Errore inizializzazione Video Mixer");
        return false;
    }

    VkFenceCreateInfo fInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                             nullptr, VK_FENCE_CREATE_SIGNALED_BIT };
    VkSemaphoreCreateInfo sInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    m_display.inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_display.imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_display.renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateFence(m_ctx.device,     &fInfo, nullptr, &m_display.inFlightFences[i]);
        vkCreateSemaphore(m_ctx.device, &sInfo, nullptr, &m_display.imageAvailableSemaphores[i]);
        vkCreateSemaphore(m_ctx.device, &sInfo, nullptr, &m_display.renderFinishedSemaphores[i]);
    }


    tracy_ctx = TracyVkContext(m_ctx.physicalDevice, m_ctx.device, m_ctx.graphicsQueue, m_ctx.commandBuffers[0]);

    gpu_active = true;
    return true;
}

bool CRenderer::CreateDescriptorPool() {
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 500 }, // Abbondante per texture NDI, icone e anteprime
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 }          // Più che sufficienti per matrici e parametri shader
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // IMPORTANTE
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 500;

    // Opzionale: permette di liberare i singoli set (indispensabile per il mixer dinamico)
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(m_ctx.device, &poolInfo, nullptr, &m_ctx.descriptorPool) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("Fallita creazione Descriptor Pool");
        return false;
    }
    return true;
}

bool CRenderer::CreateDefaultSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR; // Filtro per ingrandimento
    samplerInfo.minFilter = VK_FILTER_LINEAR; // Filtro per rimpicciolimento
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(m_ctx.device, &samplerInfo, nullptr, &m_ctx.defaultSampler) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("Fallita creazione Sampler!");
        return false;
    }
    return true;
}

bool CRenderer::CreateMasterResources(MasterResources* out, uint32_t w, uint32_t h) {
    if (!out) return false;

    // Evitiamo dimensioni zero che causerebbero crash immediati
    if (w == 0 || h == 0) {
        w = 1280; h = 720;
    }

    out->image.width  = w;
    out->image.height = h;

    VkExternalMemoryImageCreateInfo externalInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
#ifdef _WIN32
    externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#endif
    // 1. Creazione Immagine Master (R8G8B8A8_UNORM)
    VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent        = { w, h, 1 };
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_STORAGE_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.pNext         = &externalInfo;

    if (vkCreateImage(m_ctx.device, &imageInfo, nullptr, &out->image.image) != VK_SUCCESS) return false;

    // Allocazione Memoria
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_ctx.device, out->image.image, &memReqs);

    VkMemoryDedicatedAllocateInfo dedicatedInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    dedicatedInfo.image = out->image.image;
    dedicatedInfo.pNext = nullptr;

#ifdef _WIN32
    VkExportMemoryAllocateInfo exportInfo = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
    exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    exportInfo.pNext = &dedicatedInfo;
    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.pNext           = &exportInfo;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(&m_ctx,memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
#else
    VkExportMemoryAllocateInfo exportInfo = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
    exportInfo.pNext = &dedicatedInfo;
    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.pNext           = &exportInfo;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(&m_ctx,memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
#endif
    if (vkAllocateMemory(m_ctx.device, &allocInfo, nullptr, &out->image.memory) != VK_SUCCESS) return false;
    vkBindImageMemory(m_ctx.device, out->image.image, out->image.memory, 0);

    // 2. Render Pass Offscreen
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format         = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    std::array<VkSubpassDependency, 2> dependencies;
    dependencies[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass      = 0;
    dependencies[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass      = 0;
    dependencies[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo rpInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments    = &colorAttachment;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    rpInfo.pDependencies   = dependencies.data();

    if (vkCreateRenderPass(m_ctx.device, &rpInfo, nullptr, &out->renderPass) != VK_SUCCESS) return false;

    // 3. View e Framebuffer
    VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image            = out->image.image;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    if (vkCreateImageView(m_ctx.device, &viewInfo, nullptr, &out->image.view) != VK_SUCCESS) return false;

    VkFramebufferCreateInfo fbInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fbInfo.renderPass      = out->renderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments    = &out->image.view;
    fbInfo.width           = w;
    fbInfo.height          = h;
    fbInfo.layers          = 1;

    if (vkCreateFramebuffer(m_ctx.device, &fbInfo, nullptr, &out->framebuffer) != VK_SUCCESS) return false;

    DEJAVISUI_LOG_DEBUG("[VULKAN] MasterResources create: %dx%d (RP: %p, IMG: %p)",
                        w, h, (void*)out->renderPass, (void*)out->image.image);
    return true;
}

void CRenderer::DestroyMasterResources(MasterResources* out) {
    if (!out) return;

    if (out->framebuffer) { vkDestroyFramebuffer(m_ctx.device, out->framebuffer, nullptr); out->framebuffer = VK_NULL_HANDLE; }
    if (out->image.view)        { vkDestroyImageView(m_ctx.device, out->image.view, nullptr);          out->image.view        = VK_NULL_HANDLE; }
    if (out->renderPass)  { vkDestroyRenderPass(m_ctx.device, out->renderPass, nullptr);   out->renderPass  = VK_NULL_HANDLE; }
    if (out->image.image)       { vkDestroyImage(m_ctx.device, out->image.image, nullptr);             out->image.image       = VK_NULL_HANDLE; }
    if (out->image.memory)      { vkFreeMemory(m_ctx.device, out->image.memory, nullptr);              out->image.memory      = VK_NULL_HANDLE; }

    out->image.width = 0;
    out->image.height = 0;
}

void CRenderer::Render() {
    ZoneScoped;
    VideoMixer_SyncInputs();
    dejatimer.update();

    if (videoMixerTextures[0].isVisible) {
        m_projectm_wrapper->Execute_ProjectM();
    }
        VkCommandBuffer cmd = m_ctx.commandBuffers[m_display.currentFrame ];


    uint32_t syncIndex  = m_display.currentFrame;
    uint32_t imageIndex = 0;

    // 1. Aspettiamo che lo SLOT di sincronizzazione sia libero (CPU aspetta GPU)
    vkWaitForFences(m_ctx.device, 1, &m_display.inFlightFences[m_display.currentFrame], VK_TRUE, UINT64_MAX);

    // 2. Acquisiamo l'immagine dalla swapchain
    // Usiamo il semaforo imageAvailableSemaphores che verrà segnalato quando l'immagine è pronta
    VkResult result = vkAcquireNextImageKHR(m_ctx.device, m_display.swapchain, UINT64_MAX,
                                            m_display.imageAvailableSemaphores[syncIndex],
                                            VK_NULL_HANDLE, &imageIndex);

    // 3. Gestione errori immediata
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapChain();
        return; // Usciamo, la fence non deve essere resettata
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        DEJAVISUI_LOG_ERROR("AcquireNextImage fallita: %d", result);
        return;
    }

    // 4. PROTEZIONE EXTRA: Controlliamo se l'immagine fisica è ancora usata da un vecchio frame
    // m_display.imagesInFlight deve essere un array di VkFence inizializzato a VK_NULL_HANDLE
    if (m_display.imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(m_ctx.device, 1, &m_display.imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }

    // 5. Associamo la fence dello slot corrente all'immagine acquisita
    m_display.imagesInFlight[imageIndex] = m_display.inFlightFences[syncIndex];

    // 6. RESET FENCE: Solo ora che sappiamo di poter procedere con il submit
    vkResetFences(m_ctx.device, 1, &m_display.inFlightFences[syncIndex]);

    // --- Da qui in poi puoi procedere con la registrazione dei Command Buffer e il Submit ---
    // Ricorda di passare 'imageIndex' al tuo framebuffer/renderpass!

    // 5. Selezioniamo il Command Buffer corretto
    cmd = m_ctx.commandBuffers[syncIndex];

    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;


    vkBeginCommandBuffer(cmd, &beginInfo);

    TracyVkCollect(tracy_ctx, cmd);

    {
        TracyVkZone(tracy_ctx, cmd, "PreRender");
        ProcessVideoMixer_PreRenderPass(cmd);
    }

    // --- CICLO DEI BUS VIDEO ---
    for (size_t busIdx = 0; busIdx < m_videoBusResources.size(); ++busIdx) {
        auto& bus = m_videoBusResources[busIdx];
        auto& curBusRes = bus.m_master_per_frame[m_display.currentFrame];

        // 1. Preparazione RenderPass per il bus corrente
        VkRenderPassBeginInfo busRpInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        busRpInfo.renderPass = curBusRes.renderPass;
        busRpInfo.framebuffer = curBusRes.framebuffer;
        busRpInfo.renderArea.extent = { curBusRes.image.width, curBusRes.image.height };
        VkClearValue clearColor = {{{0.00f, 0.00f, 0.00f, 1.0f}}};
        busRpInfo.clearValueCount = 1;
        busRpInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(cmd, &busRpInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{0.0f, 0.0f, (float)curBusRes.image.width, (float)curBusRes.image.height, 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, {curBusRes.image.width, curBusRes.image.height}};
        vkCmdSetScissor(cmd, 0, 1, &scissor);


        {
            TracyVkZone(tracy_ctx, cmd, "Processvideomixer_Bus0");
            ProcessVideoMixer(cmd,busIdx);
        }

        if (busIdx == 0) {    // IMGUI on bus 0;
            {
                ImGui_Render();
                ZoneScopedN("ImGui_Render (CPU)");
            }
            {
                TracyVkZone(tracy_ctx, cmd, "IMGUI RenderDrawData");
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
            }
        }

        vkCmdEndRenderPass(cmd);

        TransitionImageLayout(cmd, curBusRes.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    }

    // L'ultimo bus renderizzato è quello che inviamo allo schermo
    auto& displayBus = m_videoBusResources[display_bus_preview].m_master_per_frame[m_display.currentFrame];
    auto& webrtc_video_Bus = m_videoBusResources[webrtc_bus_preview].m_master_per_frame[m_display.currentFrame];
    auto& spout_video_Bus = m_videoBusResources[spout2_bus_preview].m_master_per_frame[m_display.currentFrame];



#ifdef USE_SPOUT
    if(spout2_sender_active) {
        sender_SPOUT2.SendImage(m_ctx.physicalDevice, m_ctx.device, cmd,
                                spout_video_Bus.image.image,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                spout_video_Bus.image.width, spout_video_Bus.image.height, VK_FORMAT_R8G8B8A8_UNORM);

    }
#endif

    if (AV_ENCODER->isRunning() && AV_ENCODER->HaveServices()) {
        RGB2YUVSlotResources* slot = AV_ENCODER->acquireSlot();
        if (slot) {
            RGB2YUVPipeline::instance().setInputView(*slot, webrtc_video_Bus.image.view);
            m_pending_encoder_slot = slot;
            m_pending_encoder_pts  = AV_ENCODER->peekMasterClockUs();
        } else {
            m_pending_encoder_slot = nullptr;
        }
    }

    if(glfw_active) {
        TracyVkZone(tracy_ctx, cmd, "BlitToSwapchain");

        VkImageBlit blitRegion{};
        blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = {(int32_t) displayBus.image.width, (int32_t) displayBus.image.height, 1};

        blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blitRegion.dstOffsets[0] = { (int32_t)m_display.viewport.x, (int32_t)m_display.viewport.y, 0 };
        blitRegion.dstOffsets[1] = { (int32_t)(m_display.viewport.x + m_display.viewport.width), (int32_t)(m_display.viewport.y + m_display.viewport.height), 1 };

        TransitionImageLayout_RAW(cmd, m_display.swapchainImages[imageIndex],
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vkCmdBlitImage(cmd,
                       displayBus.image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       m_display.swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blitRegion, VK_FILTER_LINEAR);

        TransitionImageLayout_RAW(cmd, m_display.swapchainImages[imageIndex],
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        // Riporta l'immagine dell'ultimo bus al layout di partenza per il prossimo frame
        TransitionImageLayout(cmd, displayBus.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }

    if (m_pending_encoder_slot) {
        TransitionImageLayout(cmd, displayBus.image, VK_IMAGE_LAYOUT_GENERAL);
    } else {
        TransitionImageLayout(cmd, displayBus.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }

    vkEndCommandBuffer(cmd);

    /*

    auto& cur = m_videoBusResources[0].m_master_per_frame[m_display.currentFrame];
    if (m_ctx.device == VK_NULL_HANDLE) { printf("DEBUG: device_null\r\n"); return; }
    VkCommandBuffer cmd = m_ctx.commandBuffers[m_display.currentFrame ];


    uint32_t syncIndex  = m_display.currentFrame;
    uint32_t imageIndex = 0;

    // 1. Aspettiamo che lo SLOT di sincronizzazione sia libero (CPU aspetta GPU)
    vkWaitForFences(m_ctx.device, 1, &m_display.inFlightFences[m_display.currentFrame], VK_TRUE, UINT64_MAX);

    // 2. Acquisiamo l'immagine dalla swapchain
    // Usiamo il semaforo imageAvailableSemaphores che verrà segnalato quando l'immagine è pronta
    VkResult result = vkAcquireNextImageKHR(m_ctx.device, m_display.swapchain, UINT64_MAX,
                                            m_display.imageAvailableSemaphores[syncIndex],
                                            VK_NULL_HANDLE, &imageIndex);

    // 3. Gestione errori immediata
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapChain();
        return; // Usciamo, la fence non deve essere resettata
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        DEJAVISUI_LOG_ERROR("AcquireNextImage fallita: %d", result);
        return;
    }

    // 4. PROTEZIONE EXTRA: Controlliamo se l'immagine fisica è ancora usata da un vecchio frame
    // m_display.imagesInFlight deve essere un array di VkFence inizializzato a VK_NULL_HANDLE
    if (m_display.imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(m_ctx.device, 1, &m_display.imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }

    // 5. Associamo la fence dello slot corrente all'immagine acquisita
    m_display.imagesInFlight[imageIndex] = m_display.inFlightFences[syncIndex];

    // 6. RESET FENCE: Solo ora che sappiamo di poter procedere con il submit
    vkResetFences(m_ctx.device, 1, &m_display.inFlightFences[syncIndex]);

    // --- Da qui in poi puoi procedere con la registrazione dei Command Buffer e il Submit ---
    // Ricorda di passare 'imageIndex' al tuo framebuffer/renderpass!

    // 5. Selezioniamo il Command Buffer corretto
    cmd = m_ctx.commandBuffers[syncIndex];

    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);

    TracyVkCollect(tracy_ctx, cmd);

    {
        TracyVkZone(tracy_ctx, cmd, "PreRender");
        ProcessVideoMixer_PreRenderPass(cmd);
    }

#ifdef __APPLE__

    TransitionImageLayout(cmd, videoTextures[0].VkTexture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // B. La copia effettiva
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { (uint32_t)videoTextures[0].VkTexture.width, (uint32_t)videoTextures[0].VkTexture.height, 1 };

    vkCmdCopyBufferToImage(cmd, videoTextures[0].stagingBuffer, videoTextures[0].VkTexture.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // C. Transizione a SHADER_READ_ONLY (per essere disegnata)
    TransitionImageLayout(cmd, videoTextures[0].VkTexture,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);


#else
    //TransitionImageLayout(cmd, videoTextures[0].VkTexture,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    TransitionImageLayout(cmd, videoTextures[0].VkTexture,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
#endif



    VkRenderPassBeginInfo masterRpInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    masterRpInfo.renderPass = cur.renderPass;
    masterRpInfo.framebuffer = cur.framebuffer;
    masterRpInfo.renderArea.extent = { cur.image.width, cur.image.height };
    VkClearValue clearColor = {{{0.00f, 0.00f, 0.0f, 1.0f}}};
    masterRpInfo.clearValueCount = 1;
    masterRpInfo.pClearValues = &clearColor;


    vkCmdBeginRenderPass(cmd, &masterRpInfo, VK_SUBPASS_CONTENTS_INLINE);



    VkViewport viewport{0.0f, 0.0f, (float)cur.image.width, (float)cur.image.height, 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, {cur.image.width, cur.image.height}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);


    {
        ImGui_Render();
        ZoneScopedN("ImGui_Render (CPU)");
    }

    {
        TracyVkZone(tracy_ctx, cmd, "Processvideomixer");
        ProcessVideoMixer(cmd);
    }

    {
        TracyVkZone(tracy_ctx, cmd, "IMGUI RenderDrawData");

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    }



    vkCmdEndRenderPass(cmd);
    cur.image.currentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    TransitionImageLayout(cmd,cur.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);


#ifdef USE_SPOUT
    if(spout2_sender_active) {
        sender_SPOUT2.SendImage(m_ctx.physicalDevice, m_ctx.device, cmd,
                                cur.image.image,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                cur.image.width, cur.image.height, VK_FORMAT_R8G8B8A8_UNORM);

    }
#endif





    if (AV_ENCODER->isRunning() && AV_ENCODER->HaveServices()) {
        RGB2YUVSlotResources* slot = AV_ENCODER->acquireSlot();
        if (slot) {
            RGB2YUVPipeline::instance().setInputView(*slot, cur.image.view);
            m_pending_encoder_slot = slot;

            m_pending_encoder_pts  = AV_ENCODER->peekMasterClockUs();
        } else {
            m_pending_encoder_slot = nullptr;
        }
    }
    //TracyVkZone(tracy_ctx, cmd, "AV ENCODER");
    if(glfw_active) {
        TracyVkZone(tracy_ctx, cmd, "BlitToSwapchain");
// --- 5. PREPARAZIONE MASTER PER COPIA (Una sola volta!) ---

        //TransitionImageLayout(cmd, m_master.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkImageBlit blitRegion{};
        // Sorgente: l'intera immagine master
        blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = {(int32_t) cur.image.width, (int32_t) cur.image.height, 1};

        // Destinazione: Usiamo i valori calcolati nella struct m_display per l'aspetto corretto
        blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};

        // Offset 0: l'angolo in alto a sinistra della nostra "area utile" (include x e y calcolati)
        blitRegion.dstOffsets[0] = {
            (int32_t)m_display.viewport.x,
            (int32_t)m_display.viewport.y,
            0
        };

        // Offset 1: l'angolo in basso a destra della nostra "area utile"
        blitRegion.dstOffsets[1] = {
            (int32_t)(m_display.viewport.x + m_display.viewport.width),
            (int32_t)(m_display.viewport.y + m_display.viewport.height),
            1
        };

        TransitionImageLayout_RAW(cmd, m_display.swapchainImages[imageIndex],
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdBlitImage(cmd,
                       cur.image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       m_display.swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blitRegion, VK_FILTER_LINEAR);

        TransitionImageLayout_RAW(cmd, m_display.swapchainImages[imageIndex],
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR); // dst: prima del present
        TransitionImageLayout(cmd, cur.image,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }

    if (m_pending_encoder_slot) {
        TransitionImageLayout(cmd, cur.image,
                              VK_IMAGE_LAYOUT_GENERAL);
    } else {
        TransitionImageLayout(cmd, cur.image,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }


    vkEndCommandBuffer(cmd);
*/
    if (glfw_active) {
        GLFW_Vulkan_Submit(cmd, imageIndex, syncIndex);
        //m_display.currentFrame = (m_display.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }
    LimitFrameRate();
    FrameMark;
}

bool CRenderer::CreateStagingResources(uint32_t w, uint32_t h) {
    VkDeviceSize imageSize = w * h * 4; // RGBA = 4 byte per pixel

    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_ctx.device, &bufferInfo, nullptr, &m_stagingBuffer) != VK_SUCCESS) return false;

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(m_ctx.device, m_stagingBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReqs.size;
    // Usiamo HOST_VISIBLE per poterlo leggere con la CPU
    allocInfo.memoryTypeIndex = FindMemoryType(&m_ctx,memReqs.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(m_ctx.device, &allocInfo, nullptr, &m_stagingMemory) != VK_SUCCESS) return false;
    vkBindBufferMemory(m_ctx.device, m_stagingBuffer, m_stagingMemory, 0);

    // Mappiamo la memoria una volta sola per massime prestazioni
    vkMapMemory(m_ctx.device, m_stagingMemory, 0, imageSize, 0, &m_mappedData);
    return true;
}

void CRenderer::CaptureToCPU(VkCommandBuffer cmd, uint32_t w, uint32_t h) {
    // 1. Transizione immagine a TRANSFER_SRC
    auto& cur = m_videoBusResources[0].m_master_per_frame[m_display.currentFrame];

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = m_videoBusResources[0].m_master_per_frame[0].image.image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // 2. Copia Immagine -> Buffer
    VkBufferImageCopy region = {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { w, h, 1 };

    vkCmdCopyImageToBuffer(cmd, cur.image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_stagingBuffer, 1, &region);
/*
    // 3. Riporta l'immagine a COLOR_ATTACHMENT per il prossimo frame
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    */
 }

void CRenderer::CleanVideoFX() {
    VideoFXPipeline::instance().shutdown();
}

void CRenderer::InitVideoFX() {
    if (!VideoFXPipeline::instance().init(&m_ctx)) {
        DEJAVISUI_LOG_ERROR("[VideoFX Pipeline] init failed");
    }
}

void CRenderer::CleanYUV2RGB()
{
    YUV2RGBPipeline::instance().shutdown();
}


void CRenderer::InitYUV2RGB() {
    if (!YUV2RGBPipeline::instance().init(&m_ctx)) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB Pipeline0] init failed");
    }
}

void CRenderer::CleanRGB2YUV()
{
    RGB2YUVPipeline::instance().shutdown();
}


void CRenderer::InitRGB2YUV() {
    if (!RGB2YUVPipeline::instance().init(&m_ctx)) {
        DEJAVISUI_LOG_ERROR("[YUV2RGB Pipeline0] init failed");
    }
    for (size_t i = 0; i < AV_ENCODER->getSlotCount(); ++i) {
        auto& slot = AV_ENCODER->getSlot(i);
        RGB2YUVFormat fmt =
#ifdef __APPLE__
    RGB2YUVFormat::YUV420P;
#else
        RGB2YUVFormat::NV12;
#endif
        RGB2YUVPipeline::instance().createSlot(
            slot, core_w, core_h, fmt,
            m_videoBusResources[0].m_master_per_frame[0].image.view);
    }
}


static void ff_vk_lock_queue(AVHWDeviceContext* c, uint32_t qf, uint32_t){
    static_cast<VulkanContext*>(c->user_opaque)->queueMutex(qf).lock();
}

static void ff_vk_unlock_queue(AVHWDeviceContext* c, uint32_t qf, uint32_t){
    static_cast<VulkanContext*>(c->user_opaque)->queueMutex(qf).unlock();
}

AVBufferRef* CRenderer::CreateFFmpegVulkanHWContext()
{
    if (m_ctx.device == VK_NULL_HANDLE) return nullptr;

    AVBufferRef* hw_device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
    if (!hw_device_ctx) return nullptr;

    AVHWDeviceContext* devCtx = reinterpret_cast<AVHWDeviceContext*>(hw_device_ctx->data);
    AVVulkanDeviceContext* vkCtx  = reinterpret_cast<AVVulkanDeviceContext*>(devCtx->hwctx);

    devCtx->user_opaque = &m_ctx;
    vkCtx->get_proc_addr = vkGetInstanceProcAddr;
    vkCtx->inst          = m_ctx.instance;
    vkCtx->phys_dev      = m_ctx.physicalDevice;
    vkCtx->act_dev       = m_ctx.device;
    vkCtx->device_features = m_ctx.deviceFeatures2;

    vkCtx->enabled_inst_extensions    = nullptr;
    vkCtx->nb_enabled_inst_extensions = 0;
    vkCtx->enabled_dev_extensions     = m_ctx.devExt.data();
    vkCtx->nb_enabled_dev_extensions  = (int)m_ctx.devExt.size();

    #if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(59, 34, 100)
    int n = 0;
    auto addQF = [&](uint32_t idx, int num, VkQueueFlagBits flags,
                     VkVideoCodecOperationFlagBitsKHR vcaps)
    {
        if (idx == std::numeric_limits<uint32_t>::max() || n >= 64) return;
        vkCtx->qf[n].idx        = (int)idx;
        vkCtx->qf[n].num        = num;          // 1 queue per family
        vkCtx->qf[n].flags      = flags;
        vkCtx->qf[n].video_caps = vcaps;
        ++n;
    };


    addQF(m_ctx.graphicsQueueFamily, 1, VK_QUEUE_GRAPHICS_BIT,
          (VkVideoCodecOperationFlagBitsKHR)0);
    addQF(m_ctx.computeQueueFamily,  1, VK_QUEUE_COMPUTE_BIT,
          (VkVideoCodecOperationFlagBitsKHR)0);
    addQF(m_ctx.transferQueueFamily, 1, VK_QUEUE_TRANSFER_BIT,
          (VkVideoCodecOperationFlagBitsKHR)0);
    addQF(m_ctx.decodeQueueFamily,   1, VK_QUEUE_VIDEO_DECODE_BIT_KHR,
          (VkVideoCodecOperationFlagBitsKHR)(
              VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR |
              VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR|VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR));
    addQF(m_ctx.encodeQueueFamily,   1, VK_QUEUE_VIDEO_ENCODE_BIT_KHR,
          (VkVideoCodecOperationFlagBitsKHR)(
              VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR |
              VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR));
    vkCtx->nb_qf = n;

    // --- Campi deprecati: la struct e' zero-init, ma 0 e' un indice VALIDO,
    //     quindi su versioni transitorie va riempito esplicitamente per non far
    //     credere a FFmpeg che la family 0 sia tx/comp/decode.
#else
    vkCtx->queue_family_index        = (int)m_ctx.graphicsQueueFamily; vkCtx->nb_graphics_queues = 1;
    vkCtx->queue_family_tx_index     = (int)m_ctx.transferQueueFamily; vkCtx->nb_tx_queues       = 1;
    vkCtx->queue_family_comp_index   = (int)m_ctx.computeQueueFamily;  vkCtx->nb_comp_queues     = 1;
    vkCtx->queue_family_decode_index = -1;
    vkCtx->nb_decode_queues   = 0;
    if (m_ctx->decodeQueueFamily != std::numeric_limits<uint32_t>::max()) {
        vkCtx->queue_family_decode_index = (int)m_ctx.decodeQueueFamily;
        vkCtx->nb_decode_queues          = 1;
    } else {
        vkCtx->queue_family_decode_index = -1;
        vkCtx->nb_decode_queues          = 0;
    }
    if (m_ctx->encodeQueueFamily != std::numeric_limits<uint32_t>::max()) {
        vkCtx->queue_family_encode_index = (int)m_ctx.encodeQueueFamily;
        vkCtx->nb_encode_queues          = 1;
    } else {
        vkCtx->queue_family_encode_index = -1;
        vkCtx->nb_encode_queues          = 0;
    }

    vkCtx->lock_queue   = ff_vk_lock_queue;
    vkCtx->unlock_queue = ff_vk_unlock_queue;

#endif



    if (av_hwdevice_ctx_init(hw_device_ctx) < 0) {
        av_buffer_unref(&hw_device_ctx);
        return nullptr;
    }

    return hw_device_ctx; // Restituisce il riferimento pronto
}