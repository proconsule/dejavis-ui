
#ifndef DEJAVIS_APP_AUDIO_EFFECT_H
#define DEJAVIS_APP_AUDIO_EFFECT_H

#include <cstddef>

namespace audio_utils {

    class AudioEffect {
    public:
        virtual ~AudioEffect() = default;

        virtual bool Init() = 0;
        // Metodo per aggiornare i parametri senza distruggere l'effetto se possibile
        virtual bool Reconfigure(const void* config) = 0;

        virtual void ProcessBlockStereo(float* inOut, size_t numFrames, float pre_gain = 1.0f) = 0;
        virtual void ProcessBlockStereoPlanar(float* left, float* right, size_t numFrames, float pre_gain = 1.0f) = 0;

        virtual int GetLatencyFrames() const = 0;
    };

} // namespace audio_utils

#endif // DEJAVIS_APP_AUDIO_EFFECT_H