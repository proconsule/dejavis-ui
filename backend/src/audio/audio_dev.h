#ifndef DEJAVIS_UI_AUDIO_DEV_H
#define DEJAVIS_UI_AUDIO_DEV_H

#include <cstdint>
#include <string>
#include <functional>
#include "../ringbuffer.h"
#include <portaudio.h>

#include "backend/src/logger.h"


using ClockTickCallback = std::function<void(unsigned long frames)>;

struct cdejavisaudio_dev_buffer_struct {
    std::vector<RingBuffer*> buffers;
    RingBuffer* master_output_buffer = nullptr;
    int32_t _channels;
};

class cdejavisaudio_dev {
public:
    cdejavisaudio_dev();
    ~cdejavisaudio_dev();
    bool InitHW(int deviceId,uint32_t _channels,uint32_t _samplerate);

    void StopHW();

    std::string name;

    void addInputBuffer(RingBuffer* _buffer) {
        if (_buffer != nullptr) {
            bool gia_presente = false;
            for (auto b : input_data.buffers) {
                if (b == _buffer) {
                    gia_presente = true;
                    break;
                }
            }

            if (!gia_presente) {
                input_data.buffers.push_back(_buffer);
                DEJAVISUI_LOG_DEBUG("[AUDIO] Added buffer to clock synchronization.");
            } else {
                DEJAVISUI_LOG_DEBUG("[AUDIO] Buffer already registered for clock sync, skipping duplicate.");
            }
        }
    };

    void setInputBuffer(RingBuffer * _buffer) {
        input_buffer = _buffer;
        DEJAVISUI_LOG_DEBUG("Setting Input Buffer");
    };

    void setMasterOutputBuffer(RingBuffer* _buffer) {
        input_data.master_output_buffer = _buffer;
        addInputBuffer(_buffer);
        DEJAVISUI_LOG_DEBUG("[AUDIO] Master output buffer assigned.");
    };

    void removeInputBuffer(RingBuffer* _buffer) {
        if (_buffer != nullptr) {
            auto& bins = input_data.buffers;
            auto it = std::remove(bins.begin(), bins.end(), _buffer);

            if (it != bins.end()) {
                bins.erase(it, bins.end());
                DEJAVISUI_LOG_DEBUG("[AUDIO] Buffer removed from clock synchronization.");
            } else {
                DEJAVISUI_LOG_DEBUG("[AUDIO] Buffer to remove not found in clock sync.");
            }

            if (input_data.master_output_buffer == _buffer) {
                input_data.master_output_buffer = nullptr;
                DEJAVISUI_LOG_DEBUG("[AUDIO] Removed buffer was the master output. Master output reset to nullptr.");
            }
        }
    };

    void clearBuffers() {
        input_data.buffers.clear();
        input_data.master_output_buffer = nullptr;
    };

private:
    bool running;
    PaStreamParameters params;
    PaStream *outputStream = nullptr;
    bool master_clock = false;

    RingBuffer * input_buffer;


    cdejavisaudio_dev_buffer_struct input_data;

};


#endif