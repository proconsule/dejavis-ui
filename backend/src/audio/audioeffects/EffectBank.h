#ifndef DEJAVIS_APP_LIMITER_BANK_H
#define DEJAVIS_APP_LIMITER_BANK_H

#include "audioeffects.h"
#include "FFmpegLimiter.h"
#include "FFmpegEcho.h"
#include "FFmpegChorus.h"

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <vector>
#include <json/value.h>

#include "FFmpegTremolo.h"
#include "FFmpegVibrato.h"

namespace audio_utils {

    class EffectBank
    {
    public:
        enum class EffectType {
            None,
            Limiter,
            Echo,
            Chorus,
            Tremolo,
            Vibrato
        };

        struct SlotConfig {
            EffectType type = EffectType::None;
            FFmpegLimiter::Config limiter;
            FFmpegEcho::Config    echo;
            FFmpegChorus::Config  chorus;
            FFmpegTremolo::Config tremolo;
            FFmpegVibrato::Config vibrato;
            float meterDecaySec = 1.5f;
        };

        explicit EffectBank(size_t maxSlots);
        ~EffectBank();

        EffectBank(const EffectBank&)            = delete;
        EffectBank& operator=(const EffectBank&) = delete;

        int Alloc();

        int Alloc(const SlotConfig& cfg);

        int AddEffectToSlot(int slotId, const SlotConfig& cfg);

        void RemoveEffectFromSlot(int slotId, size_t effectIndex);

        void FreeSlot(int slotId);

        bool ReconfigureEffect(int slotId, size_t effectIndex, const SlotConfig& cfg);


        bool IsActive(int slotId) const;

        void Process(int slotId, float* inOut, size_t numFrames, float pre_gain = 1.0f);

        void ProcessPlanar(int slotId, float* left, float* right,
                               size_t numFrames, float pre_gain = 1.0f);

        bool SetLimiterParams(int slotId, const FFmpegLimiter::Config& cfg);
        bool SetEchoParams(int slotId, const FFmpegEcho::Config& cfg);
        bool SetChorusParams(int slotId, const FFmpegChorus::Config& cfg);
        bool SetTremoloParams(int slotId, const FFmpegTremolo::Config& cfg);
        bool SetVibratoParams(int slotId, const FFmpegVibrato::Config& cfg);


        Json::Value GetSlotConfigJson(int slotId) const;


        void ResetMeters(int slotId);

        // ---- Getter dei meter (thread-safe) ----
        float GetPreLevelL (int slotId) const;
        float GetPreLevelR (int slotId) const;
        float GetPostLevelL(int slotId) const;
        float GetPostLevelR(int slotId) const;

        int  GetLatencyFrames(int slotId) const;

        size_t Capacity() const { return slots_.size(); }

    private:
        struct EffectChain {
            std::vector<std::shared_ptr<AudioEffect>> effects;
        };
        struct Slot {
            bool                            active   = false;
            SlotConfig                      lastCfg;
            std::shared_ptr<EffectChain>    chain{nullptr};
            mutable std::shared_mutex       chainMutex;
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