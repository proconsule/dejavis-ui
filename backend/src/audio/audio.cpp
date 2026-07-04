#include "audio.h"
#include <ostream>
#include <iostream>

#include "audio_mixer.h"
#include "backend/src/logger.h"

std::vector<double> standardRates = {
    8000.0,
    16000.0,
    32000.0,
    44100.0,
    48000.0,
    88200.0, // This is just for fun...useless since i do all at 48KHz
    96000.0, // This is just for fun...useless since i do all at 48KHz
    176400.0, // This is just for fun...useless since i do all at 48KHz
    192000.0, // This is just for fun...useless since i do all at 48KHz
    384000.0  // This is just for fun...useless since i do all at 48KHz
};


static int audioInputCallback(const void *inputBuffer, void *outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void *userData) {

    if (inputBuffer != nullptr && userData != nullptr) {
        AudioMixerInputItem* mixer_item = (AudioMixerInputItem*)userData;
        MultiChannelRingBuffer* out = mixer_item->buffer_planar.get();

        AVFrame* converted = mixer_item->Resampler.processInterleaved(
            (const float*)inputBuffer, framesPerBuffer);

        if (converted) {

            if (out->getAvailableWrite() < (size_t)converted->nb_samples) {
                out->skip(converted->nb_samples);
            }
            out->write(reinterpret_cast<const float* const*>(converted->data),
                       converted->nb_samples);
        }
    }

    return paContinue;

}

CAudio::CAudio(){

	lastError = Pa_Initialize();
	if (lastError != paNoError) {
	    DEJAVISUI_LOG_ERROR("PortAudio initialization failed");
	}
	refreshDevices();

}


void CAudio::refreshDevices() {
	clearLists();
	int numDevices = Pa_GetDeviceCount();
	defaultIn = Pa_GetDefaultInputDevice();
	defaultOut = Pa_GetDefaultOutputDevice();

	for (int i = 0; i < numDevices; ++i) {
		const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
		const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(info->hostApi);

		AudioDevice dev;
		dev.id = i;
		dev.name = info->name;
		dev.hostApiName = apiInfo->name;
		dev.maxInputChannels = info->maxInputChannels;
		dev.maxOutputChannels = info->maxOutputChannels;

		dev.defaultSampleRate = info->defaultSampleRate;
        dev.maxSampleRate = info->defaultSampleRate;

		if(i == defaultIn)dev.isDefaultInput = true;
		if(i == defaultOut)dev.isDefaultOutput = true;

        for (double rate : standardRates) {
            PaStreamParameters params;
            params.device = i;
            params.channelCount = (info->maxInputChannels > 0) ? info->maxInputChannels : info->maxOutputChannels;
            params.sampleFormat = paFloat32;
            params.suggestedLatency = 0;
            params.hostApiSpecificStreamInfo = nullptr;

            PaError err;
            if (info->maxInputChannels > 0) {
                err = Pa_IsFormatSupported(&params, nullptr, rate);
            } else {
                err = Pa_IsFormatSupported(nullptr, &params, rate);
            }

            if (err == paFormatIsSupported) {
                if (rate > dev.maxSampleRate) {
                    dev.maxSampleRate = rate;
                }
            }
        }

        if (info->maxInputChannels > 0) inputDevices.push_back(dev);
        if (info->maxOutputChannels > 0) outputDevices.push_back(dev);

	}

    DEJAVISUI_LOG_INFO("[AUDIO] Found %d audio input devices",inputDevices.size());
    DEJAVISUI_LOG_INFO("[AUDIO] Found %d audio output devices",outputDevices.size());
}


void CAudio::Print_Audio_List(){


	std::cout << "Input Devices: " << std::endl;
	std::cout << "----------------------" << std::endl;
	int numDevices = inputDevices.size();
	for (int i = 0; i < numDevices; ++i) {
		std::cout << "ID: " << inputDevices[i].id << "Name: " << inputDevices[i].name << " API: " << inputDevices[i].hostApiName <<std::endl;
		std::cout << "\t" << "Def Samplerate: " << inputDevices[i].defaultSampleRate << std::endl;
	}
	std::cout << "Output Devices: " << std::endl;
	std::cout << "----------------------" << std::endl;
	numDevices = outputDevices.size();
	for (int i = 0; i < numDevices; ++i) {
		std::cout << "ID: " << outputDevices[i].id << " Name: " << outputDevices[i].name << " API: " << outputDevices[i].hostApiName <<std::endl;
		std::cout << "\t" << "Def Samplerate: " << outputDevices[i].defaultSampleRate << std::endl;
	}
	std::cout << "----------------------" << std::endl;
}

