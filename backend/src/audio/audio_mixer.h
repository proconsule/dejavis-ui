#ifndef DEJAVIS_APP_AUDIO_MIXER_H
#define DEJAVIS_APP_AUDIO_MIXER_H


#include <string.h>
#include "../ringbuffer.h"
#include "../multichannel_ringbuffer.h"
#include <cstdint>
#include <stdexcept>
#include <mutex>
#include <memory>
#include <thread>
#include <json/json.h>
#include "audio_resampler.h"
#include <portaudio.h>

#include "audio_threadpool.h"
#include "audio_rtc.h"

#include "../audioplayer/audioplayer.h"

#include "../ndi_receiver.h"
#include "backend/src/ndi_sender.h"
#include "audioeffects/EffectBank.h"

#include <tracy/Tracy.hpp>


enum class GainPreset {
    Plus0dB = 0,
    Plus6dB,
    Plus12dB,
    Plus20dB
};

struct AudioMixerInputItem {
    std::string name;
    int mixerout_idx = -1;
    int type = -1;
    bool isLive = false;
    bool isActive = false;
    bool isMuted = false;
    bool isSolo = false;
    bool soloExlude = false;
    int64_t source_sampleRate;
    float volume = 1.0f;
    float pan = 0.0f;
    float levelL = 0.0f;
    float levelR = 0.0f;

    GainPreset gainPreset = GainPreset::Plus0dB;

    caudioplayer *fileplayer = nullptr;
    CNDIReceiver *ndi_source = nullptr;
    int videomixer_idx = -1;

    std::unique_ptr<MultiChannelRingBuffer> buffer_planar;
    uint64_t overflowCount{0};
    uint64_t underflowCount{0};

    bool filterEnabled = false;
    bool delayEnabled  = false;
    bool compEnabled   = false;
    bool reverbEnabled = false;

    AudioResampler Resampler;
    PaStream *inputStream = nullptr;

    int limiterSlot = -1;
    bool limiterEnabled = false;

    AudioMixerInputItem(std::string _name, bool _live, int64_t _sr ,int64_t _outsr, size_t len)
        : name(_name), isLive(_live), source_sampleRate(_sr), pan(0.0f), buffer_planar(std::make_unique<MultiChannelRingBuffer>(2,len)) {

    }
};

struct AudioMixerOutputItem {
    std::string name;
    int audio_dev_id;
    std::string audio_dev_name;
    std::unique_ptr<RingBuffer> buffer;
    float volume;
    float pan = 0.0;
    int channels;
    int samplerate;
    uint64_t overflowCount{0};
    uint64_t underflowCount{0};

    float levelL{0.0};
    float levelR{0.0};
    float prelimiterL{0.0};
    float prelimiterR{0.0};

    int limiterSlot = -1;

    AudioResampler Resampler;
    CNDISender ndi_audio_out;



    AudioMixerOutputItem(std::string _name,size_t len ,size_t _oubufle = 16348)
    :name(_name), buffer(std::make_unique<RingBuffer>(len)) {
        audio_dev_name = _name;
        audio_dev_id = -1;
        volume = 1.0f;



    }

};


struct PendingNDILoad {
    std::atomic<bool> shouldLoad{false};
    int mixerid = 0;
    std::string url;
    bool status = false;
};

struct PendingNDIUnLoad {
    std::atomic<bool> shouldUnLoad{false};
    bool isvideomixer = false;
    int mixerid = 0;
};

class CAUDIO_MIXER {
public:

    enum MIXER_OUTPUTS {
        OUTPUT_NONE = -1,
        OUTPUT_MASTER,
        OUTPUT_AUX
    };


    CAUDIO_MIXER() {
        m_silence_buffer = std::make_unique<MultiChannelRingBuffer>(4096);
        std::vector<float> zeros(4096, 0.0f);
        //m_silence_buffer->write(zeros.data(), 1024);
        m_silence_chunk.assign(SILENCE_CHUNK_FRAMES, 0.0f);
        m_silence_chans.assign(m_silence_buffer->getChannels(), m_silence_chunk.data());
        m_master_out = std::make_unique<RingBuffer>(16384);



        m_outputs.push_back(std::make_unique<AudioMixerOutputItem>("MASTER", 8192));
        m_outputs.push_back(std::make_unique<AudioMixerOutputItem>("AUX", 8192));

        trash_buffer = std::make_unique<RingBuffer>(16384);
        trash_buffer_out = std::make_unique<RingBuffer>(16384);


        audio_utils::EffectBank::SlotConfig cfg;
        cfg.type = audio_utils::EffectBank::EffectType::Limiter;
        cfg.limiter.sampleRate = 48000;
        cfg.limiter.limit      = 0.95f;
        cfg.limiter.attackMs   = 1.0f;
        cfg.limiter.releaseMs  = 50.0f;
        cfg.limiter.autoLevel  = false;
        cfg.limiter.asc        = true;
        cfg.meterDecaySec      = 0.1f;

        m_outputs[0]->limiterSlot = output_effectBank.AddEffectToSlot(0,cfg);
        m_outputs[1]->limiterSlot = output_effectBank.AddEffectToSlot(1,cfg);

        constexpr size_t MAX_FRAMES = 4096;

        m_masterMix[0].resize(MAX_FRAMES);
        m_masterMix[1].resize(MAX_FRAMES);
        m_mastertempInput[0].resize(MAX_FRAMES);
        m_mastertempInput[1].resize(MAX_FRAMES);

        m_AuxMix[0].resize(MAX_FRAMES);
        m_AuxMix[1].resize(MAX_FRAMES);
        m_AuxtempInput[0].resize(MAX_FRAMES);
        m_AuxtempInput[1].resize(MAX_FRAMES);

        m_masterRead[0].resize(MAX_FRAMES);
        m_masterRead[1].resize(MAX_FRAMES);

        m_auxRead[0].resize(MAX_FRAMES);
        m_auxRead[1].resize(MAX_FRAMES);


        for (int i=0;i<16;i++) {
            std::string _name = "CH " + std::to_string(i);
            AddToMixer(_name,true,48000);
        }


        for (int i=0;i<16;i++) {
            m_inputs[i]->limiterSlot = i;
        }



    }

