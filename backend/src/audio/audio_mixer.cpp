#include "audio_mixer.h"
#include <iostream>
#include <math.h>
#include "../logger.h"

constexpr float PI_2 = 3.14159265f / 2.0f;

float getGainFactor(GainPreset preset) {
    switch (preset) {
        case GainPreset::Plus6dB:  return std::pow(10.0f, 6.0f / 20.0f);  // ~2.0f
        case GainPreset::Plus12dB: return std::pow(10.0f, 12.0f / 20.0f); // ~3.98f
        case GainPreset::Plus20dB: return 10.0f;
        default: return 1.0f;
    }
}

uint32_t scaleSamples(uint32_t sourceSamples, uint32_t sourceRate, uint32_t targetRate) {
    if (sourceRate == targetRate) return sourceSamples;

    return static_cast<uint32_t>(std::ceil(static_cast<double>(sourceSamples) * targetRate / sourceRate));
}


int CAUDIO_MIXER::AddToMixer(std::string _name, bool _live, int64_t _samplerte,uint32_t _bufferlen) {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);

    m_inputs.push_back(std::make_unique<AudioMixerInputItem>(_name, _live, _samplerte,master_samplerate,_bufferlen));

    return m_inputs.size()-1 ;
}

void CAUDIO_MIXER::RemoveFromMixer(int _idx) {
    if (_idx >= m_inputs.size()) return;
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    m_inputs[_idx].get()->type = -1;
    m_inputs[_idx].get()->isActive = false;
    if (m_inputs[_idx].get()->fileplayer) {
        m_inputs[_idx].get()->fileplayer->Stop();
        delete m_inputs[_idx].get()->fileplayer;
        m_inputs[_idx].get()->fileplayer = nullptr;
    }
}

void CAUDIO_MIXER::AddPlayerToMixer(std::string _basepath,int _mixerd) {
    if (_mixerd >= m_inputs.size()) return;
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    if (m_inputs[_mixerd].get()->fileplayer)return;
    m_inputs[_mixerd].get()->fileplayer = new caudioplayer();
    m_inputs[_mixerd].get()->fileplayer->Init(48000,2,m_inputs[_mixerd].get()->buffer_planar.get());
    m_inputs[_mixerd].get()->fileplayer->SetFileBroswerBasePath(_basepath);
    m_inputs[_mixerd].get()->isActive = true;
    m_inputs[_mixerd].get()->mixerout_idx = 0;
    m_inputs[_mixerd].get()->type = 0;
    CheckSolo(0);
}

void CAUDIO_MIXER::AddGenericToMixer(int _mixerd) {
    if (_mixerd >= m_inputs.size()) return;
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    if (m_inputs[_mixerd].get()->fileplayer || m_inputs[_mixerd].get()->isActive)return;
    m_inputs[_mixerd].get()->isActive = true;
    m_inputs[_mixerd].get()->mixerout_idx = 0;
    m_inputs[_mixerd].get()->type = 3;
    CheckSolo(0);
}

int CAUDIO_MIXER::GetFirstFreeSlot() {
    for (int i=0;i<m_inputs.size();i++) {
        if (!m_inputs[i]->isActive)return i;
    }
    return -1;
}


void CAUDIO_MIXER::AddNDIToMixer(int _mixerd) {
    if (_mixerd >= m_inputs.size()) return;
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    if (m_inputs[_mixerd].get()->isActive)return;
    m_inputs[_mixerd].get()->ndi_source = new CNDIReceiver();
    m_inputs[_mixerd].get()->ndi_source->Init_AudioOnly(m_inputs[_mixerd].get()->buffer_planar.get(),48000,2);
    m_inputs[_mixerd].get()->isActive = true;
    m_inputs[_mixerd].get()->mixerout_idx = 0;
    m_inputs[_mixerd].get()->type = 6;
    CheckSolo(0);
}

void CAUDIO_MIXER::RemoveNDIFromMixer(int _mixerd) {
    if (_mixerd >= m_inputs.size()) return;
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    m_inputs[_mixerd].get()->isActive = false;
    m_inputs[_mixerd].get()->mixerout_idx = -1;
    m_inputs[_mixerd].get()->type =-1;
}

void CAUDIO_MIXER::RemoveGenericToMixer(int _mixerd) {
    DEJAVISUI_LOG_DEBUG("REMOVING GENERIC FROM MIXERID: %d",_mixerd);
    if (_mixerd >= m_inputs.size()) return;
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    m_inputs[_mixerd].get()->isActive = false;
    m_inputs[_mixerd].get()->mixerout_idx = -1;
    m_inputs[_mixerd].get()->type =-1;
    //m_inputs[_mixerd].get()->buffer.reset();
}