int CAudio::startMixerInput(int mixer_id,int deviceId,uint32_t _channels,uint32_t _samplerate) {

    std::lock_guard<std::mutex> lock(audioMutex);

    AudioMixerInputItem * mixer_item  = AUDIO_MIXER.getMixerInputItem( mixer_id );

    if (!mixer_item->Resampler.init(_samplerate, AUDIO_MIXER.master_samplerate, _channels, 2)) {
        DEJAVISUI_LOG_ERROR("Errore inizializzazione resampler input");
        return false;
    }

    PaStreamParameters inputParams;
    inputParams.device = deviceId;
    inputParams.channelCount = _channels;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = Pa_GetDeviceInfo(deviceId)->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(&mixer_item->inputStream, &inputParams, nullptr, _samplerate, 128*_channels, paClipOff, audioInputCallback, mixer_item);
    if (err != paNoError) return -1;

    mixer_item->isActive = true;
    mixer_item->type = 2;
    mixer_item->mixerout_idx = 0;

    return Pa_StartStream(mixer_item->inputStream) == paNoError ? mixer_id : -1;
}

void CAudio::stopMixerInput(int _mixerid) {

    std::lock_guard<std::mutex> lock(audioMutex);

    AudioMixerInputItem *mixer_item = AUDIO_MIXER.getMixerInputItem(_mixerid);
    if (!mixer_item) return;

    if (mixer_item->inputStream) {
        if (Pa_IsStreamActive(mixer_item->inputStream) == 1) {
            Pa_StopStream(mixer_item->inputStream);
        }
        Pa_CloseStream(mixer_item->inputStream);
        mixer_item->inputStream = nullptr;
    }
    mixer_item->Resampler.cleanup();

    AUDIO_MIXER.RemoveFromMixer(_mixerid);

}

bool CAudio::startTrashDummy() {
    std::lock_guard<std::mutex> lock(audioMutex);

    trashDummyTimerRunning = true;
    trashdummyTimerThread = std::thread(&CAudio::startTrashTimer, this);

    return true;
}


void CAudio::startTrashTimer() {
    const int64_t sampleRate = AUDIO_MIXER.master_samplerate;;
    const int channels = 2;
    const size_t chunkSize = 256;
    std::vector<float> buffer(chunkSize);
    const int64_t usecPerChunk = (1000000 * (chunkSize / channels)) / sampleRate;
    auto nextTick = std::chrono::steady_clock::now();
    while (trashDummyTimerRunning) {
        if (AUDIO_MIXER.GetUnassignedBuffer()->getAvailableRead() >= chunkSize) {

            AUDIO_MIXER.trash_buffer.get()->read(buffer.data(), chunkSize);
            nextTick += std::chrono::microseconds(usecPerChunk);
            std::this_thread::sleep_until(nextTick);

            trashdummyTimer.update();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            nextTick = std::chrono::steady_clock::now();
            trashdummyTimer.update();
        }
    }

}

bool CAudio::startAuxOutput(int deviceId,uint32_t _channels,uint32_t _samplerate) {

    if (AUDIO_MIXER.getMixerOutputItem(1)->isdummy) {
        AUDIO_MIXER.getMixerOutputItem(0)->dev_audio_out.removeInputBuffer(AUDIO_MIXER.getMixerOutputItem(1)->buffer.get());
    }

    AUDIO_MIXER.getMixerOutputItem(1)->audio_dev_id = deviceId;


    AUDIO_MIXER.getMixerOutputItem(1)->channels = _channels;
    AUDIO_MIXER.getMixerOutputItem(1)->samplerate = _samplerate;

    AUDIO_MIXER.getMixerOutputItem(1)->Resampler.init(AUDIO_MIXER.master_samplerate,_samplerate,2,_channels,AV_SAMPLE_FMT_FLTP,AV_SAMPLE_FMT_FLT);


    AUDIO_MIXER.getMixerOutputItem(1)->dev_audio_out.addInputBuffer(AUDIO_MIXER.getMixerOutputItem(1)->buffer.get());
    AUDIO_MIXER.getMixerOutputItem(1)->dev_audio_out.setMasterOutputBuffer(AUDIO_MIXER.getMixerOutputItem(1)->buffer.get());


    bool initok =  AUDIO_MIXER.getMixerOutputItem(1)->dev_audio_out.InitHW(deviceId,_channels,_samplerate);

    AUDIO_MIXER.getMixerOutputItem(1)->audio_dev_name = AUDIO_MIXER.getMixerOutputItem(1)->dev_audio_out.name;


    return initok;
}