    ~CAUDIO_MIXER();
    void Stop();

    void setGainFactor(int _mixerid, GainPreset _gainpreset);

    bool IsStopping() const { return m_stopping.load(); }

    int AddToMixer(std::string _name,bool _live,int64_t _samplerte,uint32_t _bufferlen = 16384);
    void SetActiveStatus(int _idx, bool _active);
    void RemoveFromMixer(int _idx);

    void AddPlayerToMixer(std::string _basepath,int _mixerid);
    void AddGenericToMixer(int _mixerd);

    int GetFirstFreeSlot();

    void RemoveGenericToMixer(int _mixerd);


    void AddNDIToMixer(int _mixerd);
    void RemoveNDIFromMixer(int _mixerd);


    // EFEFCTS

    void SetCompressor(int _mixerid,float _threshold,float _ratio,float _attack,float _release);
    void SetCompressorEnable(int _mixerid,bool enabled);

    void SerReverb(int _mixerid,float _feedback,float _lpfreq);
    void SetReverbEnable(int _mixerid, bool enabled);

    void SetDelay(int _mixerid,float _delay,float _feedback);
    void SetDelayEnable(int _mixerid, bool enabled);

    void SelMixetOut(int _mixerid,MIXER_OUTPUTS _out);

    void SetInputMute(int _idx,bool _muted);
    void SetInputSolo(int _idx,bool _solo);
    void SetInputVolume(int _idx,float _val);
    void SetInputPan(int _idx,float _val);
    void SetInputMixerID(int _idx,int _mixerid);
    void SetMasterVolume(float _val);
    void SetAuxVolume(float _val);

    void SetMasterPan(float _val);
    void SetAuxPan(float _val);

    void CheckSolo(int _mixerout_idx);

    std::vector<float> GetInputVolumes();
    std::vector<float> GetOutputVolumes();

    void Mixer_NDI_Output(int _mixeroutid,bool _status);

    void ConsumeBuffers(uint32_t _samples);
    MultiChannelRingBuffer* GetBuffer(int _idx);
    MultiChannelRingBuffer* GetSelectedBuffer();

    void ProcessMix(size_t _samples);
    void ProcessOut(size_t _samples);

    void ProcessMasterMix(size_t _samples);
    void ProcessAuxMix(size_t _samples);
    void ProcessUnassigned(size_t _samples);

    void ProcessMasterOutput(float **_planar_in, int _channel_size);
    void ProcessAuxOutput(float * samples,int size);

    void ProcessMixInputs(size_t _samples);

    void ProcessInput(AudioMixerInputItem *_input);

    void ProcessUnassignedOutput(float * samples,int size);


    MultiChannelRingBuffer * getMasterProcessBuffer(){return &prcessmaster_buffer;};
    MultiChannelRingBuffer * getAuxProcessBuffer(){return  &processaux_buffer;};

    Json::Value GetStatus();
    int64_t master_samplerate = 48000;
    int master_channels = 2;

    AudioMixerInputItem *getMixerInputItem(int _idx) {
        return m_inputs[_idx].get();
    }

    AudioMixerOutputItem *getMixerOutputItem(int _idx) {
        return m_outputs[_idx].get();
    }

    RingBuffer* GetMasterBuffer() { return m_outputs[0]->buffer.get(); }
    RingBuffer* GetAuxBuffer() { return m_outputs[1]->buffer.get(); }
    RingBuffer* GetUnassignedBuffer() { return trash_buffer_out.get(); }
    RTCManager * RTCAudio = nullptr;

    std::unique_ptr<RingBuffer> trash_buffer;
    std::unique_ptr<RingBuffer> trash_buffer_out;

    audio_utils::EffectBank input_effectBank{16};
    audio_utils::EffectBank output_effectBank{2};


    PendingNDILoad NDIOutLoad;

    std::array<std::vector<float>, 2> m_masterRead;
    std::array<std::vector<float>, 2> m_auxRead;

    std::mutex m_mixer_mutex;
private:
    std::unique_ptr<RingBuffer> m_master_out;
    std::vector<std::unique_ptr<AudioMixerInputItem>> m_inputs;
    std::vector<std::unique_ptr<AudioMixerOutputItem>> m_outputs;

    AudioThreadPool mix_thread_pool{3};

    std::unique_ptr<MultiChannelRingBuffer> m_silence_buffer;

    int m_selected_idx = -1;

    static constexpr size_t SILENCE_CHUNK_FRAMES = 4096;
    std::vector<float>        m_silence_chunk;
    std::vector<const float*> m_silence_chans;

    float m_current_scale = 1.0f;
    float m_lerp_speed = 0.05f;

    MultiChannelRingBuffer prcessmaster_buffer;
    MultiChannelRingBuffer processaux_buffer;

    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_stopped{false};

    std::array<std::vector<float>, 2> m_mastertempInput;
    std::array<std::vector<float>, 2> m_masterMix;


    std::array<std::vector<float>, 2> m_AuxtempInput;
    std::array<std::vector<float>, 2> m_AuxMix;




};

#endif