#ifndef DEJAVISRENDERER_H
#define DEJAVISRENDERER_H

#ifdef _WIN32
  #include <cstddef> // Carica std::byte
  #define _HAS_STD_BYTE 0
#endif

#include <glad/glad.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "projectm_wrapper.h"
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
#include <vulkan/vulkan_wayland.h>
#endif

#ifdef  USE_SPOUT
#include "../SpoutVK.h"
#include "SpoutDX/SpoutDX.h"
#include "SpoutDX/SpoutDirectX.h"
#endif

#include "../av_encoder.h"
#include "../audio/audio.h"
#include "../image_viewer.h"


#include <shared_mutex>

#include "render_globals.h"
#include <shaderc/shaderc.hpp>
#include "../logger.h"
#include "../vulkan_logger.h"

#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>
#include "../av_decoder.h"
#include "imgui.h"

#include "vulkan_utils.h"

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>



const int MAX_FRAMES_IN_FLIGHT = 2;

struct Vulkan_ShaderCompileResult {
    bool success;
    std::string errorMessage;
    uint32_t complexity;
};

struct videomixeritem {
    float pos_x = 0.0f, pos_y = 0.0f, scale_x = 1.0f, scale_y = 1.0f, alpha = 1.0f;
    bool y_flip = false;
	bool useLanczos = false;
    bool inUse = false;
	bool isVisible = false;
    int type = -1;
    int layer = -1;
    int originalIdx = -1;
	int audiomixerid = 1;
	bool keepaspect = true;
	bool anime4k = true;
	CAV_DECODER *AV_DECODER = nullptr;
	CAV_DECODER *AV_STREAM_DECODER = nullptr;
	cimage_viewer *img_viewver = nullptr;
	CNDIReceiver *ndi_receiver = nullptr;
	KeyerPushConstants chromaParams;
	LumaKeyParams lumaParams;
	ColorParams     colorParams;
	FxKeyerMode keyerMode = FxKeyerMode::Chroma; //chroma

};

typedef struct{
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceProperties dev_prop;
    VkPhysicalDeviceMemoryProperties mem_prop; // Necessaria per calcolare la VRAM reale

    double vramGB = 0.0;
    bool is_discrete = false;
    uint32_t api_major = 0;
    uint32_t api_minor = 0;
    std::string displayName;
}gpulist_struct;

struct PendingImageLoad {
	std::atomic<bool> shouldLoad{false};
	std::string url;
	unsigned char * data = nullptr;
	unsigned data_size = 0;
	bool data_isinit = false;
	int mixerid = 0;

};

struct PendingImageUnLoad {
	std::atomic<bool> shouldUnLoad{false};
	std::string url;
	int mixerid = 0;

};

struct PendingVideoLoad {
	std::atomic<bool> shouldLoad{false};
	std::string url;
	int mixerid = 0;

};

struct PendingVideoUnLoad {
	std::atomic<bool> shouldUnLoad{false};
	int mixerid = 0;
};

struct PendingVideoUrlLoad {
	std::atomic<bool> shouldLoad{false};
	std::string url;
	int mixerid = 0;

};

struct PendingVideoUrlUnLoad {
	std::atomic<bool> shouldUnLoad{false};
	int mixerid = 0;
};

using clock_type = std::chrono::steady_clock;



class CRenderer{
public:
	CRenderer();


    bool Init_Core(uint32_t gpuidx,uint32_t _core_w,uint32_t _core_h);
	void Cleanup_Core();
    bool gpu_active = false;
    uint32_t gpu_acvtive_idx = 0;
    void FetchGPUList(bool _vulkandebug);
    void Print_GPU_List();
    bool CreateVulkanDevice(uint32_t gpuIdx);
    bool CreateDescriptorPool();
    bool CreateDefaultSampler();
    uint32_t core_w;
    uint32_t core_h;

	std::string fileplayers_basepath = "";

	cprojectm_wrapper  * m_projectm_wrapper = nullptr;


// SDL2 PART
    bool Init_SDL_Window(uint32_t _w,uint32_t _h);
    bool CreateFences();

    std::atomic<bool> fullscreen = false;
    //SDL_Window* sdl_window = nullptr;
    uint32_t window_w;
    uint32_t window_h;
    //bool sdl2_active = false;
    bool glfw_active = false;


    GLFWwindow * glfw_window;

    bool CreateSwapChainOnly();
    bool RecreateSwapChain();
    void CleanupSwapChain();
    void CleanupSDL();


	void CleanVideoFX();
	void InitVideoFX();

	void CleanYUV2RGB();
	void InitYUV2RGB();

	void CleanRGB2YUV();
	void InitRGB2YUV();