void CAudio::startProcessing() {
    if (!isRunning) {
        isRunning = true;
        processingThread = std::thread(&CAudio::processingLoop, this);
    }
}

void CAudio::processMasterOutSamples(std::vector<float>& _block, bool* _dataprocessed) {

    MultiChannelRingBuffer* InputMasterMixed = AUDIO_MIXER.getMasterProcessBuffer();

    const size_t channels = 2;
    const size_t frames   = _block.size() ;

    if (InputMasterMixed->getAvailableRead() < frames) return;

    if (AUDIO_MIXER.GetMasterBuffer()->getAvailableWrite() < _block.size()) return;

    float* tempIn[2] = { AUDIO_MIXER.m_masterRead[0].data(), AUDIO_MIXER.m_masterRead[1].data() };
    if (!InputMasterMixed->read(tempIn, frames)) return;


    AUDIO_MIXER.ProcessMasterOutput(
        tempIn,
        frames
    );

    srtLiveBuffer_planar.write(tempIn, frames);

    if (AUDIO_MIXER.RTCAudio &&
    AUDIO_MIXER.RTCAudio->mixer_sel_value.load() == 0) {
        AUDIO_MIXER.RTCAudio->sendAudioFrame(
            reinterpret_cast<const float* const*>(tempIn),
            frames,
            channels
        );
    }
    if (m_projectm_wrapper) {
        m_projectm_wrapper->PushAudio(tempIn,frames);

    }

    auto* ndiOut = AUDIO_MIXER.getMixerOutputItem(
        CAUDIO_MIXER::MIXER_OUTPUTS::OUTPUT_MASTER);
    if (ndiOut->ndi_audio_out.isRunning()) {
        ndiOut->ndi_audio_out.PushAudio(reinterpret_cast<const float* const*>(tempIn),frames);
    }

    if (av_ndi_sender) {
        av_ndi_sender->PushAudio(reinterpret_cast<const float* const*>(tempIn),frames);
    }

    *_dataprocessed = true;
}

void CAudio::processAuxOutSamples(std::vector<float>& _block, bool* _dataprocessed) {
    MultiChannelRingBuffer* InputAuxMixed = AUDIO_MIXER.getAuxProcessBuffer();

    const size_t channels = 2;
    const size_t frames   = _block.size();

    if (InputAuxMixed->getAvailableRead() < frames) return;

    if (AUDIO_MIXER.GetAuxBuffer()->getAvailableWrite() < _block.size()) return;

    float* tempIn[2] = { AUDIO_MIXER.m_auxRead[0].data(), AUDIO_MIXER.m_auxRead[1].data() };
    if (!InputAuxMixed->read(tempIn, frames)) return;

    AUDIO_MIXER.ProcessAuxOutput(
        tempIn,
        frames
    );
    if (AUDIO_MIXER.RTCAudio &&
    AUDIO_MIXER.RTCAudio->mixer_sel_value.load() == 1) {
        AUDIO_MIXER.RTCAudio->sendAudioFrame(
            reinterpret_cast<const float* const*>(tempIn),  // float*[2] da m_masterRead
            frames,                                          // sample per canale
            channels                                          // 2 per stereo
        );
    }

    auto* ndiOut = AUDIO_MIXER.getMixerOutputItem(
        CAUDIO_MIXER::MIXER_OUTPUTS::OUTPUT_AUX);
    if (ndiOut->ndi_audio_out.isRunning()) {
        ndiOut->ndi_audio_out.PushAudio(reinterpret_cast<const float* const*>(tempIn),frames);
    }

    *_dataprocessed = true;
}

