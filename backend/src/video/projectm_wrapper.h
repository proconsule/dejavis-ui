#ifndef DEJAVIS_UI_PROJECTM_WRAPPER_H
#define DEJAVIS_UI_PROJECTM_WRAPPER_H

#include <string>
#ifdef _WIN32
  #include <cstddef>
  #define _HAS_STD_BYTE 0
#endif

#include <glad/glad.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "backend/src/ndi_receiver.h"
#include "backend/src/ndi_sender.h"
#ifdef __APPLE__
#include <vulkan/vulkan_metal.h>
#endif
#include <vector>
#include <cmath>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>
#include <array>
#include <unistd.h>
#include <json/json.h>
#include "../dejatimer.h"

#define DEJAVIS_USE_VULKAN 1
#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif
#ifdef __linux__
#define VK_USE_PLATFORM_WAYLAND_KHR
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan_wayland.h>
#endif

#ifdef  USE_SPOUT
#include "../SpoutVK.h"
#include "SpoutDX/SpoutDX.h"
#include "SpoutDX/SpoutDirectX.h"
#endif


#include "render_globals.h"
#include "../logger.h"
#include "../vulkan_logger.h"

#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>

#include "vulkan_utils.h"

#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>

#include "../db/milkplaylistdb.h"

typedef struct {
    std::string name;
    int id;

}wrapperpreset_status_struct;

class cprojectm_wrapper {
public:
    void Init(VulkanContext *_ctx, VulkanUniTexture * _tex, uint32_t _width, uint32_t _height);

    void Init_ProjectM_Opengl();

    void InitInteropResource();

    void InitInteropOpenGL();

    void Execute_ProjectM();

    void PostProcessInit();


    void setKeyer(FxKeyerMode _keyer) {
        if (m_postProcessor) {
            m_postProcessor->setEnabled(true);
            m_postProcessor->setKeyerMode(_keyer);
        }
    }

    void setLumaKey(LumaKeyParams &myparams) {
        if (m_postProcessor) {
            m_postProcessor->setEnabled(true);
            m_postProcessor->setLumaParams(myparams);
        }
    }

    void setChromaKey(KeyerPushConstants &_mycroma) {
        if (m_postProcessor) {
            m_postProcessor->setEnabled(true);
            m_postProcessor->setChromaParams(_mycroma);
        }
    }

    void setColor(ColorParams &col) {
        if (m_postProcessor) {
            m_postProcessor->setEnabled(true);
            m_postProcessor->setColorParams(col);
        }
    }

    VulkanUniTexture * getOutputTexture() const {
        return outText;
    }

    VkDescriptorSet getMixerDescriptorSet() const {
        return m_postProcessor->getOutputDescriptorSet();
    }

    CPostProcessor* getPostProcessor() { return m_postProcessor.get(); }


    bool m_shouldLoadPresetData = false;
    int m_shouldLoadPresetID = -1;
    std::string m_presetDataToLoad = "";
    std::string m_presetDataToLoadOrigFile = "";


    //std::string currentpreset = "projectM IDLE Preset";

    void PushAudio(const float* const* input, int in_samples);


    projectm_handle  getprojectM_Handle() {
        return _projectM;
    }

    Json::Value getStatusJson();

    wrapperpreset_status_struct preset_status;

private:
    GLFWwindow* m_glContext = nullptr;
    VulkanContext *m_ctx = nullptr;
    GLuint m_glFbo;

    VulkanUniTexture * outText = nullptr;

    uint32_t pjm_width = 0;
    uint32_t pjm_height = 0;

    projectm_handle _projectM{nullptr};
    projectm_playlist_handle _playlist{nullptr};


    std::unique_ptr<CPostProcessor> m_postProcessor;
    bool m_fxEnabled = false;

    std::mutex m_mixer_mutex;
    cmilkplaylistdb *milkdb = nullptr;


#ifdef WIN32
    PFN_vkGetMemoryWin32HandleKHR fpGetMemoryWin32HandleKHR = nullptr;
#endif

};


#endif //DEJAVIS_UI_PROJECTM_WRAPPER_H