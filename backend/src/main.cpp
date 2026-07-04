#define SDL_MAIN_HANDLED
#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <thread>
#include <csignal>
#include <cstdio>
#include "webcontroller/websocket.h"
#include "webcontroller/uploadmanager.h"
#include "video/renderer.h"
#include "audio/audio.h"

#include "fs/localfilebrowser.h"
#include "av_encoder.h"
#include "av_decoder.h"
#include "logger.h"
#include "db/milkplaylistdb.h"

#include "video/projectm_wrapper.h"
#include "fs/folders.h"
#include "userconfig.h"


#ifdef _WIN32
#include <windows.h>

// maledette gpu multiple
extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 1;
}
#endif

#ifdef __APPLE__
#include "audio/macos_permissions.h"
#endif

#include "unimixer.h"

CRenderer Renderer;
CAudio Audio;
CAV_ENCODER AV_Encoder;
CAV_DECODER AV_Decoder;

cprojectm_wrapper projectm_wrapper;

cmilkplaylistdb milkplaylistdb;
cuserconfig userconfig;

cunimixer unimixer;

bool running = true;

uint32_t test = 0;

bool event_registered = false;

void Handle_Events();
void HandleRenderChanges();

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void window_pos_callback(GLFWwindow* window, int xpos, int ypos);

void my_handler(int s) {
    printf("Forzatura chiusura (Segnale %d)\n", s);
    drogon::app().quit();

}