    bool Init_GLFW_Window(uint32_t _w, uint32_t _h);
    void GLFW_Vulkan_Submit(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t syncIndex);
    void SetFullScreen(bool _val,bool _reschange = false);
    int win_pos_x = 0;
    int win_pos_y = 0;

    std::vector<VulkanUniTexture> videoTextures{10};
    std::vector<videomixeritem> videoMixerTextures{10};


	TracyVkCtx tracy_ctx;

    bool initVideoMixer();
    void drawTestOverlay(VkCommandBuffer cmd, VkDescriptorSet textureSet);
	void drawMixerVideoLayer(VkCommandBuffer cmd,videomixeritem *_mixerprop,VulkanUniTexture &_texture);

    void drawVideoLayer(VkCommandBuffer cmd, VkDescriptorSet textureSet,
                               float x, float y, float scaleX, float scaleY,
                               float alpha, bool yFlip,bool useLanczos);

    void ProcessVideoMixer(VkCommandBuffer cmd);
	void ProcessVideoMixer_PreRenderPass(VkCommandBuffer cmd);

    int FindFreeVideoMixerSlot();
    bool AddImageToMixer(const unsigned char* img_data,int img_size,bool isHDR);
    void RemoveImageFromMixer(int slot);

    void SetVideoMixerProps(videomixeritem &prop,int _mixerid);
    bool AddAVDecoderToMixer(std::string url, int _mixerid);

	bool AddNDIToMixer(int _mixerid);

	void RemoveNDIFromVideoMixerID(int _video_mixer_id);

	void RemoveNDIFromAudioMixerID(int _audio_mixer_id);

	void RemoveNDIFromVideoMixer(int _audio_mixer_id);

	void RemoveNDIFromMixer(int _audio_mixer_id);

	int AddVideoFilePlayerToMixer(std::string _path,int _audio_mixer_id);
	void RemoveVideoFilePlayerFromMixer(int _audio_mixer_id);

	int AddVideoURLPlayerToMixer(std::string _path,int _audio_mixer_id);
	void RemoveVideoURLPlayerFromMixer(int _audio_mixer_id);

	PendingVideoLoad m_pendingLoad;
	PendingImageLoad m_pendingImageLoad;
	PendingImageUnLoad m_pendingImageUnLoad;
	PendingVideoLoad m_pendingInputLoad;
	PendingVideoUnLoad m_peningUnload;
	PendingVideoUrlLoad m_pendingVideoUrlload;
	PendingVideoUrlUnLoad m_pendingVideoUrlunload;


	PendingNDILoad m_pendingNDI;
	PendingNDIUnLoad m_pendingNDI_Unload;
	PendingNDILoad m_pendingNDISource;

	std::mutex m_videoMixerMutex;


	void SetLumaKey(int _mixeridx,LumaKeyParams &params);
	void SetChromaKey(int _mixeridx,KeyerPushConstants &params);
	void SetColor(int _mixeridx,ColorParams &params);

	void VideoMixer_SyncInputs();

	void SetKeyer(int _mixeridx,FxKeyerMode _keyer);

    videomixeritem GetTestVideoMixer();
    Json::Value GetVideoMixerJson();

    VkDescriptorSet createTextureDescriptor(VkImageView imageView);

    VkPipelineLayout      m_mixerPipelineLayout   = VK_NULL_HANDLE;
    VkPipeline            m_mixerPipeline         = VK_NULL_HANDLE;

    //SPOUT2
#ifdef SPOUT2_ENABLED
    spoutVK sender_SPOUT2;
    void Init_SPOUT2();
    void Close_SPOUT2();
#else
    void Init_SPOUT2(){};
    void Close_SPOUT2(){};
#endif

    std::atomic<bool> spout2_sender_active = false;


	bool PopulateEncoderSlots();

	bool CreateYUVDescriptorSetForSlot(YUVComputeResources& s);

	void DestroyYUVSlot(YUVComputeResources& s);

	RGB2YUVSlotResources* m_pending_encoder_slot;
	int64_t              m_pending_encoder_pts  = 0;

	bool CreateYUVResources(YUVComputeResources * yuvcompute,uint32_t w, uint32_t h);
    CAV_ENCODER *AV_ENCODER;

	bool InitComputeSync(YUVComputeResources& yuv);

    VkBuffer m_stagingBuffer;
    VkDeviceMemory m_stagingMemory;
    void* m_mappedData = nullptr;
    bool CreateStagingResources(uint32_t w, uint32_t h);
    void CaptureToCPU(VkCommandBuffer cmd, uint32_t w, uint32_t h);
    bool InitYUVComputePipeline();
	bool InitYUVToRGBAComputePipeline();
    //VkPipeline m_yuvComputePipeline;
    VkPipelineLayout m_yuvPipelineLayout;
    VkDescriptorSetLayout m_yuvDescriptorLayout;
    VkDescriptorSet m_yuvDescriptorSet;
    VkDescriptorPool m_yuvDescriptorPool;

