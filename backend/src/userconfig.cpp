#include "userconfig.h"

#include <json/json.h>
#include <fstream>
#include <iostream>
#include <filesystem>

#include "logger.h"

namespace fs = std::filesystem;

void cuserconfig::createConfig(std::string _filename) {

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "    ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());


    Json::Value config;
    Json::Value general;
    Json::Value audio;
    Json::Value video;
    Json::Value srt_output;


    general["debuglog"] = false;

    audio["file_path"] = "./";
    audio["file_path"].setComment(std::string("/* path to be used by audio file players */"),Json::commentAfterOnSameLine);

    Json::Value ndi_output;
    ndi_output["enabled"] = false;


    srt_output["enabled"] = true;
    srt_output["enabled"].setComment(std::string("/* NEEDED FOR WEBRTC ALSO */"),Json::commentAfterOnSameLine);

    srt_output["video_bitrate"] = 5000000;
    srt_output["audio_bitrate"] = 256000;

    video["gpu_idx"] = 0;
    video["file_path"] = "./";
    video["file_path"].setComment(std::string("/* path to be used by video file players */"),Json::commentAfterOnSameLine);
    video["fullscreen"] = false;
    video["gpu_idx"].setComment(std::string("/* GPU index to use */"),Json::commentAfterOnSameLine);
    video["window_w"] = 1920;
    video["window_w"].setComment(std::string("/* Window width */"),Json::commentAfterOnSameLine);
    video["window_h"] = 1080;
    video["window_h"].setComment(std::string("/* Window height */"),Json::commentAfterOnSameLine);
    video["core_w"] = 1280;
    video["core_w"].setComment(std::string("/* Core rendering width */"),Json::commentAfterOnSameLine);
    video["core_h"] = 720;
    video["core_h"].setComment(std::string("/* Core rendering height */"),Json::commentAfterOnSameLine);
    video["vsync"] = true;
    video["vsync"].setComment(std::string("/* VSYNC */"),Json::commentAfterOnSameLine);


    config["audio"] = audio;
    config["video"] = video;
    config["srt_output"] = srt_output;
    config["ndi_output"] = ndi_output;
    config["general"] = general;

    std::ofstream outFile(_filename);
    writer->write(config, &outFile);
    outFile.close();

}

bool cuserconfig::loadConfig(std::string _filename) {

    if (!fs::exists(_filename)) {
        DEJAVISUI_LOG_WARN("No config file found, creating a new one.");
        createConfig(_filename);
    }

    Json::Value config;
    Json::CharReaderBuilder builder;
    std::string errs;

    std::ifstream inFile(_filename);

    if (Json::parseFromStream(builder, inFile, &config, &errs)) {
        filename = _filename;
        user_config = config;

    } else {
        DEJAVISUI_LOG_DEBUG("Errore nel parsing: %s",errs.c_str());
        return false;
    }

    return true;
}

bool cuserconfig::saveConfig() {

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "    ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());

    std::ofstream outFile(filename);
    writer->write(user_config, &outFile);
    outFile.close();

    return true;
}