int main(int argc, char* argv[]) {
#ifdef __APPLE__
    std::string configpath = getMacAppSupportPath() + "/";
#else
    std::string configpath = "./";
#endif

#ifdef  __APPLE__
    requestMicrophonePermission();
#endif
    DEJAVISUI_LOG_INFO("[DEJAVISUI] Using config path: %s",configpath.c_str());
    userconfig.loadConfig(configpath+"dejavis_config.json");

    Json::Value video_config = userconfig.getConfig()["video"];
    Json::Value audio_config = userconfig.getConfig()["audio"];;
    Json::Value general_config = userconfig.getConfig()["general"];
    Json::Value srt_out_config = userconfig.getConfig()["srt_output"];
    Json::Value ndi_out_config = userconfig.getConfig()["ndi_output"];;


    if (general_config["debuglog"].asBool() == true) {
        Logger::getInstance().setMinLevel(LogLevel::LevelDebug);
        av_log_set_level(AV_LOG_VERBOSE);
    }else {
        Logger::getInstance().setMinLevel(LogLevel::LevelInfo);
        av_log_set_level(AV_LOG_INFO);
    }

    std::string audiopath = audio_config["file_path"].asString();
    milkplaylistdb.Init(configpath + "projectm_milk.db");


    Audio.m_projectm_wrapper = &projectm_wrapper;

    Audio.m_penedingAudioDevLoad.deviceid = Audio.getDefaultOutput();
    Audio.m_penedingAudioDevLoad.samplerate = 48000;
    Audio.m_penedingAudioDevLoad.outputtype = 1;
    Audio.m_penedingAudioDevLoad.outidx = CAUDIO_MIXER::OUTPUT_MASTER;
    Audio.m_penedingAudioDevLoad.channels = 2;
    Audio.m_penedingAudioDevLoad.shouldLoad.store(true);

    Audio.m_penedingDummyLoad.device = CAUDIO_MIXER::OUTPUT_AUX;
    Audio.m_penedingDummyLoad.masterclock_device = CAUDIO_MIXER::OUTPUT_MASTER;
    Audio.m_penedingDummyLoad.shouldLoad.store(true);


    Audio.startProcessing();

    Audio.fileplayers_basepath = audiopath.c_str();

    Renderer.SetAudioEngine(&Audio);
    Renderer.AV_ENCODER = &AV_Encoder;
    Renderer.FetchGPUList(general_config["debuglog"].asBool());
    if (general_config["debuglog"].asBool()){
        Renderer.Print_GPU_List();
    }




    Renderer.vsync = false;
    Renderer.SetFrameLimit(60);


    if (video_config["file_path"].asString() == "") {
        Renderer.fileplayers_basepath = audiopath;
    }else {
        Renderer.fileplayers_basepath = video_config["file_path"].asString();
    }



    Renderer.Init_Core(video_config["gpu_idx"].asInt(),video_config["core_w"].asInt(),video_config["core_h"].asInt());
    if (general_config["debuglog"].asBool() == true) {
        VulkanLog::Init(Renderer.m_ctx.instance,Renderer.m_ctx.device,"DEJAVISUI");
    }
    Renderer.Init_GLFW_Window(video_config["window_w"].asInt(),video_config["window_h"].asInt());
    Json::Value video = userconfig.getConfig()["video"];
    if (video["fullscreen"].asBool()) {
        Renderer.SetFullScreen(true);
    }

    unimixer.Init(&Audio,&Renderer);
    unimixer.setAudioBasePath(audio_config["file_path"].asString());
    unimixer.setVideoBasePath(video_config["file_path"].asString());


    Renderer.videoTextures[0].VkTexture.sampler = Renderer.m_ctx.defaultSampler;
    projectm_wrapper.Init(&Renderer.m_ctx,&Renderer.videoTextures[0],video_config["core_w"].asInt(),video_config["core_h"].asInt());
    Renderer.m_projectm_wrapper = &projectm_wrapper;

    Renderer.videoTextures[0].VkTexture.descriptorSet = createTextureDescriptor(&Renderer.m_ctx,Renderer.m_ctx.defaultSampler,Renderer.videoTextures[0].VkTexture.view);

#ifndef __APPLE__
    Renderer.videoMixerTextures[0].y_flip = true;
#endif
    Renderer.videoMixerTextures[0].inUse = true;
    Renderer.videoMixerTextures[0].type = 0;
    Renderer.videoMixerTextures[0].isVisible = true;
    projectm_wrapper.PostProcessInit();
    Renderer.Init_ImGui();

    /* UGLY JUST FOR TESTING */
    Renderer.ffmpeg_vk_ctx =  Renderer.CreateFFmpegVulkanHWContext();

    AV_Encoder.InitHW(&Renderer.m_ctx);
    AV_Encoder.InitVulkanEncoderHW(Renderer.ffmpeg_vk_ctx);
    AV_Encoder.init(ndi_out_config["enabled"].asBool(),Renderer.core_w,Renderer.core_h,srt_out_config["video_bitrate"].asInt(),srt_out_config["audio_bitrate"].asInt(),Audio.AUDIO_MIXER.master_samplerate,&Audio.srtLiveBuffer_planar);
    Audio.av_ndi_sender = &AV_Encoder.m_ndi_sender;

    Renderer.InitRGB2YUV();

    if (!YUV2RGBPipeline::instance().isInitialized()) {
        if (!YUV2RGBPipeline::instance().init(&Renderer.m_ctx)) {
            DEJAVISUI_LOG_ERROR("YUV2RGBPipeline::init fallita");
        }
    }
    YUV2RGBPipeline::instance().setMixerDescriptorLayout(Renderer.m_ctx.m_mixerDescriptorLayout);
    std::string srt_url = "srt://0.0.0.0:5000?mode=listener&latency=20&payload_size=1316";

    if (srt_out_config["enabled"].asBool()) {
        AV_Encoder.addOutput(srt_url,"mpegts");
    }

#ifdef USE_SPOUT
    Renderer.Init_SPOUT2();
#endif

    CWebSocket::Renderer = &Renderer;
    CWebSocket::Audio = &Audio;
    CWebSocket::milkPlaylistDB = &milkplaylistdb;
    CWebSocket::AV_ENCODER = &AV_Encoder;
    CWebSocket::m_projectm_wrapper = &projectm_wrapper;
    CWebSocket::m_cunimixer = &unimixer;

    CWebSocket::initBroadcaster();
    CWebSocket::initCallbacks();




    std::thread thr([]() {
#ifdef __APPLE__
    std::string certPath = "../Resources/server.crt";
    std::string keyPath = "../Resources/server.key";
#else
    std::string certPath = "./server.crt";
    std::string keyPath = "./server.key";
#endif



        if (std::filesystem::exists(certPath) && std::filesystem::exists(keyPath)) {
            DEJAVISUI_LOG_DEBUG("Websocket Ready with https mode and WEBRTC Audio support!");
            app().addListener("0.0.0.0", 8848, true, certPath, keyPath);
#ifdef __APPLE__
            app().setDocumentRoot(getMacResourcesFrontendPath());
#else
            app().setDocumentRoot("./webpages");
#endif

            app().setClientMaxBodySize(500 * 1024 * 1024);
            app().registerPostHandlingAdvice([](const drogon::HttpRequestPtr &, const drogon::HttpResponsePtr &resp) {
                resp->addHeader("Access-Control-Allow-Origin", "*");
                resp->addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS, PUT, DELETE");
                resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
            });
            app().setTermSignalHandler([&]() {
                DEJAVISUI_LOG_DEBUG("Chiusura intercettata da Drogon...");

                //app().quit();
                running = false;
            });
            DEJAVISUI_LOG_INFO("Listening on https port 8848");
            app().run();
        } else {
            DEJAVISUI_LOG_ERROR("NO %s - %s found, starting with http mode, no WEBRTC Audio support", certPath.c_str(), keyPath.c_str());
            app().addListener("0.0.0.0", 8848);
            app().setDocumentRoot("./dist");
            app().setClientMaxBodySize(500 * 1024 * 1024);
            app().registerPostHandlingAdvice([](const drogon::HttpRequestPtr &, const drogon::HttpResponsePtr &resp) {
                resp->addHeader("Access-Control-Allow-Origin", "*");
                resp->addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS, PUT, DELETE");
                resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
            });
            app().setTermSignalHandler([&]() {
                DEJAVISUI_LOG_DEBUG("Chiusura intercettata da Drogon...");

                //app().quit();
                running = false;
            });
            DEJAVISUI_LOG_INFO("Listening on port http 8848");
            app().run();
        }
    });


    
    while(running) {
        HandleRenderChanges();
        if(Renderer.gpu_active){
            Handle_Events();
            CWebSocket::broadcastAudioBinary();
            Renderer.Render();
        }else{
            usleep(33000);
        }

    }

    if (general_config["debuglog"].asBool() == true) {
        VulkanLog::Shutdown(Renderer.m_ctx.instance);
    }

    AV_Encoder.cleanup();
    projectm_wrapper.Cleanup();

    Audio.stop();
    Renderer.Cleanup_Core();
    app().quit();

    return 0;

}


