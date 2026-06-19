#include "unimixer.h"
#include <memory>
#include "multichannel_ringbuffer.h"


cunimixer::cunimixer() {

}

cunimixer::~cunimixer() {

}

void cunimixer::Init(CAudio *_audio, CRenderer *_video) {
    audio_ref = _audio;
    video_ref = _video;
}

bool cunimixer::AddAudioPlayer(int _mixerd) {
    int _testaudioid = _mixerd;
    if (_testaudioid == -1) {
        _testaudioid = audio_ref->AUDIO_MIXER.GetFirstFreeSlot();
    }
    if (_testaudioid == -1) {
        return false;
    }

    AudioMixerInputItem * input_item = audio_ref->AUDIO_MIXER.getMixerInputItem(_testaudioid);

    if (!input_item) return false;
    if (input_item->fileplayer)return false;
    input_item->fileplayer = new caudioplayer();
    input_item->fileplayer->Init(48000,2,input_item->buffer_planar.get());
    input_item->fileplayer->SetFileBroswerBasePath(audio_base_path);
    input_item->isActive = true;
    input_item->mixerout_idx = 0;
    input_item->type = 0;;
    audio_ref->AUDIO_MIXER.CheckSolo(0);
    return true;
}


bool cunimixer::AddVideoFilePlayer(int _audio_mixer_id) {
    int slot = video_ref->FindFreeVideoMixerSlot();
    if (slot < 0) {
        return false;
    }

    int _testaudioid = _audio_mixer_id;
    if (_testaudioid == -1) {
        _testaudioid = audio_ref->AUDIO_MIXER.GetFirstFreeSlot();
    }
    if (_testaudioid == -1) {
        return false;
    }

    AudioMixerInputItem * input_item = audio_ref->AUDIO_MIXER.getMixerInputItem(_testaudioid);
    if (input_item->fileplayer || input_item->isActive)return false;


    video_ref->videoMixerTextures[slot].inUse = true;
    video_ref->videoMixerTextures[slot].layer = 0;
    video_ref->videoMixerTextures[slot].isVisible = true;
    video_ref->videoMixerTextures[slot].type = 2;

    input_item->isActive = true;
    input_item->mixerout_idx = 0;
    input_item->type = 3;
    audio_ref->AUDIO_MIXER.CheckSolo(0);

    input_item->videomixer_idx = slot;
    video_ref->videoMixerTextures[slot].AV_DECODER = new CAV_DECODER();
    video_ref->videoMixerTextures[slot].AV_DECODER->SetFileBroswerBasePath(video_base_path);
    video_ref->videoMixerTextures[slot].AV_DECODER->InitDecoder(&video_ref->m_ctx,input_item->buffer_planar.get(),48000,2);
    video_ref->videoMixerTextures[slot].AV_DECODER->InitFFmpegVulkanHW();

    return true;
}

void cunimixer::RemoveVideoFilePlayer(int _audio_mixer_id) {
    DEJAVISUI_LOG_DEBUG("REMOVING VIDEOFILEPLAYER FROM AUDIO MIXERID: %d",_audio_mixer_id);

    CAV_DECODER* decoderToDelete = nullptr;
    int videoslot = audio_ref->AUDIO_MIXER.getMixerInputItem(_audio_mixer_id)->videomixer_idx;

    {
        std::lock_guard<std::mutex> lock(video_ref->m_videoMixerMutex);

        decoderToDelete = video_ref->videoMixerTextures[videoslot].AV_DECODER;
        video_ref->videoMixerTextures[videoslot].AV_DECODER = nullptr;
        int originalidx = video_ref->videoMixerTextures[videoslot].originalIdx;
        video_ref->videoMixerTextures[videoslot] = videomixeritem();
        video_ref->videoMixerTextures[videoslot].originalIdx = originalidx;

    }
    if (decoderToDelete) {
        decoderToDelete->cleanup();
    }
    audio_ref->AUDIO_MIXER.RemoveGenericToMixer(_audio_mixer_id);  // libera ring
    if (decoderToDelete) {
        delete decoderToDelete;
    }


}

