#ifndef DEJAVIS_APP_FFMPEG_LIMITER_H
#define DEJAVIS_APP_FFMPEG_LIMITER_H

#include <cstddef>
#include <vector>

// Forward declarations per evitare di trascinare gli header ffmpeg
// in chi include questo file.
struct AVFilterGraph;
struct AVFilterContext;
struct AVFrame;

namespace audio_utils {

    /**
     * Wrapper su ffmpeg libavfilter `alimiter` (look-ahead limiter).
     *
     * Supporta sia layout INTERLEAVED che PLANARE (stereo float):
     *   - ProcessBlockStereo(...)        -> [L0 R0 L1 R1 ...]
     *   - ProcessBlockStereoPlanar(...)  -> due buffer separati L[] e R[]
     *
     * Internamente la pipeline lavora sempre interleaved verso FFmpeg;
     * la variante planare fa interleave/deinterleave su scratch buffer
     * pre-allocati, quindi niente allocazioni nell'audio thread.
     *
     * Caratteristiche:
     *   - Vero look-ahead: cattura i transienti senza leakage.
     *   - level=false (auto-level OFF) -> preserva la dinamica originale.
     *   - Latenza fissa = attackMs (default ~1ms = 48 sample a 48kHz).
     */
    class FFmpegLimiter
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
        bool Init();

        /** Inizializza con configurazione custom. */
        bool Init(const Config& cfg);

        /**
         * Processa un blocco stereo INTERLEAVED in-place.
         *  @param inOut      buffer stereo interleaved float [L0 R0 L1 R1 ...]
         *  @param numFrames  numero di FRAME (sample stereo), NON sample totali
         *  @param pre_gain   guadagno applicato all'ingresso prima del limiter
         *
         * Restituisce sempre numFrames in uscita. Durante il warm-up iniziale
         * eventuali frame mancanti vengono riempiti con silenzio.
         */
        void ProcessBlockStereo(float* inOut, size_t numFrames, float pre_gain = 1.0f);

        /**
         * Processa un blocco stereo PLANARE in-place (due buffer separati).
         * Stessa semantica di ProcessBlockStereo ma con layout L/R separati.
         *
         *  @param left       canale sinistro, numFrames sample
         *  @param right      canale destro,   numFrames sample
         *  @param numFrames  numero di frame stereo
         *  @param pre_gain   guadagno applicato all'ingresso prima del limiter
         *
         * Nota: e' lecito alternare chiamate interleaved e planari sullo
         * stesso istanza (lo stato interno della pipeline e' indipendente
         * dal layout esterno).
         */
        void ProcessBlockStereoPlanar(float* left, float* right,
                                      size_t numFrames, float pre_gain = 1.0f);

        /** Latenza in frame introdotta dal look-ahead. */
        int  GetLatencyFrames() const { return latencyFrames_; }

        /** Distrugge e ricrea il filter graph (utile per cambiare parametri). */
        bool Reconfigure(const Config& cfg);

    private:
        bool buildGraph(const Config& cfg);
        void destroyGraph();

        bool pushFrame(const float* data, size_t numFrames);
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

        // Scratch buffer riutilizzabili per evitare allocazioni sull'audio thread:
        //  - scratchPlanar_: interleave/deinterleave in ProcessBlockStereoPlanar
        //  - scratchDrain_:  conversione FLTP -> interleaved in drainAvailable
        // Dimensionati una tantum in buildGraph; ridimensionati on-demand solo
        // se arriva un blocco piu' grande del previsto (raro, una sola volta).
        std::vector<float> scratchPlanar_;
        std::vector<float> scratchDrain_;
    };

} // namespace audio_utils

#endif // DEJAVIS_APP_FFMPEG_LIMITER_H