void HandleRenderChanges() {

}

void Handle_Events() {
    if (!Renderer.glfw_active) return;
    if (!event_registered) {
        glfwSetKeyCallback(Renderer.glfw_window, key_callback);
        glfwSetWindowPosCallback(Renderer.glfw_window, window_pos_callback);
        event_registered = true;
    }



    if (glfwWindowShouldClose(Renderer.glfw_window)) {
        running = false;
    }

    glfwPollEvents();

    if (unimixer.p_audioplayerLoad.shouldLoad.load() == true) {
        unimixer.AddAudioPlayer(unimixer.p_audioplayerLoad.mixerid);
        unimixer.p_audioplayerLoad.shouldLoad.store(false);
    }


    if (unimixer.p_videoPlayerLoad.shouldLoad.load()) {
        unimixer.p_videoPlayerLoad.shouldLoad.store(false);
        unimixer.AddVideoFilePlayer(unimixer.p_videoPlayerLoad.audio_mixerid);
        //AddVideoFilePlayerToMixer(m_pendingInputLoad.url,m_pendingInputLoad.mixerid);

    }
    if (unimixer.p_videoPlayerFileLoad.shouldLoad.load()) {
        unimixer.p_videoPlayerFileLoad.shouldLoad.store(false);
        int videoslot = unimixer.p_videoPlayerFileLoad.video_mixerid;
        if (videoslot == -1) {
            videoslot = Audio.AUDIO_MIXER.getMixerInputItem(unimixer.p_videoPlayerFileLoad.audio_mixerid)->videomixer_idx;
        }

        if (Renderer.videoMixerTextures[videoslot].AV_DECODER) {
            Renderer.videoMixerTextures[videoslot].AV_DECODER->LoadFileAsync(unimixer.p_videoPlayerFileLoad.path);
        }

    }

    if (unimixer.p_videoMixerUnLoad.shouldUnLoad.load() == true) {
        unimixer.p_videoMixerUnLoad.shouldUnLoad.store(false);
        unimixer.RemoveVideoFilePlayer(unimixer.p_videoMixerUnLoad.audio_mixerid);
    }

    if (Audio.AUDIO_MIXER.NDIOutLoad.shouldLoad.load()) {
        Audio.AUDIO_MIXER.NDIOutLoad.shouldLoad.store(false);
        if (Audio.AUDIO_MIXER.NDIOutLoad.mixerid == 0) {
            Audio.AUDIO_MIXER.Mixer_NDI_Output(CAUDIO_MIXER::MIXER_OUTPUTS::OUTPUT_MASTER,Audio.AUDIO_MIXER.NDIOutLoad.status);
        }
        if (Audio.AUDIO_MIXER.NDIOutLoad.mixerid == 1) {
            Audio.AUDIO_MIXER.Mixer_NDI_Output(CAUDIO_MIXER::MIXER_OUTPUTS::OUTPUT_AUX,Audio.AUDIO_MIXER.NDIOutLoad.status);
        }
    }


}

void window_pos_callback(GLFWwindow* window, int xpos, int ypos) {
    //auto app = reinterpret_cast<RendererClass*>(glfwGetWindowUserPointer(window));

    if (xpos != 0 && ypos != 0) {
        Renderer.win_pos_x = xpos;
        Renderer.win_pos_y = ypos;
    }

}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {

    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_F:
                Renderer.SetFullScreen(!Renderer.fullscreen);
                break;
            case GLFW_KEY_ESCAPE:
                running = false;
                glfwSetWindowShouldClose(window, GLFW_TRUE);
                break;

        }
    }
}