bool cunimixer::AddImage(const unsigned char* img_data,int img_size,bool isHDR) {
    int slot = video_ref->FindFreeVideoMixerSlot();
    if (slot < 0) {
        return false;
    }
    video_ref->videoMixerTextures[slot].img_viewver = new cimage_viewer();
    video_ref->videoMixerTextures[slot].img_viewver->Init(&video_ref->m_ctx);
    video_ref->videoMixerTextures[slot].img_viewver->Vulkan_LoadTexture_FromMemory(&video_ref->videoTextures[slot],img_data, img_size,isHDR);
    video_ref->videoMixerTextures[slot].inUse = true;
    video_ref->videoMixerTextures[slot].layer = 0;
    video_ref->videoMixerTextures[slot].isVisible = true;
    video_ref->videoMixerTextures[slot].type = 1;
    return true;
}

bool cunimixer::AddNDI(int _audio_mixer_id) {
    int slot = video_ref->FindFreeVideoMixerSlot();
    if (slot < 0) {
        return false;
    }
    int _testaudioid = _audio_mixer_id;
    if (_testaudioid == -1) {
        _testaudioid = audio_ref->AUDIO_MIXER.GetFirstFreeSlot();
    }
    if (_testaudioid == -1) {
        return false;
    }

    AudioMixerInputItem * input_item = audio_ref->AUDIO_MIXER.getMixerInputItem(_testaudioid);



    input_item->isActive = true;
    input_item->mixerout_idx = 0;
    input_item->type = 5;
    audio_ref->AUDIO_MIXER.CheckSolo(0);
    input_item->videomixer_idx = slot;

    video_ref->videoMixerTextures[slot].inUse = true;
    video_ref->videoMixerTextures[slot].layer = 0;
    video_ref->videoMixerTextures[slot].isVisible = true;
    video_ref->videoMixerTextures[slot].type = 5;
    video_ref->videoMixerTextures[slot].audiomixerid = _testaudioid;
    video_ref->videoMixerTextures[slot].ndi_receiver = new CNDIReceiver();
    video_ref->videoMixerTextures[slot].ndi_receiver->Init_VideoAudio(&video_ref->m_ctx,&video_ref->videoTextures[slot],input_item->buffer_planar.get(),48000,2);
    return true;
}

void cunimixer::SetKeyer(int _mixeridx,FxKeyerMode _keyer) {
    std::lock_guard<std::mutex> lock(video_ref->m_videoMixerMutex);
    video_ref->videoMixerTextures[_mixeridx].keyerMode = FxKeyerMode::Luma;
    if (_mixeridx >=0 && _mixeridx < 10) {
        if(video_ref->videoMixerTextures[_mixeridx].AV_DECODER) {
            video_ref->videoMixerTextures[_mixeridx].AV_DECODER->setKeyer(_keyer);

        }
        if(video_ref->videoMixerTextures[_mixeridx].img_viewver) {
            video_ref->videoMixerTextures[_mixeridx].img_viewver->setKeyer(_keyer);
        }
        if(video_ref->videoMixerTextures[_mixeridx].AV_STREAM_DECODER) {
            video_ref->videoMixerTextures[_mixeridx].AV_STREAM_DECODER->setKeyer(_keyer);
        }
        if(video_ref->videoMixerTextures[_mixeridx].ndi_receiver) {
            video_ref->videoMixerTextures[_mixeridx].ndi_receiver->setKeyer(_keyer);
        }
    }

}

void cunimixer::SetLumaKey(int _mixeridx,LumaKeyParams &params) {

    if(params.enabled >0.0) {
        SetKeyer(_mixeridx,FxKeyerMode::Luma);
    }

    std::lock_guard<std::mutex> lock(video_ref->m_videoMixerMutex);

    if (_mixeridx >=0 && _mixeridx < 10) {

        if(_mixeridx == 0) { //projectM
            if (video_ref->m_projectm_wrapper) {
                video_ref->m_projectm_wrapper->setLumaKey(params);
            }
        }

        video_ref->videoMixerTextures[_mixeridx].lumaParams = params;
        if(video_ref->videoMixerTextures[_mixeridx].AV_DECODER) {
            video_ref->videoMixerTextures[_mixeridx].AV_DECODER->setLumaKey(params);

        }
        if(video_ref->videoMixerTextures[_mixeridx].img_viewver) {
            video_ref->videoMixerTextures[_mixeridx].img_viewver->setLumaKey(params);

        }
        if(video_ref->videoMixerTextures[_mixeridx].AV_STREAM_DECODER) {
            video_ref->videoMixerTextures[_mixeridx].AV_STREAM_DECODER->setLumaKey(params);

        }
        if(video_ref->videoMixerTextures[_mixeridx].ndi_receiver) {
            video_ref->videoMixerTextures[_mixeridx].ndi_receiver->setLumaKey(params);

        }


    }
}

