#ifndef DEJAVIS_APP_LIMITER_BANK_H
#define DEJAVIS_APP_LIMITER_BANK_H

#include "FFmpegLimiter.h"

#include <atomic>
#include <memory>
#include <vector>

namespace audio_utils {

    class LimiterBank
    {
    public:
        struct SlotConfig {
            FFmpegLimiter::Config limiter;
            float                 meterDecaySec = 1.5f;
        };

        explicit LimiterBank(size_t maxSlots);
        ~LimiterBank();

        LimiterBank(const LimiterBank&)            = delete;
        LimiterBank& operator=(const LimiterBank&) = delete;

        int  Alloc();

        int  Alloc(const SlotConfig& cfg);

        void Free(int slotId);

        bool Reconfigure(int slotId, const SlotConfig& cfg);

        bool IsActive(int slotId) const;

        void Process(int slotId, float* inOut, size_t numFrames, float pre_gain = 1.0f);

        void ProcessPlanar(int slotId, float* left, float* right,
                           size_t numFrames, float pre_gain = 1.0f);

        void ResetMeters(int slotId);

        // ---- Getter dei meter (thread-safe) ----
        float GetPreLevelL (int slotId) const;
        float GetPreLevelR (int slotId) const;
        float GetPostLevelL(int slotId) const;
        float GetPostLevelR(int slotId) const;

        int  GetLatencyFrames(int slotId) const;

        size_t Capacity() const { return slots_.size(); }

    private:
        struct Slot {
            bool                            active   = false;
            std::unique_ptr<FFmpegLimiter>  limiter;
            std::atomic<float>              preLevelL{0.0f};
            std::atomic<float>              preLevelR{0.0f};
            std::atomic<float>              postLevelL{0.0f};
            std::atomic<float>              postLevelR{0.0f};
            float                           perSampleDecay = 1.0f;
        };

        bool validSlot(int id) const;
        void applyDecayConstant(Slot& s, const SlotConfig& cfg);

        void updateMeters(Slot& s, size_t numFrames,
                          float prePeakL, float prePeakR,
                          float postPeakL, float postPeakR);

        std::vector<std::unique_ptr<Slot>> slots_;
    };

} // namespace audio_utils

#endif // DEJAVIS_APP_LIMITER_BANK_H