	void RecordYUVConversionCommand(VkCommandBuffer cmd,YUVComputeResources& s);
	void RecordYUVToRGBAConversionCommand(VkCommandBuffer cmd,YUVComputeResources &yuvcompute, VulkanUniTexture& texture);
    VulkanContext m_ctx;

	std::vector<MasterResources> m_master_per_frame;

	Vulkan_DisplayContext m_display;

    VkShaderModule CreateShaderModule(const uint32_t* code, size_t sizeInBytes);

	void Render();

    void Init_ImGui();
	bool InitImGuiDescriptorPool();
    void ImGui_Render();
    void GUI_Render();

    ImFont* fontMarquee;

    void GUI_Marquee(std::string _id,ImVec2 pos,std::string text,int font_size = 16);

    VkDescriptorPool imguiPool;

    projectm_handle _projectM{nullptr};
    projectm_playlist_handle _playlist{nullptr};
    void initProjectM(const std::string& presetPath, int width, int height);
    void Init_ProjectM_Opengl(const std::string& presetPath);
    void InitInteropResource(uint32_t w, uint32_t h);
    void InitInteropOpenGL();

	void Execute_ProjectM();

	bool m_shouldLoadPresetFile = false;
    std::string m_presetFileToLoad = "";

    bool m_shouldLoadPresetData = false;
    std::string m_presetDataToLoad = "";
    std::string m_presetDataToLoadOrigFile = "";



    GLFWwindow* m_glContext;
    GLuint m_glFbo;

	bool CreateMasterResources(MasterResources* out, uint32_t w, uint32_t h);
	void DestroyMasterResources(MasterResources* out);

    void ImageBarrier(VkCommandBuffer cmd, VkImage image,
                             VkImageLayout oldLayout, VkImageLayout newLayout,
                             VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                             VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) ;

    float deltaTime = 0.0;
    float currentFps = 0.0;
	bool running;
	uint32_t framecount = 0;

    std::atomic<bool> vsync = false;
    std::atomic<bool> framelimiter = false;
    std::atomic<int> fpstarget = 60;
    std::atomic<float> fpstargetMS = 16.66;

	std::vector<gpulist_struct > get_gpu_list(){
		return gpu_list;
	}
	Json::Value getRendererStatusJson();
    Json::Value getVisualDJJson();
    void updateVisualDjFromJSON(const Json::Value& root);


    void SetFrameLimit(uint32_t _targetFPS);
	void LimitFrameRate();


    bool vulkanReady = false;
    std::atomic<bool> deinit = false;
    std::atomic<bool> reinit = false;
    std::atomic<uint32_t> reinit_gpuidx = -1;
    void FullShutdown();

    void SetAudioEngine(CAudio* audio) { m_audio = audio; }



    void UpdateCustomShader(std::string shadercode);
    std::string GetCustomShader();
    Vulkan_ShaderCompileResult EvaluateCustomShader(std::string shadercode);


    void Get_Analysis_Buffer(float* out_buffer);
    float* analysis_buffer = new float[2052];
    void Update_Analysis_Buffer() ;
    mutable std::shared_mutex analysis_buffer_mutex;

    void CleanupTexture(VulkanTexture& tex);
    VulkanTexture Vulkan_LoadTexture(VulkanContext* ctx, const char* filename, bool isHDR);
    VulkanUniTexture Vulkan_LoadTexture_FromMemory(VulkanContext* ctx, const unsigned char* img_data,int img_size, bool isHDR);
    void Vulkan_CopyBufferToImage(VulkanContext* ctx, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    void Vulkan_CreateBuffer(VulkanContext* ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);

	VkDevice getDevice() const { return m_ctx.device; }
    VkInstance getInstance() const { return m_ctx.instance; }
    VkSurfaceKHR & getSurface()  { return m_display.surface; }





private:

    CDEJATIMER dejatimer;
    float target_frame_time = 0.016;

	std::vector<gpulist_struct > gpu_list;
	
	uint32_t win_w = 1280;
	uint32_t win_h = 1280;
	clock_type::time_point lastTime = clock_type::now();
    std::mutex render_mutex;

    CAudio* m_audio = nullptr;
    std::vector<float> m_vizBuffer;
    static const int VIZ_SAMPLES = 256;


    std::vector<float> audoirenderdata{512, 0.0f};

#ifdef WIN32
    PFN_vkGetMemoryWin32HandleKHR fpGetMemoryWin32HandleKHR = nullptr;
#endif

	void BlitMasterToSwapchain(VkCommandBuffer cmd, uint32_t imageIndex);

    void InitSpout();

#ifdef USE_SPOUT
    bool bSpoutInitialized = false;
#endif

};


#endif