void CAUDIO_MIXER::SetInputMute(int _idx, bool _muted) {
    if (_idx == -1) return;
    std::lock_guard<std::mutex> lock(m_mixer_mutex);

    if (_idx >= m_inputs.size()) return;

    m_inputs[_idx]->isMuted = _muted;
    DEJAVISUI_LOG_DEBUG("Slot %u mute status impostato a: %s\n", _idx, _muted ? "MUTED" : "UNMUTED");
}

void CAUDIO_MIXER::SetInputSolo(int _idx, bool _solo) {
    if (_idx == -1) return;
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    if (_idx >= m_inputs.size()) return;
    int outmixinuse = m_inputs[_idx]->mixerout_idx;
    for (int i=0;i<m_inputs.size();i++) {
        if (m_inputs[i]->mixerout_idx == outmixinuse) {
            m_inputs[i]->isSolo = false;
            m_inputs[i]->soloExlude = false;
        }
    }

    m_inputs[_idx]->isSolo = _solo;
    CheckSolo(outmixinuse);
    DEJAVISUI_LOG_DEBUG("Slot %u SOLO status impostato a: %s\n", _idx, _solo ? "ON" : "OFF");
}

void CAUDIO_MIXER::CheckSolo(int _mixerout_idx) {
    int soloidx = -1;
    for (int i=0;i<m_inputs.size();i++) {
        if (m_inputs[i]->mixerout_idx == _mixerout_idx) {
            if (m_inputs[i]->isSolo)soloidx = i;
            m_inputs[i]->soloExlude = false;
        }
    }
    if (soloidx>=0) {
        for (int i=0;i<m_inputs.size();i++) {
            if (m_inputs[i]->mixerout_idx == _mixerout_idx) {
                if (i == soloidx)continue;
                m_inputs[i]->soloExlude = true;
            }
        }
    }
}

/*
void CAUDIO_MIXER::SetInputVolume(int _idx,float _val) {
    if (_idx == -1) return;
    if (_val < 0.0f || _val >1.0f)return;
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    m_inputs[_idx]->volume = _val;
}
*/

void CAUDIO_MIXER::SetInputPan(int _idx,float _val) {
    if (_idx == -1) return;
    if (_val < -1.0f || _val > 1.0f)return;
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    m_inputs[_idx]->pan = _val;
}

void CAUDIO_MIXER::SetMasterPan(float _val) {
    m_outputs[0]->pan = _val;
}

void CAUDIO_MIXER::SetAuxPan(float _val) {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    m_outputs[1]->pan = _val;
}

void CAUDIO_MIXER::SetMasterVolume(float _val) {
    if (_val < 0.0f || _val >1.0f)return;
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    m_outputs[0]->volume = _val;
}

void CAUDIO_MIXER::SetAuxVolume(float _val) {
    if (_val < 0.0f || _val >1.0f)return;
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    m_outputs[1]->volume = _val;
}

void CAUDIO_MIXER::SetActiveStatus(int _idx, bool _active) {
    if (_idx == -1) return;
    std::lock_guard<std::mutex> lock(m_mixer_mutex);

    if (_idx >= m_inputs.size()) return;

    m_inputs[_idx]->isActive = _active;
    DEJAVISUI_LOG_DEBUG("Slot %u status impostato a: %s\n", _idx, _active ? "ACTIVE" : "INACTIVE");
}

MultiChannelRingBuffer* CAUDIO_MIXER::GetSelectedBuffer() {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);

    if (m_inputs.empty() || m_selected_idx >= m_inputs.size()) {
        size_t remaining = m_silence_buffer->getAvailableWrite();
        while (remaining > 0) {
            size_t n = std::min(remaining, SILENCE_CHUNK_FRAMES);
            if (!m_silence_buffer->write(m_silence_chans.data(), n)) break;
            remaining -= n;
        }
        return m_silence_buffer.get();
    }

    return m_inputs[m_selected_idx]->buffer_planar.get();
}

MultiChannelRingBuffer* CAUDIO_MIXER::GetBuffer(int _idx) {
    if (_idx == -1) return nullptr;
    std::lock_guard<std::mutex> lock(m_mixer_mutex);

    if (_idx < m_inputs.size()) {
        return m_inputs[_idx]->buffer_planar.get();
    }

    return m_silence_buffer.get();
}

