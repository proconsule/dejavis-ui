#include "FFmpegAtempo.h"
#include "backend/src/logger.h"

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace audio_utils {

    FFmpegAtempo::~FFmpegAtempo() { destroyGraph(); }

    bool FFmpegAtempo::Init() { return Init(Config{}); }

    bool FFmpegAtempo::Init(const Config& cfg) {
        destroyGraph();
        return buildGraph(cfg);
    }

    bool FFmpegAtempo::Reconfigure(const Config& cfg) {
        destroyGraph();
        return buildGraph(cfg);
    }

    void FFmpegAtempo::destroyGraph() {
        if (inFrame_)  av_frame_free(&inFrame_);
        if (outFrame_) av_frame_free(&outFrame_);
        if (graph_)    avfilter_graph_free(&graph_);
        srcCtx_ = sinkCtx_ = nullptr;
        ringRead_ = ringWrite_ = ringCount_ = 0;
    }

    void FFmpegAtempo::ensureScratch(std::vector<float>& v, size_t n) {
        if (v.size() < n) v.resize(n);
    }

    bool FFmpegAtempo::buildGraph(const Config& cfg) {
        sampleRate_ = cfg.sampleRate;
        graph_ = avfilter_graph_alloc();
        if (!graph_) return false;

        float clampedTempo = std::max(0.5f, std::min(2.0f, cfg.tempo));

        char srcArgs[256];
        std::snprintf(srcArgs, sizeof(srcArgs),
            "sample_rate=%d:sample_fmt=fltp:channel_layout=stereo:time_base=1/%d",
            cfg.sampleRate, cfg.sampleRate);

        if (avfilter_graph_create_filter(&srcCtx_, avfilter_get_by_name("abuffer"), "in", srcArgs, nullptr, graph_) < 0) {
            destroyGraph(); return false;
        }

        AVFilterContext* tempoCtx = nullptr;
        char tempoArgs[64];
        std::snprintf(tempoArgs, sizeof(tempoArgs), "tempo=%.3f", clampedTempo);

        if (avfilter_graph_create_filter(&tempoCtx, avfilter_get_by_name("atempo"), "tempo", tempoArgs, nullptr, graph_) < 0) {
            destroyGraph(); return false;
        }

        AVFilterContext* fmtCtx = nullptr;
        if (avfilter_graph_create_filter(&fmtCtx, avfilter_get_by_name("aformat"), "fmt", "sample_fmts=fltp", nullptr, graph_) < 0) {
            destroyGraph(); return false;
        }

        if (avfilter_graph_create_filter(&sinkCtx_, avfilter_get_by_name("abuffersink"), "out", nullptr, nullptr, graph_) < 0) {
            destroyGraph(); return false;
        }

        if (avfilter_link(srcCtx_, 0, tempoCtx, 0) < 0 ||
            avfilter_link(tempoCtx, 0, fmtCtx, 0) < 0 ||
            avfilter_link(fmtCtx, 0, sinkCtx_, 0) < 0) {
            destroyGraph(); return false;
        }

        if (avfilter_graph_config(graph_, nullptr) < 0) {
            destroyGraph(); return false;
        }

        inFrame_ = av_frame_alloc();
        outFrame_ = av_frame_alloc();
    
        ringSize_ = 65536 * 2;
        ringBuf_.assign(ringSize_, 0.0f);
        scratchPlanar_.assign(16384, 0.0f);
        scratchDrain_.assign(16384, 0.0f);

        return true;
    }

    bool FFmpegAtempo::pushFrame(const float* left, const float* right, size_t numFrames) {
        av_frame_unref(inFrame_);
        inFrame_->nb_samples = static_cast<int>(numFrames);
        inFrame_->format = AV_SAMPLE_FMT_FLTP; // Planar
        inFrame_->sample_rate = sampleRate_;
        av_channel_layout_default(&inFrame_->ch_layout, 2);

        if (av_frame_get_buffer(inFrame_, 0) < 0) return false;

        // Copy each channel to its own plane
        std::memcpy(inFrame_->data[0], left, numFrames * sizeof(float));
        std::memcpy(inFrame_->data[1], right, numFrames * sizeof(float));

        return av_buffersrc_add_frame_flags(srcCtx_, inFrame_, AV_BUFFERSRC_FLAG_KEEP_REF) >= 0;
    }

    void FFmpegAtempo::drainAvailable() {
        while (av_buffersink_get_frame(sinkCtx_, outFrame_) >= 0) {
            size_t nf = static_cast<size_t>(outFrame_->nb_samples);
            if (outFrame_->format == AV_SAMPLE_FMT_FLTP) {
                // Copia planare -> interleaved nel ring buffer
                ensureScratch(scratchDrain_, nf * 2);
                const float *l = reinterpret_cast<const float*>(outFrame_->data[0]);
                const float *r = reinterpret_cast<const float*>(outFrame_->data[1]);
                for (size_t i = 0; i < nf; ++i) {
                    scratchDrain_[i*2] = l[i];
                    scratchDrain_[i*2+1] = r[i];
                }
                ringPush(scratchDrain_.data(), nf * 2);
            } else if (outFrame_->format == AV_SAMPLE_FMT_FLT) {
                ringPush(reinterpret_cast<const float*>(outFrame_->data[0]), nf * 2);
            }
            av_frame_unref(outFrame_);
        }
    }

    void FFmpegAtempo::ringPush(const float* data, size_t numFloats) {
        if (numFloats == 0) return;
        if (ringCount_ + numFloats > ringSize_) {
            size_t toDrop = (ringCount_ + numFloats) - ringSize_;
            ringRead_ = (ringRead_ + toDrop) % ringSize_;
            ringCount_ -= toDrop;
        }
        size_t first = std::min(numFloats, ringSize_ - ringWrite_);
        std::memcpy(&ringBuf_[ringWrite_], data, first * sizeof(float));
        if (first < numFloats) std::memcpy(&ringBuf_[0], data + first, (numFloats - first) * sizeof(float));
        ringWrite_ = (ringWrite_ + numFloats) % ringSize_;
        ringCount_ += numFloats;
    }

    size_t FFmpegAtempo::ringPop(float* dst, size_t numFloats) {
        size_t avail = std::min(numFloats, ringCount_);
        if (avail == 0) return 0;
        size_t first = std::min(avail, ringSize_ - ringRead_);
        std::memcpy(dst, &ringBuf_[ringRead_], first * sizeof(float));
        if (first < avail) std::memcpy(dst + first, &ringBuf_[0], (avail - first) * sizeof(float));
        ringRead_ = (ringRead_ + avail) % ringSize_;
        ringCount_ -= avail;
        return avail;
    }

    void FFmpegAtempo::ProcessBlockStereo(float* inOut, size_t numFrames, float pre_gain) {
        if (!srcCtx_ || !sinkCtx_) return;

        ensureScratch(scratchPlanar_, numFrames * 2);

        float* leftPtr = scratchPlanar_.data();
        float* rightPtr = scratchPlanar_.data() + numFrames;

        for (size_t i = 0; i < numFrames; ++i) {
            leftPtr[i] = inOut[i*2] * pre_gain;
            rightPtr[i] = inOut[i*2+1] * pre_gain;
        }

        pushFrame(leftPtr, rightPtr, numFrames);
        drainAvailable();

        size_t got = ringPop(inOut, numFrames * 2);
        if (got < numFrames * 2) {
            std::memset(inOut + got, 0, (numFrames * 2 - got) * sizeof(float));
        }
    }

    void FFmpegAtempo::ProcessBlockStereoPlanar(float* left, float* right, size_t numFrames, float pre_gain) {
        if (!srcCtx_ || !sinkCtx_) return;

        if (pre_gain == 1.0f) {
            pushFrame(left, right, numFrames);
        } else {
            ensureScratch(scratchPlanar_, numFrames * 2);
            float* lGain = scratchPlanar_.data();
            float* rGain = scratchPlanar_.data() + numFrames;
            for (size_t i = 0; i < numFrames; ++i) {
                lGain[i] = left[i] * pre_gain;
                rGain[i] = right[i] * pre_gain;
            }
            pushFrame(lGain, rGain, numFrames);
        }

        drainAvailable();

        ensureScratch(scratchPlanar_, numFrames * 2);
        size_t got = ringPop(scratchPlanar_.data(), numFrames * 2);
        size_t gotFrames = got / 2;

        for (size_t i = 0; i < gotFrames; ++i) {
            left[i] = scratchPlanar_[i*2];
            right[i] = scratchPlanar_[i*2+1];
        }
        for (size_t i = gotFrames; i < numFrames; ++i) {
            left[i] = 0.0f;
            right[i] = 0.0f;
        }
    }

} // namespace audio_utils