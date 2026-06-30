#include "audio_dev.h"

#include "backend/src/logger.h"


cdejavisaudio_dev::cdejavisaudio_dev() {
    master_clock = false;
    running = false;
}

cdejavisaudio_dev::~cdejavisaudio_dev() {
    if (outputStream) {
        Pa_StopStream(outputStream);
        Pa_CloseStream(outputStream);
    }
}

static int audioDeviceOutputCallback(const void *inputBuffer, void *outputBuffer,
                                     unsigned long framesPerBuffer,
                                     const PaStreamCallbackTimeInfo* timeInfo,
                                     PaStreamCallbackFlags statusFlags,
                                     void *userData) {

    if (outputBuffer != nullptr) {
        cdejavisaudio_dev_buffer_struct* data = static_cast<cdejavisaudio_dev_buffer_struct*>(userData);
        float* out = static_cast<float*>(outputBuffer);
        unsigned long totalSamples = framesPerBuffer * data->_channels;

        std::fill_n(out, totalSamples, 0.0f);
        std::vector<float> discard(totalSamples);
        for (RingBuffer* buf : data->buffers) {
            if (buf != nullptr) {
                if (buf->getAvailableRead() >= totalSamples) {

                    if (buf == data->master_output_buffer) {
                        buf->read(out, totalSamples);
                    } else {

                        buf->read(discard.data(), totalSamples);
                    }

                } else {


                }
            }
        }
    }

    return paContinue;
}

bool cdejavisaudio_dev::InitHW(int deviceId, uint32_t _channels, uint32_t _samplerate) {
    if (running || outputStream != nullptr) {
        StopHW();
    }
    PaStreamParameters outputParams;
    outputParams.device = deviceId;
    outputParams.channelCount = _channels;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency = Pa_GetDeviceInfo(deviceId)->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = nullptr;
    name = Pa_GetDeviceInfo(deviceId)->name;

    input_data._channels = _channels;

    PaError err = Pa_OpenStream(&outputStream, nullptr, &outputParams, _samplerate, 256, paClipOff, audioDeviceOutputCallback, &input_data);
    if (err != paNoError) {
        DEJAVISUI_LOG_INFO("[AUDIO DEVICE] Error: %s", Pa_GetErrorText(err));
        return false;
    }

    DEJAVISUI_LOG_INFO("[AUDIO DEVICE] Opening %s, %d Hz, Master Clock Active", name.c_str(), _samplerate);
    master_clock = true;
    return Pa_StartStream(outputStream) == paNoError;
}

void cdejavisaudio_dev::StopHW() {
    if (outputStream != nullptr) {
        DEJAVISUI_LOG_INFO("[AUDIO DEVICE] Stopping stream for %s...", name.c_str());

        PaError err = Pa_StopStream(outputStream);
        if (err != paNoError && err != paStreamIsStopped) {
            DEJAVISUI_LOG_INFO("[AUDIO DEVICE] Warning while stopping stream: %s", Pa_GetErrorText(err));
        }

        err = Pa_CloseStream(outputStream);
        if (err != paNoError) {
            DEJAVISUI_LOG_INFO("[AUDIO DEVICE] Error closing stream: %s", Pa_GetErrorText(err));
        }

        outputStream = nullptr;
    }

    running = false;
    master_clock = false;

    DEJAVISUI_LOG_INFO("[AUDIO DEVICE] Hardware stopped");
}