void CAUDIO_MIXER::ConsumeBuffers(uint32_t _samples) {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);

    for (uint32_t i = 0; i < m_inputs.size(); ++i) {

        if (!m_inputs[i]->isActive) {
            continue;
        }

        m_inputs[i]->buffer_planar->skip(_samples);
    }
}

void CAUDIO_MIXER::ProcessMasterOutput(float **_planar_in,int _channel_size) {

    auto* out = m_outputs[0].get();


    float volL = out->volume;
    float volR = out->volume;
    if (out->pan > 0.0f) {
        volL *= (1.0f - out->pan);
    } else if (out->pan < 0.0f) {
        volR *= (1.0f + out->pan);
    }

    for (int i = 0; i < _channel_size; i++) {
        _planar_in[0][i] *= volL;
        _planar_in[1][i] *= volR;
    }


    limiterBank.ProcessPlanar(
        m_outputs[0]->limiterSlot,
        _planar_in[0],
        _planar_in[1],
        _channel_size
    );

    m_outputs[0]->levelL = limiterBank.GetPostLevelL (m_outputs[0]->limiterSlot);
    m_outputs[0]->levelR = limiterBank.GetPostLevelR (m_outputs[0]->limiterSlot);
    m_outputs[0]->prelimiterL = limiterBank.GetPreLevelL (m_outputs[0]->limiterSlot);
    m_outputs[0]->prelimiterR = limiterBank.GetPreLevelR (m_outputs[0]->limiterSlot);


    auto& outResampler = getMixerOutputItem(0)->Resampler;
    AVFrame* converted = outResampler.processPlanar(_planar_in, _channel_size);
    if (!converted) return;


    out->buffer->write((float *)converted->data[0], converted->nb_samples * 2);

}


void CAUDIO_MIXER::ProcessUnassignedOutput(float * samples,int size) {

    trash_buffer_out->write(samples, size);

}


void CAUDIO_MIXER::ProcessAuxOutput(float * samples,int size) {

    auto* out = m_outputs[1].get();

    float volL = out->volume;
    float volR = out->volume;
    if (out->pan > 0.0f) {
        volL *= (1.0f - out->pan);
    } else if (out->pan < 0.0f) {
        volR *= (1.0f + out->pan);
    }

    limiterBank.Process(
        m_outputs[1]->limiterSlot,
        samples,
        size / 2,
        1.0f
    );
    m_outputs[1]->levelL = limiterBank.GetPostLevelL (m_outputs[1]->limiterSlot);
    m_outputs[1]->levelR = limiterBank.GetPostLevelR (m_outputs[1]->limiterSlot);
    m_outputs[1]->prelimiterL = limiterBank.GetPreLevelL (m_outputs[1]->limiterSlot);
    m_outputs[1]->prelimiterR = limiterBank.GetPreLevelR (m_outputs[1]->limiterSlot);

    out->buffer->write(samples, size);
}

void CAUDIO_MIXER::ProcessMixInputs(size_t _samples) {

}


void CAUDIO_MIXER::ProcessMasterMix(size_t frames) {
    ZoneScopedN("ProcessMasterMix");
    size_t master_frames = scaleSamples(frames, 48000, m_outputs[0]->samplerate);

    if (m_outputs[0]->buffer->getAvailableWrite() < master_frames*2) return;

    std::fill(m_masterMix[0].begin(), m_masterMix[0].begin() + frames, 0.0f);
    std::fill(m_masterMix[1].begin(), m_masterMix[1].begin() + frames, 0.0f);

    float* tempIn[2] = { m_mastertempInput[0].data(), m_mastertempInput[1].data() };

    for (auto& input : m_inputs) {
        if (!input->isActive) continue;
        if (input->mixerout_idx != MIXER_OUTPUTS::OUTPUT_MASTER) continue;
        if (input->buffer_planar->getAvailableRead() < frames) continue;
        if (!input->buffer_planar->read(tempIn, frames)) continue;

        const float gain = getGainFactor(input->gainPreset);
        float volL = input->volume * gain;
        float volR = input->volume * gain;
        if (input->pan >= 0.0f) volL *= (1.0f - input->pan);
        else                    volR *= (1.0f + input->pan);

        const bool  emit = !(input->isMuted || input->soloExlude);
        float       peakL = 0.0f, peakR = 0.0f;

        const float* Lin = m_mastertempInput[0].data();
        const float* Rin = m_mastertempInput[1].data();

        if (emit) {
            float* Lout = m_masterMix[0].data();
            float* Rout = m_masterMix[1].data();
            for (size_t i = 0; i < frames; i++) {
                float L = Lin[i] * volL;
                float R = Rin[i] * volR;
                peakL = std::max(peakL, std::fabs(L));
                peakR = std::max(peakR, std::fabs(R));
                Lout[i] += L;
                Rout[i] += R;
            }
        } else {
            for (size_t i = 0; i < frames; i++) {
                float L = Lin[i] * volL;
                float R = Rin[i] * volR;
                peakL = std::max(peakL, std::fabs(L));
                peakR = std::max(peakR, std::fabs(R));
            }
        }

        input->levelL = peakL;
        input->levelR = peakR;
    }

    const float* outChans[2] = { m_masterMix[0].data(), m_masterMix[1].data() };
    prcessmaster_buffer.write(outChans, frames);
}

