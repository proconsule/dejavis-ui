#ifndef DEJAVIS_APP_FFMPEG_LIMITER_H
#define DEJAVIS_APP_FFMPEG_LIMITER_H

#include <vector>
#include "audioeffects.h"


struct AVFilterGraph;
struct AVFilterContext;
struct AVFrame;

namespace audio_utils {

    class FFmpegLimiter : public AudioEffect
    {
    public:
        struct Config {
            int   sampleRate = 48000;
            float limit      = 0.95f;   // soglia (lineare, max 1.0)
            float attackMs   = 1.0f;    // look-ahead = latenza
            float releaseMs  = 50.0f;
            bool  autoLevel  = false;   // false = non comprimere/normalizzare
            bool  asc        = true;    // auto release su materiale denso
        };

        FFmpegLimiter() = default;
        ~FFmpegLimiter();

        FFmpegLimiter(const FFmpegLimiter&)            = delete;
        FFmpegLimiter& operator=(const FFmpegLimiter&) = delete;

        /** Inizializza con default ragionevoli a 48kHz. */
        bool Init() override;

        /** Inizializza con configurazione custom. */
        bool Init(const Config& cfg);

        /** Distrugge e ricrea il filter graph (utile per cambiare parametri). */
        /** Implementazione dell'interfaccia base */
        bool Reconfigure(const void* config) override {
            if (!config) return false;
            return Reconfigure(*static_cast<const Config*>(config));
        }

        void ProcessBlockStereoPlanar(float* left, float* right,
                                          size_t numFrames, float pre_gain = 1.0f) override;

        int  GetLatencyFrames() const override { return latencyFrames_; }


        void ProcessBlockStereo(float* inOut, size_t numFrames, float pre_gain = 1.0f) override;

        bool Reconfigure(const Config& cfg);

    private:
        bool buildGraph(const Config& cfg);
        void destroyGraph();

        bool pushFrame(const float* left, const float* right, size_t numFrames);
        void drainAvailable();

        // Ring buffer di output (float, interleaved L/R)
        void   ringPush(const float* data, size_t numFloats);
        size_t ringPop (float* dst,        size_t numFloats);

        // Garantisce che `v` abbia almeno `n` elementi (no-op se gia' sufficiente).
        static void ensureScratch(std::vector<float>& v, size_t n);

        AVFilterGraph*   graph_    = nullptr;
        AVFilterContext* srcCtx_   = nullptr;
        AVFilterContext* sinkCtx_  = nullptr;
        AVFrame*         inFrame_  = nullptr;
        AVFrame*         outFrame_ = nullptr;

        int sampleRate_    = 48000;
        int latencyFrames_ = 0;

        std::vector<float> ringBuf_;
        size_t ringSize_   = 0;
        size_t ringRead_   = 0;
        size_t ringWrite_  = 0;
        size_t ringCount_  = 0;

        std::vector<float> scratchPlanar_;
        std::vector<float> scratchDrain_;
    };

} // namespace audio_utils

#endif // DEJAVIS_APP_FFMPEG_LIMITER_H