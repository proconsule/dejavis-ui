#include "LimiterBank.h"

#include <algorithm>
#include <cmath>

namespace audio_utils {

    LimiterBank::LimiterBank(size_t maxSlots) {
        slots_.reserve(maxSlots);
        for (size_t i = 0; i < maxSlots; ++i) {
            slots_.push_back(std::make_unique<Slot>());
        }
    }

    LimiterBank::~LimiterBank() = default;

    bool LimiterBank::validSlot(int id) const {
        return id >= 0
            && static_cast<size_t>(id) < slots_.size()
            && slots_[id]
            && slots_[id]->active;
    }

    void LimiterBank::applyDecayConstant(Slot& s, const SlotConfig& cfg) {
        const float sr  = static_cast<float>(cfg.limiter.sampleRate);
        const float tau = std::max(cfg.meterDecaySec, 0.001f);
        s.perSampleDecay = std::exp(-1.0f / (tau * sr));
    }

    void LimiterBank::updateMeters(Slot& s, size_t numFrames,
                                   float prePeakL, float prePeakR,
                                   float postPeakL, float postPeakR) {
        const float blockDecay = std::pow(s.perSampleDecay,
                                          static_cast<float>(numFrames));

        auto update = [blockDecay](std::atomic<float>& meter, float peak) {
            float cur = meter.load(std::memory_order_relaxed) * blockDecay;
            if (peak > cur) cur = peak;
            meter.store(cur, std::memory_order_relaxed);
        };
        update(s.preLevelL,  prePeakL);
        update(s.preLevelR,  prePeakR);
        update(s.postLevelL, postPeakL);
        update(s.postLevelR, postPeakR);
    }

    int LimiterBank::Alloc() {
        return Alloc(SlotConfig{});
    }

    int LimiterBank::Alloc(const SlotConfig& cfg) {
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (!slots_[i]->active) {
                auto lim = std::make_unique<FFmpegLimiter>();
                if (!lim->Init(cfg.limiter)) return -1;

                Slot& s   = *slots_[i];
                s.limiter = std::move(lim);
                applyDecayConstant(s, cfg);
                s.preLevelL .store(0.0f, std::memory_order_relaxed);
                s.preLevelR .store(0.0f, std::memory_order_relaxed);
                s.postLevelL.store(0.0f, std::memory_order_relaxed);
                s.postLevelR.store(0.0f, std::memory_order_relaxed);
                s.active = true;
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void LimiterBank::Free(int slotId) {
        if (slotId < 0 || static_cast<size_t>(slotId) >= slots_.size()) return;
        Slot& s = *slots_[slotId];
        s.active = false;
        s.limiter.reset();
        s.preLevelL .store(0.0f, std::memory_order_relaxed);
        s.preLevelR .store(0.0f, std::memory_order_relaxed);
        s.postLevelL.store(0.0f, std::memory_order_relaxed);
        s.postLevelR.store(0.0f, std::memory_order_relaxed);
    }

    bool LimiterBank::Reconfigure(int slotId, const SlotConfig& cfg) {
        if (!validSlot(slotId)) return false;
        Slot& s = *slots_[slotId];
        if (!s.limiter->Reconfigure(cfg.limiter)) {
            s.active = false;
            return false;
        }
        applyDecayConstant(s, cfg);
        return true;
    }

    bool LimiterBank::IsActive(int slotId) const {
        return validSlot(slotId);
    }

    int LimiterBank::GetLatencyFrames(int slotId) const {
        if (!validSlot(slotId)) return 0;
        return slots_[slotId]->limiter->GetLatencyFrames();
    }

    void LimiterBank::Process(int slotId, float* inOut, size_t numFrames, float pre_gain) {
        if (!validSlot(slotId) || inOut == nullptr || numFrames == 0) return;
        Slot& slot = *slots_[slotId];

        if (pre_gain != 1.0f) {
            const size_t total = numFrames * 2;
            for (size_t i = 0; i < total; ++i) inOut[i] *= pre_gain;
        }

        float prePeakL = 0.0f, prePeakR = 0.0f;
        for (size_t i = 0; i < numFrames; ++i) {
            const float aL = std::fabs(inOut[i * 2]);
            const float aR = std::fabs(inOut[i * 2 + 1]);
            if (aL > prePeakL) prePeakL = aL;
            if (aR > prePeakR) prePeakR = aR;
        }

        slot.limiter->ProcessBlockStereo(inOut, numFrames, 1.0f);

        float postPeakL = 0.0f, postPeakR = 0.0f;
        for (size_t i = 0; i < numFrames; ++i) {
            const float aL = std::fabs(inOut[i * 2]);
            const float aR = std::fabs(inOut[i * 2 + 1]);
            if (aL > postPeakL) postPeakL = aL;
            if (aR > postPeakR) postPeakR = aR;
        }

        updateMeters(slot, numFrames, prePeakL, prePeakR, postPeakL, postPeakR);
    }

    void LimiterBank::ProcessPlanar(int slotId, float* left, float* right,
                                    size_t numFrames, float pre_gain) {
        if (!validSlot(slotId) || left == nullptr || right == nullptr || numFrames == 0) return;
        Slot& slot = *slots_[slotId];

        if (pre_gain != 1.0f) {
            for (size_t i = 0; i < numFrames; ++i) {
                left[i]  *= pre_gain;
                right[i] *= pre_gain;
            }
        }

        float prePeakL = 0.0f, prePeakR = 0.0f;
        for (size_t i = 0; i < numFrames; ++i) {
            const float aL = std::fabs(left[i]);
            const float aR = std::fabs(right[i]);
            if (aL > prePeakL) prePeakL = aL;
            if (aR > prePeakR) prePeakR = aR;
        }

        slot.limiter->ProcessBlockStereoPlanar(left, right, numFrames, 1.0f);

        float postPeakL = 0.0f, postPeakR = 0.0f;
        for (size_t i = 0; i < numFrames; ++i) {
            const float aL = std::fabs(left[i]);
            const float aR = std::fabs(right[i]);
            if (aL > postPeakL) postPeakL = aL;
            if (aR > postPeakR) postPeakR = aR;
        }

        updateMeters(slot, numFrames, prePeakL, prePeakR, postPeakL, postPeakR);
    }

    void LimiterBank::ResetMeters(int slotId) {
        if (!validSlot(slotId)) return;
        Slot& s = *slots_[slotId];
        s.preLevelL .store(0.0f, std::memory_order_relaxed);
        s.preLevelR .store(0.0f, std::memory_order_relaxed);
        s.postLevelL.store(0.0f, std::memory_order_relaxed);
        s.postLevelR.store(0.0f, std::memory_order_relaxed);
    }

    float LimiterBank::GetPreLevelL(int slotId) const {
        if (!validSlot(slotId)) return 0.0f;
        return slots_[slotId]->preLevelL.load(std::memory_order_relaxed);
    }

    float LimiterBank::GetPreLevelR(int slotId) const {
        if (!validSlot(slotId)) return 0.0f;
        return slots_[slotId]->preLevelR.load(std::memory_order_relaxed);
    }

    float LimiterBank::GetPostLevelL(int slotId) const {
        if (!validSlot(slotId)) return 0.0f;
        return slots_[slotId]->postLevelL.load(std::memory_order_relaxed);
    }

    float LimiterBank::GetPostLevelR(int slotId) const {
        if (!validSlot(slotId)) return 0.0f;
        return slots_[slotId]->postLevelR.load(std::memory_order_relaxed);
    }

} // namespace audio_utils