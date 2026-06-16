#ifndef DEJAVISAUDIO_H
#define DEJAVISAUDIO_H

#include <stdio.h>
#include <portaudio.h>
#include <stdexcept>
#include <vector>
#include <string>
#include <cstdint>
#include "backend/src/video/projectm_wrapper.h"
#include "../ringbuffer.h"
#include "../dejatimer.h"
#include <thread>
#include <json/json.h>
#include "audio_mixer.h"
#include "audio_resampler.h"
#include "../audioplayer/audioplayer.h"
#define DEJAVIS_USE_VULKAN 1

#include "projectM-4/projectM.h"
#include <tracy/Tracy.hpp>


struct AudioDevice {
    int id;                   
    std::string name;
    int maxInputChannels;
    int maxOutputChannels;
    double defaultSampleRate;
	double maxSampleRate;
    std::string hostApiName;
    bool isDefaultInput = false;
    bool isDefaultOutput = false;
};

struct PendingAudioOutDevLoad {
	std::atomic<bool> shouldLoad{false};
	int deviceid = -1;
	int outputtype = -1;
	int samplerate = 48000;
	int channels = 2;

};

class CAudio
{
  public:
    CAudio();
	~CAudio();


	void refreshDevices();
	void Print_Audio_List();

    bool startMasterDummy();
	bool startAuxDummy();
	bool startTrashDummy();

	bool startInput(int deviceId,uint32_t _channels,uint32_t _samplerate);

	int startMixerInput(int mixer_id,int deviceId,uint32_t _channels,uint32_t _samplerate);
	void stopMixerInput(int mixerid);

	bool startMasterOutput(int deviceId,uint32_t _channels,uint32_t _samplerate);

    bool startAuxOutput(int deviceId, uint32_t _channels, uint32_t _samplerate);


    std::string fileplayers_basepath = "";
	CAUDIO_MIXER AUDIO_MIXER;

	MultiChannelRingBuffer srtLiveBuffer_planar{2,4096};

    void startProcessing();
	void stop();
	void stopInput();
	int input_mixerid = -1;
	void stopMasterOut();
	void stopAuxOut();


	bool loadAudioFile(const std::string& path);
	void addfiletoqueue(const std::string& path);
	void removefilefromqueue(uint32_t _id);
	void SetRepeatMode(uint32_t _repmode);
    void playFile();
    void pauseFile();
    void stopFile();
    void seekFile(double seconds);
	
	const AudioFileMetadata& getFileMetadata() const;
    double getFilePosition() const;
	

	Json::Value getDevicesJson();
	Json::Value getStatusJson();


	int getDefaultInput(){return defaultIn;};
	int getDefaultOutput(){return defaultOut;};

	AudioResampler dummyResampler;

	cprojectm_wrapper * m_projectm_wrapper = nullptr;
	CNDISender * av_ndi_sender = nullptr;

	PendingAudioOutDevLoad m_penedingAudioDevLoad;


private:
	PaError lastError;
	std::vector<AudioDevice> inputDevices;
    std::vector<AudioDevice> outputDevices;
	
	void clearLists() {
        inputDevices.clear();
        outputDevices.clear();
    }


	std::thread masterdummyTimerThread;
	bool ismasterDummyTimerRunning = false;
	CDEJATIMER masterdummyTimer;

	std::thread auxdummyTimerThread;
	bool isauxDummyTimerRunning = false;
	CDEJATIMER auxdummyTimer;

	void startMasterDummyTimer();
	static void MasterdummyTimerCallback(CAudio* engine);

	void startAuxDummyTimer();
	static void AuxdummyTimerCallback(CAudio* engine);


	void startTrashTimer();
	std::thread trashdummyTimerThread;
	bool trashDummyTimerRunning = false;
	CDEJATIMER trashdummyTimer;

	const std::vector<AudioDevice>& getInputDevices() const { return inputDevices; }
    const std::vector<AudioDevice>& getOutputDevices() const { return outputDevices; }
	
	PaStream *inputStream = nullptr;
    PaStream *outputStream = nullptr;
	PaStream *auxoutputStream = nullptr;
	
	std::thread processingThread;
    std::atomic<bool> isRunning{false};

	void processMasterOutSamples(std::vector<float> &_block,bool * _dataprocessed);
	void processMixOutSamples(std::vector<float> &_block,bool * _dataprocessed);
	void processTrashOutSamples( std::vector<float> &_block,bool * _dataprocessed);
	void processingLoop();
	
	int activeInputId = -1;
    int activeOutputId = -1;

	int activeAuxId = -1;

	int defaultIn = -1;
	int defaultOut = -1;

	std::mutex audioMutex;

    bool dummy = true;
    uint32_t dummySampleRate = 48000;
    uint32_t dummyChannels = 2;

	AudioThreadPool output_samples_process{3};


};

#endif