void CAudio::processTrashOutSamples( std::vector<float> &_block,bool * _dataprocessed) {
    RingBuffer * InputMixed = AUDIO_MIXER.trash_buffer.get();
    if (InputMixed->getAvailableRead() >= _block.size()) {
        if (AUDIO_MIXER.GetAuxBuffer()->getAvailableWrite() >= _block.size()) {
            InputMixed->read(_block.data(), _block.size());
            AUDIO_MIXER.ProcessUnassignedOutput(_block.data(),_block.size());

            *_dataprocessed  = true;
        }
    }
}

bool CAudio::StartAudioDev(CAUDIO_MIXER::MIXER_OUTPUTS outputnum,int deviceId,uint32_t _channels,uint32_t _samplerate) {
    int outdevidx = -1;
    int dummyclock_idx = -1;
    if (outputnum == CAUDIO_MIXER::OUTPUT_MASTER) {
        outdevidx = 0;
        dummyclock_idx = 1;
    }
    if (outputnum == CAUDIO_MIXER::OUTPUT_AUX) {
        outdevidx = 1;
        dummyclock_idx = 0;
    }
    if (outdevidx == -1)return false;

    if (AUDIO_MIXER.getMixerOutputItem(outdevidx)->isdummy) {
        AUDIO_MIXER.getMixerOutputItem(dummyclock_idx)->dev_audio_out.removeInputBuffer(AUDIO_MIXER.getMixerOutputItem(outdevidx)->buffer.get());
        AUDIO_MIXER.getMixerOutputItem(outdevidx)->isdummy = false;
    }

    AUDIO_MIXER.getMixerOutputItem(outdevidx)->audio_dev_id = deviceId;


    AUDIO_MIXER.getMixerOutputItem(outdevidx)->channels = _channels;
    AUDIO_MIXER.getMixerOutputItem(outdevidx)->samplerate = _samplerate;

    AUDIO_MIXER.getMixerOutputItem(outdevidx)->Resampler.init(AUDIO_MIXER.master_samplerate,_samplerate,2,_channels,AV_SAMPLE_FMT_FLTP,AV_SAMPLE_FMT_FLT);


    AUDIO_MIXER.getMixerOutputItem(outdevidx)->dev_audio_out.addInputBuffer(AUDIO_MIXER.getMixerOutputItem(outdevidx)->buffer.get());
    AUDIO_MIXER.getMixerOutputItem(outdevidx)->dev_audio_out.setMasterOutputBuffer(AUDIO_MIXER.getMixerOutputItem(outdevidx)->buffer.get());


    bool initok =  AUDIO_MIXER.getMixerOutputItem(outdevidx)->dev_audio_out.InitHW(deviceId,_channels,_samplerate);

    AUDIO_MIXER.getMixerOutputItem(outdevidx)->audio_dev_name = AUDIO_MIXER.getMixerOutputItem(outdevidx)->dev_audio_out.name;

    return initok;
}

bool CAudio::StartDummyDevice(CAUDIO_MIXER::MIXER_OUTPUTS outputnum,CAUDIO_MIXER::MIXER_OUTPUTS master_clocknum) {

    AUDIO_MIXER.getMixerOutputItem(outputnum)->audio_dev_id = -1;
    AUDIO_MIXER.getMixerOutputItem(outputnum)->audio_dev_name = "Dummy Clock";
    AUDIO_MIXER.getMixerOutputItem(outputnum)->samplerate = AUDIO_MIXER.getMixerOutputItem(master_clocknum)->samplerate;
    AUDIO_MIXER.getMixerOutputItem(outputnum)->channels = 2;
    AUDIO_MIXER.getMixerOutputItem(outputnum)->isdummy = true;
    AUDIO_MIXER.getMixerOutputItem(outputnum)->Resampler.init(AUDIO_MIXER.master_samplerate,AUDIO_MIXER.getMixerOutputItem(outputnum)->samplerate,2,2,AV_SAMPLE_FMT_FLTP,AV_SAMPLE_FMT_FLT);


    AUDIO_MIXER.getMixerOutputItem(master_clocknum)->dev_audio_out.addInputBuffer(AUDIO_MIXER.getMixerOutputItem(outputnum)->buffer.get());
    DEJAVISUI_LOG_INFO("Adding %s -> %s",AUDIO_MIXER.getMixerOutputItem(outputnum)->audio_dev_name.c_str(),AUDIO_MIXER.getMixerOutputItem(master_clocknum)->audio_dev_name.c_str());
    return true;
}