void cunimixer::SetChromaKey(int _mixeridx,KeyerPushConstants &params) {

    if(params.enabled >0.0) {
        SetKeyer(_mixeridx,FxKeyerMode::Chroma);
    }

    std::lock_guard<std::mutex> lock(video_ref->m_videoMixerMutex);

    if (_mixeridx >=0 && _mixeridx < 10) {

        video_ref->videoMixerTextures[_mixeridx].chromaParams = params;

        if(_mixeridx == 0) { //projectM
            if (video_ref->m_projectm_wrapper) {
                video_ref->m_projectm_wrapper->setChromaKey(params);
            }
        }

        if(video_ref->videoMixerTextures[_mixeridx].AV_DECODER) {
            video_ref->videoMixerTextures[_mixeridx].AV_DECODER->setChromaKey(params);
        }
        if(video_ref->videoMixerTextures[_mixeridx].img_viewver) {
            video_ref->videoMixerTextures[_mixeridx].img_viewver->setChromaKey(params);
        }
        if(video_ref->videoMixerTextures[_mixeridx].AV_STREAM_DECODER) {
            video_ref->videoMixerTextures[_mixeridx].AV_STREAM_DECODER->setChromaKey(params);
        }
        if(video_ref->videoMixerTextures[_mixeridx].ndi_receiver) {
            video_ref->videoMixerTextures[_mixeridx].ndi_receiver->setChromaKey(params);
        }

    }
}

void cunimixer::SetColor(int _mixeridx, ColorParams &params) {
    std::lock_guard<std::mutex> lock(video_ref->m_videoMixerMutex);
    if (_mixeridx >=0 && _mixeridx < 10) {

        if(_mixeridx == 0) { //projectM
            if (video_ref->m_projectm_wrapper) {
                video_ref->m_projectm_wrapper->setColor(params);
            }
        }

        video_ref->videoMixerTextures[_mixeridx].colorParams = params;
        if(video_ref->videoMixerTextures[_mixeridx].AV_DECODER) {
            video_ref->videoMixerTextures[_mixeridx].AV_DECODER->setColor(params);
        }
        if(video_ref->videoMixerTextures[_mixeridx].img_viewver) {
            video_ref->videoMixerTextures[_mixeridx].img_viewver->setColor(params);
        }
        if(video_ref->videoMixerTextures[_mixeridx].AV_STREAM_DECODER) {
            video_ref->videoMixerTextures[_mixeridx].AV_STREAM_DECODER->setColor(params);
        }

    }
}

void cunimixer::SetVideoMixerProps(videomixeritem &prop,int _viddeo_mixerid) {

    video_ref->videoMixerTextures[_viddeo_mixerid].pos_x = prop.pos_x;
    video_ref->videoMixerTextures[_viddeo_mixerid].pos_y = prop.pos_y;
    video_ref->videoMixerTextures[_viddeo_mixerid].scale_x = prop.scale_x;
    video_ref->videoMixerTextures[_viddeo_mixerid].scale_y = prop.scale_y;
    video_ref->videoMixerTextures[_viddeo_mixerid].alpha = prop.alpha;
    video_ref->videoMixerTextures[_viddeo_mixerid].layer = prop.layer;

}


void cunimixer::SetInputVolume(int _idx,float _val) {
    if (_idx == -1) return;
    if (_val < 0.0f || _val >1.0f)return;
    std::lock_guard<std::mutex> lock(audio_ref->AUDIO_MIXER.m_mixer_mutex);
    AudioMixerInputItem * myinput =  audio_ref->AUDIO_MIXER.getMixerInputItem(_idx);
    if (myinput == nullptr) return;
    myinput->volume = _val;
}

void cunimixer::setInputGainFactor(int _mixerid,GainPreset _gainpreset) {
    std::lock_guard<std::mutex> lock(audio_ref->AUDIO_MIXER.m_mixer_mutex);
    AudioMixerInputItem * myinput =  audio_ref->AUDIO_MIXER.getMixerInputItem(_mixerid);
    if (myinput == nullptr) return;
    myinput->gainPreset = _gainpreset;
}

