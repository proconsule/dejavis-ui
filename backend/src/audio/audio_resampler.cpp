#include "audio_resampler.h"

AudioResampler::AudioResampler() {
    m_out_frame = av_frame_alloc();
}

AudioResampler::~AudioResampler() {
    cleanup();
    av_frame_free(&m_out_frame);
}

void AudioResampler::cleanup() {
    if (m_swr_ctx)   swr_free(&m_swr_ctx);
    if (m_out_frame) av_frame_unref(m_out_frame);
    av_channel_layout_uninit(&m_in_layout);
    av_channel_layout_uninit(&m_out_layout);
    m_in_ptrs.clear();
    m_allocated_samples = 0;
    active = false;
}

bool AudioResampler::init(int in_r, int out_r, int in_ch, int out_ch,
                          AVSampleFormat in_fmt, AVSampleFormat out_fmt) {
    cleanup();

    m_in_rate  = in_r;
    m_out_rate = out_r;
    m_in_ch    = in_ch;
    m_out_ch   = out_ch;
    m_in_fmt   = in_fmt;
    m_out_fmt  = out_fmt;

    av_channel_layout_default(&m_in_layout,  in_ch);
    av_channel_layout_default(&m_out_layout, out_ch);

    m_in_ptrs.assign(in_ch > 0 ? in_ch : 1, nullptr);

    int ret = swr_alloc_set_opts2(&m_swr_ctx,
                                  &m_out_layout, out_fmt, out_r,
                                  &m_in_layout,  in_fmt,  in_r,
                                  0, nullptr);
    if (ret < 0)              return false;
    if (swr_init(m_swr_ctx) < 0) return false;

    active = true;
    return true;
}

bool AudioResampler::prepare_output_frame(int samples_required) {

    if (m_allocated_samples >= samples_required
        && m_out_frame->data[0]
        && av_frame_is_writable(m_out_frame)) {
        m_out_frame->nb_samples = samples_required;
        return true;
    }

    av_frame_unref(m_out_frame);

    m_out_frame->format      = m_out_fmt;
    m_out_frame->sample_rate = m_out_rate;
    m_out_frame->nb_samples  = samples_required;
    av_channel_layout_copy(&m_out_frame->ch_layout, &m_out_layout);

    if (av_frame_get_buffer(m_out_frame, 0) < 0) {
        m_allocated_samples = 0;
        return false;
    }
    m_allocated_samples = samples_required;
    return true;
}

int AudioResampler::convert_internal(const uint8_t** in_data, int in_samples) {
    int64_t delay = swr_get_delay(m_swr_ctx, m_in_rate);
    int expected  = (int)av_rescale_rnd(delay + in_samples, m_out_rate, m_in_rate, AV_ROUND_UP);
    if (expected <= 0) return 0;   // nothing buffered, nothing to flush

    if (!prepare_output_frame(expected)) return -1;

    int converted = swr_convert(m_swr_ctx,
                                m_out_frame->extended_data, expected,
                                in_data, in_samples);
    if (converted < 0) return converted;

    m_out_frame->nb_samples = converted;
    m_out_frame->pts        = AV_NOPTS_VALUE;   // raw paths have no timestamp
    return converted;
}

AVFrame* AudioResampler::processInterleaved(const float* input, int in_samples) {
    if (!active || !input) return nullptr;

    // Interleaved input: swr only reads in_ptr[0].
    const uint8_t* in_ptr[1] = { (const uint8_t*)input };

    int converted = convert_internal(in_ptr, in_samples);
    return (converted > 0) ? m_out_frame : nullptr;
}

AVFrame* AudioResampler::processPlanar(const float* const* input, int in_samples) {
    if (!active || !input) return nullptr;

    for (int i = 0; i < m_in_ch; i++)
        m_in_ptrs[i] = (const uint8_t*)input[i];

    int converted = convert_internal(m_in_ptrs.data(), in_samples);
    return (converted > 0) ? m_out_frame : nullptr;
}

AVFrame* AudioResampler::processFromAVFrame(AVFrame* frame) {
    if (!active || !frame) return nullptr;

    int converted = convert_internal((const uint8_t**)frame->extended_data,
                                     frame->nb_samples);
    if (converted <= 0) return nullptr;

    if (frame->pts != AV_NOPTS_VALUE)
        m_out_frame->pts = av_rescale_rnd(frame->pts, m_out_rate, m_in_rate,
                                          AV_ROUND_NEAR_INF);
    else
        m_out_frame->pts = AV_NOPTS_VALUE;

    m_out_frame->time_base = AVRational{1, m_out_rate};
    return m_out_frame;
}

AVFrame* AudioResampler::flush() {
    if (!active) return nullptr;

    int converted = convert_internal(nullptr, 0);
    return (converted > 0) ? m_out_frame : nullptr;
}

#ifdef NDI_ENABLED
AVFrame* AudioResampler::processFromNDI(NDIlib_audio_frame_v2_t* aframe) {
    if (!active || !aframe || !aframe->p_data) return nullptr;

    int channels = std::min(aframe->no_channels, m_in_ch);
    for (int i = 0; i < channels; i++)
        m_in_ptrs[i] = (const uint8_t*)aframe->p_data + (i * aframe->channel_stride_in_bytes);

    int converted = convert_internal(m_in_ptrs.data(), aframe->no_samples);
    if (converted <= 0) return nullptr;

    // NDI timecode is in 100 ns units (10 MHz). Convert to output samples.
    if (aframe->timecode != NDIlib_send_timecode_synthesize)
        m_out_frame->pts = av_rescale(aframe->timecode, m_out_rate, 10000000);
    else
        m_out_frame->pts = AV_NOPTS_VALUE;

    m_out_frame->time_base = AVRational{1, m_out_rate};
    return m_out_frame;
}
#endif