void CAUDIO_MIXER::ProcessAuxMix(size_t frames) {
    ZoneScopedN("ProcessAuxMix");
    size_t aux_frames = scaleSamples(frames, 48000, m_outputs[1]->samplerate);

    if (m_outputs[1].get()->buffer->getAvailableWrite() < aux_frames) return;

    std::fill(m_AuxMix[0].begin(), m_AuxMix[0].begin() + frames, 0.0f);
    std::fill(m_AuxMix[1].begin(), m_AuxMix[1].begin() + frames, 0.0f);

    float* tempIn[2] = { m_AuxtempInput[0].data(), m_AuxtempInput[1].data() };

    for (auto& input : m_inputs) {
        if (!input->isActive) continue;
        if (input->mixerout_idx != MIXER_OUTPUTS::OUTPUT_AUX) continue;
        if (input->buffer_planar->getAvailableRead() < frames) continue;
        if (!input->buffer_planar->read(tempIn, frames)) continue;

        const float gain = getGainFactor(input->gainPreset);
        float volL = input->volume * gain;
        float volR = input->volume * gain;
        if (input->pan >= 0.0f) volL *= (1.0f - input->pan);
        else                    volR *= (1.0f + input->pan);

        const bool emit = !(input->isMuted || input->soloExlude);
        float peakL = 0.0f, peakR = 0.0f;

        const float* Lin = m_AuxtempInput[0].data();
        const float* Rin = m_AuxtempInput[1].data();

        if (emit) {
            float* Lout = m_AuxMix[0].data();
            float* Rout = m_AuxMix[1].data();
            for (size_t i = 0; i < frames; i++) {
                float L = Lin[i] * volL;
                float R = Rin[i] * volR;
                peakL = std::max(peakL, std::fabs(L));
                peakR = std::max(peakR, std::fabs(R));
                Lout[i] += L;
                Rout[i] += R;
            }
        } else {
            for (size_t i = 0; i < frames; i++) {
                float L = Lin[i] * volL;
                float R = Rin[i] * volR;
                peakL = std::max(peakL, std::fabs(L));
                peakR = std::max(peakR, std::fabs(R));
            }
        }

        input->levelL = peakL;
        input->levelR = peakR;
    }

    const float* outChans[2] = { m_AuxMix[0].data(), m_AuxMix[1].data() };
    processaux_buffer.write(outChans, frames);
}


void CAUDIO_MIXER::ProcessUnassigned(size_t _samples) {
    /*
    std::vector<float> mixedBlock(_samples, 0.0f);
    for (auto& input : m_inputs) {
        if (!input->isActive) continue;
        if (input->mixerout_idx != MIXER_OUTPUTS::OUTPUT_NONE)continue;
        std::vector<float> tempInput(_samples);
        if (input->buffer->getAvailableRead() >= _samples) {
            if (input->buffer->read(tempInput.data(), _samples)) {

                for (size_t i = 0; i < _samples; i += 2) {
                    float left  = tempInput[i];
                    float right = tempInput[i+1];
                    mixedBlock[i]   += left;
                    mixedBlock[i+1] += right;
                }
            }
        }
    }
    trash_buffer->write(mixedBlock.data(), _samples);
    */
}