void CAudio::processingLoop() {
    size_t framesToProcess = 256;
    size_t channels = 2;
    size_t samplesNeeded = framesToProcess * channels;

    std::vector<float> blockMaster(samplesNeeded, 0.0f);
    std::vector<float> blockMix(samplesNeeded, 0.0f);
    std::vector<float> blockTrash(samplesNeeded, 0.0f);

    while (isRunning) {

        if (m_penedingAudioDevLoad.shouldLoad.load()) {
            m_penedingAudioDevLoad.shouldLoad.store(false);
            if (m_penedingAudioDevLoad.outputtype == 1) {

                StartAudioDev(m_penedingAudioDevLoad.outidx,m_penedingAudioDevLoad.deviceid,m_penedingAudioDevLoad.channels,m_penedingAudioDevLoad.samplerate);

            }
        }
        if (m_penedingDummyLoad.shouldLoad.load()) {
            m_penedingDummyLoad.shouldLoad.store(false);
            StartDummyDevice(m_penedingDummyLoad.device,m_penedingDummyLoad.masterclock_device);
        }


        AUDIO_MIXER.ProcessMix(samplesNeeded);

        bool dataProcessedMaster = false;
        bool dataProcessedAux = false;
        bool dataProcessedTrash = false;

        output_samples_process.enqueue([this, &blockMaster,&dataProcessedMaster]{ processMasterOutSamples(blockMaster,&dataProcessedMaster); });
        output_samples_process.enqueue([this, &blockMix,&dataProcessedAux]{ processAuxOutSamples(blockMix,&dataProcessedAux); });
        output_samples_process.enqueue([this, &blockTrash,&dataProcessedTrash]{ processTrashOutSamples(blockTrash,&dataProcessedTrash); });

        output_samples_process.waitForAll(3);

        if (!dataProcessedMaster && !dataProcessedAux && !dataProcessedTrash) {
            constexpr auto blockDuration = std::chrono::microseconds(
                static_cast<long long>((256.0 / 48000.0) * 1'000'000.0)
            );

            std::this_thread::sleep_for(blockDuration / 2); //
        } else {
            std::this_thread::yield();
        }
    }

}

Json::Value CAudio::getStatusJson() {
    Json::Value status;

    status["isRunning"] = isRunning.load();
    status["mixer"] = AUDIO_MIXER.GetStatus();
    return status;
}

Json::Value CAudio::getDevicesJson() {
    Json::Value root;

    auto serialize = [](const std::vector<AudioDevice>& devices) {
        Json::Value list(Json::arrayValue);
        for (const auto& d : devices) {
            Json::Value dev;
            dev["id"] = d.id;
            dev["name"] = d.name;
            dev["api"] = d.hostApiName;
            dev["maxIn"] = d.maxInputChannels;
            dev["maxOut"] = d.maxOutputChannels;
            dev["defaultSampleRate"] = d.defaultSampleRate;
            dev["maxSampleRate"] = d.maxSampleRate;
            dev["isDefaultInput"] = d.isDefaultInput;
            dev["isDefaultOutput"] = d.isDefaultOutput;
            list.append(dev);
        }
        return list;
    };

    root["inputs"] = serialize(inputDevices);
    root["outputs"] = serialize(outputDevices);

    return root;
}

void CAudio::stop() {
    isRunning = false;

    trashDummyTimerRunning = false;
    if (trashdummyTimerThread.joinable()) trashdummyTimerThread.join();

    ismasterDummyTimerRunning = false;
    if (masterdummyTimerThread.joinable()) masterdummyTimerThread.join();

    isauxDummyTimerRunning = false;
    if (auxdummyTimerThread.joinable()) auxdummyTimerThread.join();

    if (processingThread.joinable()) {
        processingThread.join();
    }

    AUDIO_MIXER.Stop();

}

CAudio::~CAudio() {
    stop();
	if (inputStream) {
        Pa_StopStream(inputStream);
        Pa_CloseStream(inputStream);
    }
    Pa_Terminate();
}

