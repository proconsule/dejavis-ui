
#ifndef DEJAVIS_APP_FFMPEG_CHORUS_H
#define DEJAVIS_APP_FFMPEG_CHORUS_H

#include "audioeffects.h"
#include <vector>

struct AVFilterGraph;
struct AVFilterContext;
struct AVFrame;

namespace audio_utils {

    class FFmpegChorus : public AudioEffect {
    public:
        struct Config {
            int   sampleRate = 48000;
            float inGain     = 0.4f;
            float outGain    = 0.4f;
            float delayMs    = 55.0f; // Tipico 40-60ms
            float decay      = 0.4f;
            float speed     = 0.25f;
            float depth      = 2.0f;
        };

        FFmpegChorus() = default;
        ~FFmpegChorus();

        FFmpegChorus(const FFmpegChorus&) = delete;
        FFmpegChorus& operator=(const FFmpegChorus&) = delete;

        bool Init() override;
        bool Init(const Config& cfg);

        bool Reconfigure(const void* config) override {
            if (!config) return false;
            return Reconfigure(*static_cast<const Config*>(config));
        }
        bool Reconfigure(const Config& cfg);

        void ProcessBlockStereo(float* inOut, size_t numFrames, float pre_gain = 1.0f) override;
        void ProcessBlockStereoPlanar(float* left, float* right, size_t numFrames, float pre_gain = 1.0f) override;

        int GetLatencyFrames() const override { return 0; }

    private:
        bool buildGraph(const Config& cfg);
        void destroyGraph();
        bool pushFrame(const float* left, const float* right, size_t numFrames);
        void drainAvailable();
        void ringPush(const float* data, size_t numFloats);
        size_t ringPop(float* dst, size_t numFloats);
        static void ensureScratch(std::vector<float>& v, size_t n);

        AVFilterGraph*   graph_    = nullptr;
        AVFilterContext* srcCtx_   = nullptr;
        AVFilterContext* sinkCtx_  = nullptr;
        AVFrame*         inFrame_  = nullptr;
        AVFrame*         outFrame_  = nullptr;

        int sampleRate_ = 48000;
        std::vector<float> ringBuf_;
        size_t ringSize_ = 0, ringRead_ = 0, ringWrite_ = 0, ringCount_ = 0;
        std::vector<float> scratchPlanar_, scratchDrain_;
    };
}

#endif