void CAUDIO_MIXER::ProcessMix(size_t _samples) {
    if (m_stopping.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(m_mixer_mutex);


    mix_thread_pool.enqueue([this, _samples] { ProcessMasterMix(_samples); });
    mix_thread_pool.enqueue([this, _samples] { ProcessAuxMix(_samples); });
    mix_thread_pool.enqueue([this, _samples] { ProcessUnassigned(_samples); });

    mix_thread_pool.waitForAll(3);

    float decayVal = std::pow(0.9999f, _samples);
    for (auto& input : m_inputs) {
        input->levelL *= decayVal;
        input->levelR *= decayVal;
    }

}

void CAUDIO_MIXER::SetCompressor(int _mixerid, float _threshold, float _ratio, float _attack, float _release) {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    AudioMixerInputItem * tmp = m_inputs[_mixerid].get();


}

void CAUDIO_MIXER::SetCompressorEnable(int _mixerid, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    AudioMixerInputItem * tmp = m_inputs[_mixerid].get();

}

void CAUDIO_MIXER::SerReverb(int _mixerid, float _feedback, float _lpfreq) {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    AudioMixerInputItem * tmp = m_inputs[_mixerid].get();

}

void CAUDIO_MIXER::SetReverbEnable(int _mixerid, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    AudioMixerInputItem * tmp = m_inputs[_mixerid].get();


}

void CAUDIO_MIXER::SetDelay(int _mixerid, float _delay, float _feedback) {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    AudioMixerInputItem * tmp = m_inputs[_mixerid].get();

}

void CAUDIO_MIXER::SetDelayEnable(int _mixerid, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    AudioMixerInputItem * tmp = m_inputs[_mixerid].get();

}

void CAUDIO_MIXER::SelMixetOut(int _mixerid, MIXER_OUTPUTS _out) {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    AudioMixerInputItem * tmp = m_inputs[_mixerid].get();
    tmp->mixerout_idx = _out;
    tmp->isSolo = false;
    CheckSolo(_mixerid);
}

void CAUDIO_MIXER::SetInputMixerID(int _idx, int _mixerid) {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    m_inputs[_idx]->mixerout_idx = _mixerid;
}

std::vector<float> CAUDIO_MIXER::GetInputVolumes() {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    std::vector<float> tmp((50));
    int inputidx = 0;
    for (auto& input : m_inputs) {
        tmp[inputidx] = input->levelL;
        tmp[inputidx+1] = input->levelR;
        inputidx=inputidx+2;
    }
    return tmp;
}

std::vector<float> CAUDIO_MIXER::GetOutputVolumes() {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    std::vector<float> tmp((8));

    AudioMixerOutputItem * _output_master = m_outputs[0].get();
    AudioMixerOutputItem * _aux_master = m_outputs[1].get();

    tmp[0] = _output_master->levelL;
    tmp[1] = _output_master->levelR;
    tmp[2] = _output_master->prelimiterL;
    tmp[3] = _output_master->prelimiterR;
    tmp[4] = _aux_master->levelL;
    tmp[5] = _aux_master->levelR;
    tmp[6] = _aux_master->prelimiterL;
    tmp[7] = _aux_master->prelimiterR;

    return tmp;
}


void CAUDIO_MIXER::Mixer_NDI_Output(int _mixeroutid,bool _status) {
    if (_status) {
        std::string output_name_str = "Dejavis Audio Mixer - ";
        output_name_str = output_name_str + m_outputs[_mixeroutid]->name;
        m_outputs[_mixeroutid].get()->ndi_audio_out.Init_AudioOnly(output_name_str);

    }else {
        m_outputs[_mixeroutid].get()->ndi_audio_out.Stop_Audio();
    }
}

CAUDIO_MIXER::~CAUDIO_MIXER() {

    try {
        Stop();
    } catch (const std::exception& e) {
        DEJAVISUI_LOG_DEBUG("Eccezione in ~CAUDIO_MIXER: %s\n", e.what());
    } catch (...) {
        DEJAVISUI_LOG_DEBUG("Eccezione sconosciuta in ~CAUDIO_MIXER\n");
    }
}

void CAUDIO_MIXER::Stop() {
    bool expected = false;
    if (!m_stopped.compare_exchange_strong(expected, true)) {
        return;
    }

    DEJAVISUI_LOG_DEBUG("CAUDIO_MIXER::Stop() avviato\n");
    m_stopping.store(true, std::memory_order_release);

    try {
        mix_thread_pool.waitForAll(3);
    } catch (...) {

    }

    std::lock_guard<std::mutex> lock(m_mixer_mutex);

    for (size_t i = 0; i < m_inputs.size(); ++i) {
        auto* in = m_inputs[i].get();
        if (!in) continue;

        in->isActive = false;
        in->type     = -1;

        if (in->inputStream) {
            if (Pa_IsStreamActive(in->inputStream) == 1) {
                Pa_StopStream(in->inputStream);
            }
            Pa_CloseStream(in->inputStream);
            in->inputStream = nullptr;
        }

        if (in->fileplayer) {
            in->fileplayer->Stop();
            delete in->fileplayer;
            in->fileplayer = nullptr;
        }

        if (in->ndi_source) {
            delete in->ndi_source;
            in->ndi_source = nullptr;
        }

    }

    for (auto& out : m_outputs) {
        if (!out) continue;
        if (out->ndi_audio_out.isRunning()) {
            out->ndi_audio_out.Stop_Audio();
        }
    }

    m_inputs.clear();
    m_outputs.clear();
    RTCAudio = nullptr;

    DEJAVISUI_LOG_DEBUG("CAUDIO_MIXER::Stop() completato\n");
}

/*
void CAUDIO_MIXER::setGainFactor(int _mixerid,GainPreset _gainpreset) {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    m_inputs[_mixerid]->gainPreset = _gainpreset;
}
*/

Json::Value CAUDIO_MIXER::GetStatus() {
    std::lock_guard<std::mutex> lock(m_mixer_mutex);
    Json::Value status;
    status["samplerate"] = master_samplerate;
    status["channels"] = master_channels;
    //status["master_volume"] = MasterOutput.volume;
    Json::Value list(Json::arrayValue);
    for (const auto& input : m_inputs) {

        Json::Value item;
        item["name"] = input->name;
        item["mixerout_idx"] = input->mixerout_idx;
        item["samplerate"] = input->source_sampleRate;
        item["type"] = input->type  ;
        item["active"] = input->isActive;
        item["type"] = input->type;
        item["live"] = input->isLive;
        item["muted"] = input->isMuted;
        item["solo"] = input->isSolo;
        item["pan"] = input->pan;
        item["volume"] = input->volume;
        item["gain_preset"] = (int)input->gainPreset;
        item["buffer_size"] = (int)input->buffer_planar->getCapacity()*input->buffer_planar->getChannels();
        item["buffer_used"] = (int)input->buffer_planar->getAvailableRead()*input->buffer_planar->getChannels();

        item["videomixer_idx"] = input->videomixer_idx;



        if (input->fileplayer != nullptr) {
            item["fileplayer"] = input->fileplayer->getJsonStatus();
        }


        list.append(item);
    }
    status["inputs"] = list;

    Json::Value masterout;
    masterout["name"] = m_outputs[0]->name;
    masterout["audio_device_id"] = m_outputs[0]->audio_dev_id;
    masterout["audio_dev_name"] = m_outputs[0]->audio_dev_name;
    masterout["overflowCount"] = m_outputs[0]->overflowCount;
    masterout["underflowCount"] = m_outputs[0]->underflowCount;
    masterout["samplerate"] = m_outputs[0]->samplerate;
    masterout["channels"] = m_outputs[0]->channels;
    masterout["ndi_output"] =  m_outputs[0]->ndi_audio_out.isRunning();


    masterout["pan"] = m_outputs[0]->pan;
    masterout["volume"] = m_outputs[0]->volume;
    masterout["buffer_size"] = (int)m_outputs[0]->buffer->getCapacity();
    masterout["buffer_used"] = (int)m_outputs[0]->buffer->getAvailableRead();

    Json::Value auxout;
    auxout["name"] = m_outputs[1]->name;
    auxout["audio_device_id"] = m_outputs[1]->audio_dev_id;
    auxout["audio_dev_name"] = m_outputs[1]->audio_dev_name;

    auxout["overflowCount"] = m_outputs[1]->overflowCount;
    auxout["underflowCount"] = m_outputs[1]->underflowCount;
    auxout["samplerate"] = m_outputs[1]->samplerate;
    auxout["channels"] = m_outputs[1]->channels;
    auxout["ndi_output"] =  m_outputs[1]->ndi_audio_out.isRunning();

    auxout["pan"] = m_outputs[1]->pan;
    auxout["volume"] = m_outputs[1]->volume;
    masterout["buffer_size"] = (int)m_outputs[1]->buffer->getCapacity();
    masterout["buffer_used"] = (int)m_outputs[1]->buffer->getAvailableRead();

    status["master_output"] = masterout;
    status["aux_output"] = auxout;


    if (RTCAudio) {
        status["rtcaudio_selmixer"] = RTCAudio->mixer_sel_value.load();
    }else {
        status["rtcaudio_selmixer"] = -1;
    }
    return status;
}