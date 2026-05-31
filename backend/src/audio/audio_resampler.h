#ifndef AUDIO_RESAMPLER_H
#define AUDIO_RESAMPLER_H

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/frame.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>

#ifdef NDI_ENABLED
    #include <Processing.NDI.Lib.h>
#endif


class AudioResampler {
public:
    AudioResampler();
    ~AudioResampler();

    bool init(int in_rate, int out_rate, int in_ch, int out_ch,
              AVSampleFormat in_fmt  = AV_SAMPLE_FMT_FLT,
              AVSampleFormat out_fmt = AV_SAMPLE_FMT_FLTP);

    void cleanup();

    AVFrame* processInterleaved(const float* input, int in_samples);

    AVFrame* processPlanar(const float* const* input, int in_samples);

    AVFrame* processFromAVFrame(AVFrame* frame);

#ifdef NDI_ENABLED
    AVFrame* processFromNDI(NDIlib_audio_frame_v2_t* aframe);
#endif

    // ---- Accesso all'output ----
    AVFrame* getOutputFrame()   const { return m_out_frame; }
    int      getOutputSamples() const { return m_out_frame ? m_out_frame->nb_samples : 0; }
    bool     isOutputPlanar()   const { return av_sample_fmt_is_planar(m_out_fmt) == 1; }

    float* getOutputChannel(int ch = 0) const {
        if (!m_out_frame || ch < 0 || ch >= AV_NUM_DATA_POINTERS) return nullptr;
        return (float*)m_out_frame->data[ch];
    }

    int            getInputChannels()  const { return m_in_ch; }
    int            getOutputChannels() const { return m_out_ch; }
    int            getInputRate()      const { return m_in_rate; }
    int            getOutputRate()     const { return m_out_rate; }
    AVSampleFormat getInputFormat()    const { return m_in_fmt; }
    AVSampleFormat getOutputFormat()   const { return m_out_fmt; }

    bool active = false;

private:
    bool prepare_output_frame(int samples_required);
    int  convert_internal(const uint8_t* const* in_data, int in_samples);

    SwrContext* m_swr_ctx   = nullptr;
    AVFrame*    m_out_frame = nullptr;

    AVChannelLayout m_in_layout{};
    AVChannelLayout m_out_layout{};

    int m_in_rate = 0,  m_out_rate = 0;
    int m_in_ch   = 0,  m_out_ch   = 0;
    int m_allocated_samples = 0;

    AVSampleFormat m_in_fmt  = AV_SAMPLE_FMT_NONE;
    AVSampleFormat m_out_fmt = AV_SAMPLE_FMT_NONE;
};

#endif