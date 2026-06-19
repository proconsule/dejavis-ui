#include "FFmpegLimiter.h"

#include "backend/src/logger.h"

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libavutil/version.h>
}

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace audio_utils {

    FFmpegLimiter::~FFmpegLimiter() {
        destroyGraph();
    }

    bool FFmpegLimiter::Init() {
        return Init(Config{});
    }

    bool FFmpegLimiter::Init(const Config& cfg) {
        destroyGraph();
        return buildGraph(cfg);
    }

    bool FFmpegLimiter::Reconfigure(const Config& cfg) {
        destroyGraph();
        return buildGraph(cfg);
    }

    void FFmpegLimiter::destroyGraph() {
        if (inFrame_)  av_frame_free(&inFrame_);
        if (outFrame_) av_frame_free(&outFrame_);
        if (graph_)    avfilter_graph_free(&graph_);
        srcCtx_ = sinkCtx_ = nullptr;
        ringRead_ = ringWrite_ = ringCount_ = 0;
    }

    void FFmpegLimiter::ensureScratch(std::vector<float>& v, size_t n) {
        if (v.size() < n) v.resize(n);
    }

    bool FFmpegLimiter::buildGraph(const Config& cfg) {
        sampleRate_    = cfg.sampleRate;
        latencyFrames_ = static_cast<int>(std::ceil(cfg.attackMs * 0.001 * cfg.sampleRate));

        graph_ = avfilter_graph_alloc();
        if (!graph_) return false;

        char srcArgs[256];
        std::snprintf(srcArgs, sizeof(srcArgs),
                "sample_rate=%d:sample_fmt=fltp:channel_layout=stereo:time_base=1/%d",
                cfg.sampleRate, cfg.sampleRate);

        if (avfilter_graph_create_filter(&srcCtx_,
                avfilter_get_by_name("abuffer"),
                "in", srcArgs, nullptr, graph_) < 0) {
            destroyGraph();
            return false;
        }

        AVFilterContext* limCtx = nullptr;
        char limArgs[256];
        std::snprintf(limArgs, sizeof(limArgs),
            "limit=%.4f:attack=%.3f:release=%.3f:asc=%d:level=%d",
            cfg.limit, cfg.attackMs, cfg.releaseMs,
            cfg.asc ? 1 : 0, cfg.autoLevel ? 1 : 0);

        if (avfilter_graph_create_filter(&limCtx,
                avfilter_get_by_name("alimiter"),
                "lim", limArgs, nullptr, graph_) < 0) {
            destroyGraph();
            return false;
        }

        AVFilterContext* fmtCtx = nullptr;
        if (avfilter_graph_create_filter(&fmtCtx,
                avfilter_get_by_name("aformat"),
                "fmt", "sample_fmts=fltp", nullptr, graph_) < 0) {
            destroyGraph();
            return false;
                }

        if (avfilter_graph_create_filter(&sinkCtx_,
                avfilter_get_by_name("abuffersink"),
                "out", nullptr, nullptr, graph_) < 0) {
            destroyGraph();
            return false;
        }

        static const enum AVSampleFormat outFmts[] = {
            AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_NONE
        };

        if (avfilter_link(srcCtx_, 0, limCtx,  0) < 0 ||
            avfilter_link(limCtx,  0, fmtCtx,  0) < 0 ||
            avfilter_link(fmtCtx,  0, sinkCtx_, 0) < 0) {
            destroyGraph();
            return false;
        }

        if (avfilter_graph_config(graph_, nullptr) < 0) {
            destroyGraph();
            return false;
        }

        inFrame_  = av_frame_alloc();
        outFrame_ = av_frame_alloc();
        if (!inFrame_ || !outFrame_) {
            destroyGraph();
            return false;
        }

        ringSize_ = (static_cast<size_t>(latencyFrames_) * 4 + 8192) * 2;
        ringBuf_.assign(ringSize_, 0.0f);
        ringRead_ = ringWrite_ = ringCount_ = 0;

        constexpr size_t kScratchInitFloats = 4096 * 2;
        scratchPlanar_.assign(kScratchInitFloats, 0.0f);
        scratchDrain_ .assign(kScratchInitFloats, 0.0f);

        std::vector<float> silence(latencyFrames_, 0.0f);
        pushFrame(silence.data(), silence.data(), latencyFrames_);
        drainAvailable();
        ringRead_ = ringWrite_ = ringCount_ = 0;

        return true;
    }

    bool FFmpegLimiter::pushFrame(const float* left, const float* right, size_t numFrames) {
        av_frame_unref(inFrame_);

        inFrame_->nb_samples  = static_cast<int>(numFrames);
        inFrame_->format      = AV_SAMPLE_FMT_FLTP;
        inFrame_->sample_rate = sampleRate_;

        av_channel_layout_default(&inFrame_->ch_layout, 2);

        if (av_frame_get_buffer(inFrame_, 0) < 0) return false;

        std::memcpy(inFrame_->data[0], left, numFrames * sizeof(float));
        std::memcpy(inFrame_->data[1], right, numFrames * sizeof(float));

        int ret = av_buffersrc_add_frame_flags(srcCtx_, inFrame_,
                                                AV_BUFFERSRC_FLAG_KEEP_REF);
        return ret >= 0;
    }

    void FFmpegLimiter::drainAvailable() {
        while (true) {
            int ret = av_buffersink_get_frame(sinkCtx_, outFrame_);
            if (ret < 0) break;  // AVERROR(EAGAIN) o EOF -> niente per ora

            size_t nf = static_cast<size_t>(outFrame_->nb_samples);

            if (outFrame_->format == AV_SAMPLE_FMT_FLT) {
                ringPush(reinterpret_cast<const float*>(outFrame_->data[0]),
                         nf * 2);
            } else if (outFrame_->format == AV_SAMPLE_FMT_FLTP) {
                ensureScratch(scratchDrain_, nf * 2);
                const float* left  = reinterpret_cast<const float*>(outFrame_->data[0]);
                const float* right = reinterpret_cast<const float*>(outFrame_->data[1]);

                for (size_t i = 0; i < nf; ++i) {
                    scratchDrain_[i * 2]     = left[i];
                    scratchDrain_[i * 2 + 1] = right[i];
                }
                ringPush(scratchDrain_.data(), nf * 2);
            } else {
                DEJAVISUI_LOG_WARN("[FFmpegLimiter] Formato output inatteso: %d",
                                   outFrame_->format);
            }

            av_frame_unref(outFrame_);
        }
    }

    void FFmpegLimiter::ringPush(const float* data, size_t numFloats) {
        if (numFloats == 0) return;

        // Protezione overrun (non dovrebbe scattare con dimensionamento sano)
        if (ringCount_ + numFloats > ringSize_) {
            size_t toDrop = (ringCount_ + numFloats) - ringSize_;
            ringRead_ = (ringRead_ + toDrop) % ringSize_;
            ringCount_ -= toDrop;
        }

        size_t first = std::min(numFloats, ringSize_ - ringWrite_);
        std::memcpy(&ringBuf_[ringWrite_], data, first * sizeof(float));
        if (first < numFloats) {
            std::memcpy(&ringBuf_[0], data + first,
                        (numFloats - first) * sizeof(float));
        }
        ringWrite_  = (ringWrite_ + numFloats) % ringSize_;
        ringCount_ += numFloats;
    }

    size_t FFmpegLimiter::ringPop(float* dst, size_t numFloats) {
        size_t avail = std::min(numFloats, ringCount_);
        if (avail == 0) return 0;

        size_t first = std::min(avail, ringSize_ - ringRead_);
        std::memcpy(dst, &ringBuf_[ringRead_], first * sizeof(float));
        if (first < avail) {
            std::memcpy(dst + first, &ringBuf_[0],
                        (avail - first) * sizeof(float));
        }
        ringRead_  = (ringRead_ + avail) % ringSize_;
        ringCount_ -= avail;
        return avail;
    }

    void FFmpegLimiter::ProcessBlockStereo(float* inOut, size_t numFrames, float pre_gain) {
        if (!srcCtx_ || !sinkCtx_ || numFrames == 0) return;

        if (pre_gain != 1.0f) {
            const size_t total = numFrames * 2;
            for (size_t i = 0; i < total; ++i) {
                inOut[i] *= pre_gain;
            }
        }

        // Interleaved to Planar conversion for pushFrame
        std::vector<float> left(numFrames), right(numFrames);
        for(size_t i=0; i<numFrames; ++i) {
            left[i] = inOut[i*2];
            right[i] = inOut[i*2+1];
        }

        pushFrame(left.data(), right.data(), numFrames);
        drainAvailable();

        const size_t needed = numFrames * 2;
        size_t got = ringPop(inOut, needed);

        if (got < needed) {
            std::memset(inOut + got, 0, (needed - got) * sizeof(float));
        }
    }

    void FFmpegLimiter::ProcessBlockStereoPlanar(float* left, float* right,
                                                     size_t numFrames, float pre_gain) {
        if (!srcCtx_ || !sinkCtx_ || numFrames == 0) return;
        if (left == nullptr || right == nullptr) return;

        // Apply gain to local copies to avoid modifying originals before pushing
        std::vector<float> lGain(numFrames), rGain(numFrames);
        for (size_t i = 0; i < numFrames; ++i) {
            lGain[i] = left[i] * pre_gain;
            rGain[i] = right[i] * pre_gain;
        }

        pushFrame(lGain.data(), rGain.data(), numFrames);
        drainAvailable();

        const size_t totalFloats = numFrames * 2;
        ensureScratch(scratchPlanar_, totalFloats);
        size_t got = ringPop(scratchPlanar_.data(), totalFloats);
        const size_t gotFrames = got / 2;

        for (size_t i = 0; i < gotFrames; ++i) {
            left[i]  = scratchPlanar_[i * 2];
            right[i] = scratchPlanar_[i * 2 + 1];
        }

        for (size_t i = gotFrames; i < numFrames; ++i) {
            left[i]  = 0.0f;
            right[i] = 0.0f;
        }
    }

} // namespace audio_utils