void cunimixer::SetInputPan(int _idx,float _val) {
    if (_idx == -1) return;
    if (_val < -1.0f || _val > 1.0f)return;
    std::lock_guard<std::mutex> lock(audio_ref->AUDIO_MIXER.m_mixer_mutex);
    AudioMixerInputItem * myinput =  audio_ref->AUDIO_MIXER.getMixerInputItem(_idx);
    if (myinput == nullptr) return;
    myinput->pan = _val;
}

void cunimixer::SetMasterPan(float _val) {
    AudioMixerOutputItem * myoutput = audio_ref->AUDIO_MIXER.getMixerOutputItem(0);
    if (myoutput == nullptr) return;
    myoutput->pan = _val;
}

void cunimixer::SetAuxPan(float _val) {
    AudioMixerOutputItem * myoutput = audio_ref->AUDIO_MIXER.getMixerOutputItem(1);
    if (myoutput == nullptr) return;
    myoutput->pan = _val;
}

void cunimixer::SetMasterVolume(float _val) {
    if (_val < 0.0f || _val >1.0f)return;
    AudioMixerOutputItem * myoutput = audio_ref->AUDIO_MIXER.getMixerOutputItem(0);
    if (myoutput == nullptr) return;
    myoutput->volume = _val;
}

void cunimixer::SetAuxVolume(float _val) {
    if (_val < 0.0f || _val >1.0f)return;
    AudioMixerOutputItem * myoutput = audio_ref->AUDIO_MIXER.getMixerOutputItem(1);
    if (myoutput == nullptr) return;
    myoutput->volume = _val;
}

void cunimixer::SetInputSolo(int _idx, bool _solo) {
    audio_ref->AUDIO_MIXER.SetInputSolo(_idx, _solo);
}

void cunimixer::SetInputMute(int _idx, bool _muted) {
    AudioMixerInputItem * myinput =  audio_ref->AUDIO_MIXER.getMixerInputItem(_idx);
    if (myinput == nullptr) return;
    myinput->isMuted = _muted;
}

void cunimixer::SetInputMixerID(int _idx, int _mixerid) {
    AudioMixerInputItem * myinput =  audio_ref->AUDIO_MIXER.getMixerInputItem(_idx);
    if (myinput == nullptr) return;
    myinput->mixerout_idx = _mixerid;
}

void cunimixer::AddInputAudioFX(int _idx, audio_utils::EffectBank::EffectType _effecttype) {
    if (_effecttype == audio_utils::EffectBank::EffectType::Limiter) {
        audio_utils::EffectBank::SlotConfig cfg;
        cfg.type = audio_utils::EffectBank::EffectType::Limiter;
        cfg.limiter.sampleRate = 48000;
        cfg.limiter.limit      = 0.95f;
        cfg.limiter.attackMs   = 1.0f;
        cfg.limiter.releaseMs  = 50.0f;
        cfg.limiter.autoLevel  = false;
        cfg.limiter.asc        = true;
        cfg.meterDecaySec      = 0.1f;
        audio_ref->AUDIO_MIXER.input_effectBank.AddEffectToSlot(_idx, cfg);
    }
    if (_effecttype == audio_utils::EffectBank::EffectType::Echo) {
        audio_utils::EffectBank::SlotConfig cfg;
        cfg.type = audio_utils::EffectBank::EffectType::Echo;
        cfg.echo.sampleRate = 48000;
        cfg.echo.delayMs    = 45.0f;
        cfg.echo.decay       = 0.5f;
        cfg.echo.outAmplitude = 0.8f;
        audio_ref->AUDIO_MIXER.input_effectBank.AddEffectToSlot(_idx, cfg);

    }
    if (_effecttype == audio_utils::EffectBank::EffectType::Atempo) {
        audio_utils::EffectBank::SlotConfig cfg;
        cfg.type = audio_utils::EffectBank::EffectType::Atempo;
        cfg.atempo.sampleRate = 48000;
        cfg.atempo.tempo = 0.5f;
        audio_ref->AUDIO_MIXER.input_effectBank.AddEffectToSlot(_idx, cfg);

    }
}

void cunimixer::RemoveInputAudioFX(int _idx, int _fxpos) {
    audio_ref->AUDIO_MIXER.input_effectBank.RemoveEffectFromSlot(_idx, _fxpos);
}

void cunimixer::ReconfigureInputFX(int _idx, int _fxpos, audio_utils::EffectBank::SlotConfig mycfg) {
    audio_ref->AUDIO_MIXER.input_effectBank.ReconfigureEffect(_idx,_fxpos,mycfg);
}
