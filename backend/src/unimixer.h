#ifndef DEJAVIS_UI_UNIMIXER_H
#define DEJAVIS_UI_UNIMIXER_H

#include <string>
#include <json/value.h>

#include "multichannel_ringbuffer.h"

#include "video/renderer.h"
#include "audio/audio.h"
#include "audio/audioeffects/EffectBank.h"

struct PendingAudioPlayerLoad {
    std::atomic<bool> shouldLoad{false};
    std::string path;
    int mixerid = 0;

};

struct PendingAudioMixerUnLoad {
    std::atomic<bool> shouldUnLoad{false};
    int mixerid = -1;

};

struct PendingVideoMixerUnLoad {
    std::atomic<bool> shouldUnLoad{false};
    int audio_mixerid = -1;

};

struct PendingVideoPlayerLoad {
    std::atomic<bool> shouldLoad{false};
    std::string path;
    int audio_mixerid = -1;
    int video_mixerid = -1;

};

struct PendingVideoPlayerFileLoad {
    std::atomic<bool> shouldLoad{false};
    std::string path;
    int audio_mixerid = -1;
    int video_mixerid = -1;

};


class cunimixer {
public:
    cunimixer();
    ~cunimixer();

    void Init(CAudio * _audio,CRenderer *_video);

    void AssignVideoBus(int videolayeridx, int busidx);

    bool AddAudioPlayer(int _mixerd = -1);

    bool AddVideoFilePlayer(int _audio_mixer_id = -1,int busidx = 0);

    void RemoveVideoFilePlayer(int _audio_mixer_id);

    bool AddImage(const unsigned char *img_data, int img_size,int busidx = 0);

    bool AddNDI(int _audio_mixer_id = -1,int busidx = 0);

    void setAudioBasePath(std::string _basepath) {
        audio_base_path = _basepath;
    }
    void setVideoBasePath(std::string _basepath) {
        video_base_path = _basepath;
    }


    PendingAudioPlayerLoad p_audioplayerLoad;
    PendingVideoPlayerLoad p_videoPlayerLoad;
    PendingVideoPlayerFileLoad p_videoPlayerFileLoad;

    PendingVideoMixerUnLoad p_videoMixerUnLoad;


/* VIDEO HELPERS */

    void SetKeyer(int _mixeridx, FxKeyerMode _keyer);

    void SetLumaKey(int _mixeridx, LumaKeyParams &params);

    void SetChromaKey(int _mixeridx, KeyerPushConstants &params);

    void SetColor(int _mixeridx, ColorParams &params);

    void SetYuvChromaKey(int _mixeridx, float r, float g, float b, float tolerance);
    void SetYuvLumaKey(int _mixeridx, float luma, float tolerance);
    void DisableYuvKeyer(int _mixeridx);


    void SetVideoMixerProps(videomixeritem &prop, int _viddeo_mixerid);

    void SetDownscale_Bicubic(int _viddeo_mixerid, bool _active);


    /* AUDIO HELPERS */
    void SetInputVolume(int _idx, float _val);

    void setInputGainFactor(int _mixerid, GainPreset _gainpreset);

    void SetInputPan(int _idx, float _val);
    void SetMasterPan(float _val);
    void SetAuxPan(float _val);
    void SetMasterVolume(float _val);
    void SetAuxVolume(float _val);

    void SetInputSolo(int _idx, bool _solo);

    void SetInputMute(int _idx, bool _muted);

    void SetInputMixerID(int _idx, int _mixerid);


    void AddInputAudioFX(int _idx, audio_utils::EffectBank::EffectType _effecttype);
    void RemoveInputAudioFX(int _idx,int _fxpos);

    void AddShderToy(int _videomixeridx);

    void ShaderToy_DeployShader(std::string _fragshader);

    bool ShaderToy_TestShader(std::string _fragshader);

    void ReconfigureInputFX(int _idx,int _fxpos,audio_utils::EffectBank::SlotConfig mycfg);

    void SetWebRTC_Preview_BUS(int _busidx);
    void SetSPOUT2_Preview_BUS(int _busidx);
    void SetDisplay_BUS(int _busidx);


    Json::Value getMixerStatus();

    CAudio * audio_ref = nullptr;
    CRenderer * video_ref = nullptr;

    MultiChannelRingBuffer * getMixerInputBuffer(int _mixerid) {
        return audio_ref->AUDIO_MIXER.getMixerInputItem(_mixerid)->buffer_planar.get();
    }

private:

    std::string audio_base_path = "";
    std::string video_base_path = "";
};


#endif //DEJAVIS_UI_UNIMIXER_H