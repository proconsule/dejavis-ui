#include "EffectBank.h"

#include <algorithm>
#include <cmath>
#include <json/value.h>

#include "backend/src/logger.h"

namespace audio_utils {

    EffectBank::EffectBank(size_t maxSlots) {
        slots_.reserve(maxSlots);
        for (size_t i = 0; i < maxSlots; ++i) {
            slots_.push_back(std::make_unique<Slot>());
        }
    }

    EffectBank::~EffectBank() = default;

    bool EffectBank::validSlot(int id) const {
        return id >= 0
            && static_cast<size_t>(id) < slots_.size()
            && slots_[id] != nullptr;
    }

    void EffectBank::applyDecayConstant(Slot& s, const SlotConfig& cfg) {
        const float sr  = static_cast<float>(cfg.limiter.sampleRate);
        const float tau = std::max(cfg.meterDecaySec, 0.001f);
        s.perSampleDecay = std::exp(-1.0f / (tau * sr));
    }

    int EffectBank::AddEffectToSlot(int slotId, const SlotConfig& cfg) {
        if (slotId < 0 || static_cast<size_t>(slotId) >= slots_.size()) {
            DEJAVISUI_LOG_ERROR("INVALID SLOT");
            return -1;
        }

        Slot& s = *slots_[slotId];
        s.lastCfg = cfg;
        std::shared_ptr<AudioEffect> effect;

        if (cfg.type == EffectType::Limiter) {
            auto lim = std::make_shared<FFmpegLimiter>();
            if (!lim->Init(cfg.limiter)) {
                DEJAVISUI_LOG_ERROR("ERROR: FFmpegLimiter Init failed for slot %d", slotId);
                return -1;
            }
            effect = lim;
        } else if (cfg.type == EffectType::Echo) {
            auto echo = std::make_shared<FFmpegEcho>();
            if (!echo->Init(cfg.echo)) {
                DEJAVISUI_LOG_ERROR("ERROR: FFmpegEcho Init failed for slot %d", slotId);
                return -1;
            }
            effect = echo;
        } else if (cfg.type == EffectType::Atempo) {
            auto tempo = std::make_shared<FFmpegAtempo>();
            if (!tempo->Init(cfg.atempo)) {
                DEJAVISUI_LOG_ERROR("ERROR: FFmpegAtempo Init failed for slot %d", slotId);
                return -1;
            }
            effect = tempo;
        } else {
            return -1;
        }

        auto oldChain = s.chain.load(std::memory_order_relaxed);
        auto newChain = std::make_shared<EffectChain>();
        if (oldChain) {
            newChain->effects = oldChain->effects;
        }
        newChain->effects.push_back(effect);

        s.chain.store(newChain, std::memory_order_release);
        applyDecayConstant(s, cfg);
        s.active = true;
        return slotId;
    }

    void EffectBank::RemoveEffectFromSlot(int slotId, size_t effectIndex) {
        if (!validSlot(slotId)) return;
        Slot& s = *slots_[slotId];

        auto oldChain = s.chain.load(std::memory_order_relaxed);
        if (!oldChain || effectIndex >= oldChain->effects.size()) return;

        auto newChain = std::make_shared<EffectChain>();
        newChain->effects = oldChain->effects;
        newChain->effects.erase(newChain->effects.begin() + effectIndex);

        s.chain.store(newChain, std::memory_order_release);
        if (newChain->effects.empty()) s.active = false;
    }

    void EffectBank::updateMeters(Slot& s, size_t numFrames,
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

    int EffectBank::Alloc() {
        return Alloc(SlotConfig{});
    }

    int EffectBank::Alloc(const SlotConfig& cfg) {
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (!slots_[i]->active) {
                return AddEffectToSlot(static_cast<int>(i), cfg);
            }
        }
        return -1;
    }

    void EffectBank::FreeSlot(int slotId) {
        if (slotId < 0 || static_cast<size_t>(slotId) >= slots_.size()) return;
        Slot& s = *slots_[slotId];
        s.active = false;
        s.lastCfg = SlotConfig{};
        s.chain.store(nullptr, std::memory_order_release);
        s.preLevelL .store(0.0f, std::memory_order_relaxed);
        s.preLevelR .store(0.0f, std::memory_order_relaxed);
        s.postLevelL.store(0.0f, std::memory_order_relaxed);
        s.postLevelR.store(0.0f, std::memory_order_relaxed);
    }

    bool EffectBank::ReconfigureEffect(int slotId, size_t effectIndex, const SlotConfig& cfg) {
        if (!validSlot(slotId)) return false;
        Slot& s = *slots_[slotId];

        auto oldChain = s.chain.load(std::memory_order_acquire);
        if (!oldChain || effectIndex >= oldChain->effects.size()) return false;

        // Creiamo una nuova catena per non modificare quella in uso dal thread audio
        auto newChain = std::make_shared<EffectChain>();
        newChain->effects = oldChain->effects; // Copia i shared_ptr (non gli oggetti)

        // Creiamo il nuovo effetto riconfigurato
        std::shared_ptr<AudioEffect> newEffect;
        if (cfg.type == EffectType::Limiter) {
            auto lim = std::make_shared<FFmpegLimiter>();
            if (!lim->Init(cfg.limiter)) return false;
            newEffect = lim;
        } else if (cfg.type == EffectType::Echo) {
            auto echo = std::make_shared<FFmpegEcho>();
            if (!echo->Init(cfg.echo)) return false;
            newEffect = echo;
        } else if (cfg.type == EffectType::Atempo) {
            auto tempo = std::make_shared<FFmpegAtempo>();
            if (!tempo->Init(cfg.atempo)) return false;
            newEffect = tempo;
        } else {
            return false;
        }

        newChain->effects[effectIndex] = newEffect;
        s.chain.store(newChain, std::memory_order_release);

        s.lastCfg = cfg;
        applyDecayConstant(s, cfg);
        return true;
    }

    bool EffectBank::SetLimiterParams(int slotId, const FFmpegLimiter::Config& cfg) {
        if (!validSlot(slotId)) return false;
        Slot& s = *slots_[slotId];

        auto chain = s.chain.load(std::memory_order_acquire);
        if (!chain) return false;

        for (size_t i = 0; i < chain->effects.size(); ++i) {
            if (dynamic_cast<FFmpegLimiter*>(chain->effects[i].get())) {
                SlotConfig wrap;
                wrap.type = EffectType::Limiter;
                wrap.limiter = cfg;
                wrap.meterDecaySec = s.lastCfg.meterDecaySec;
                return ReconfigureEffect(slotId, i, wrap);
            }
        }
        return false;
    }

    bool EffectBank::SetEchoParams(int slotId, const FFmpegEcho::Config& cfg) {
        if (!validSlot(slotId)) return false;
        Slot& s = *slots_[slotId];

        auto chain = s.chain.load(std::memory_order_acquire);
        if (!chain) return false;

        for (size_t i = 0; i < chain->effects.size(); ++i) {
            if (dynamic_cast<FFmpegEcho*>(chain->effects[i].get())) {
                SlotConfig wrap;
                wrap.type = EffectType::Echo;
                wrap.echo = cfg;
                wrap.meterDecaySec = s.lastCfg.meterDecaySec;
                return ReconfigureEffect(slotId, i, wrap);
            }
        }
        return false;
    }

    bool EffectBank::SetAtempoParams(int slotId, const FFmpegAtempo::Config& cfg) {
        if (!validSlot(slotId)) return false;
        Slot& s = *slots_[slotId];

        auto chain = s.chain.load(std::memory_order_acquire);
        if (!chain) return false;

        for (size_t i = 0; i < chain->effects.size(); ++i) {
            if (dynamic_cast<FFmpegAtempo*>(chain->effects[i].get())) {
                SlotConfig wrap;
                wrap.type = EffectType::Atempo;
                wrap.atempo = cfg;
                wrap.meterDecaySec = s.lastCfg.meterDecaySec;
                return ReconfigureEffect(slotId, i, wrap);
            }
        }
        return false;
    }


    bool EffectBank::IsActive(int slotId) const {
        return validSlot(slotId);
    }

    int EffectBank::GetLatencyFrames(int slotId) const {
        if (!validSlot(slotId)) return 0;
        auto chain = slots_[slotId]->chain.load(std::memory_order_acquire);
        if (!chain) return 0;
        int totalLatency = 0;
        for (const auto& eff : chain->effects) {
            totalLatency += eff->GetLatencyFrames();
        }
        return totalLatency;
    }

    void EffectBank::Process(int slotId, float* inOut, size_t numFrames, float pre_gain) {
        if (!validSlot(slotId) || inOut == nullptr || numFrames == 0) return;
        Slot& slot = *slots_[slotId];

        auto chain = slot.chain.load(std::memory_order_acquire);
        if (!chain) return;

        if (pre_gain != 1.0f) {
            for (size_t i = 0; i < numFrames * 2; ++i) inOut[i] *= pre_gain;
        }

        float prePeakL = 0.0f, prePeakR = 0.0f;
        for (size_t i = 0; i < numFrames; ++i) {
            float aL = std::fabs(inOut[i * 2]);
            float aR = std::fabs(inOut[i * 2 + 1]);
            if (aL > prePeakL) prePeakL = aL;
            if (aR > prePeakR) prePeakR = aR;
        }

        for (auto& effect : chain->effects) {
            effect->ProcessBlockStereo(inOut, numFrames, 1.0f);
        }

        float postPeakL = 0.0f, postPeakR = 0.0f;
        for (size_t i = 0; i < numFrames; ++i) {
            float aL = std::fabs(inOut[i * 2]);
            float aR = std::fabs(inOut[i * 2 + 1]);
            if (aL > postPeakL) postPeakL = aL;
            if (aR > postPeakR) postPeakR = aR;
        }

        updateMeters(slot, numFrames, prePeakL, prePeakR, postPeakL, postPeakR);
    }

    void EffectBank::ProcessPlanar(int slotId, float* left, float* right,
                                        size_t numFrames, float pre_gain) {
        if (!validSlot(slotId) || left == nullptr || right == nullptr || numFrames == 0) return;
        Slot& slot = *slots_[slotId];

        auto chain = slot.chain.load(std::memory_order_acquire);
        if (!chain) return;

        if (pre_gain != 1.0f) {
            for (size_t i = 0; i < numFrames; ++i) {
                left[i] *= pre_gain;
                right[i] *= pre_gain;
            }
        }

        float prePeakL = 0.0f, prePeakR = 0.0f;
        for (size_t i = 0; i < numFrames; ++i) {
            float aL = std::fabs(left[i]);
            float aR = std::fabs(right[i]);
            if (aL > prePeakL) prePeakL = aL;
            if (aR > prePeakR) prePeakR = aR;
        }

        // 3. Processamento Catena
        for (auto& effect : chain->effects) {
            effect->ProcessBlockStereoPlanar(left, right, numFrames, 1.0f);
        }

        // 4. Calcolo Post-Peak
        float postPeakL = 0.0f, postPeakR = 0.0f;
        for (size_t i = 0; i < numFrames; ++i) {
            float aL = std::fabs(left[i]);
            float aR = std::fabs(right[i]);
            if (aL > postPeakL) postPeakL = aL;
            if (aR > postPeakR) postPeakR = aR;
        }

        updateMeters(slot, numFrames, prePeakL, prePeakR, postPeakL, postPeakR);
    }

    void EffectBank::ResetMeters(int slotId) {
        if (!validSlot(slotId)) return;
        Slot& s = *slots_[slotId];
        s.preLevelL .store(0.0f, std::memory_order_relaxed);
        s.preLevelR .store(0.0f, std::memory_order_relaxed);
        s.postLevelL.store(0.0f, std::memory_order_relaxed);
        s.postLevelR.store(0.0f, std::memory_order_relaxed);
    }

    float EffectBank::GetPreLevelL(int slotId) const {
        if (!validSlot(slotId)) return 0.0f;
        return slots_[slotId]->preLevelL.load(std::memory_order_relaxed);
    }

    float EffectBank::GetPreLevelR(int slotId) const {
        if (!validSlot(slotId)) return 0.0f;
        return slots_[slotId]->preLevelR.load(std::memory_order_relaxed);
    }

    float EffectBank::GetPostLevelL(int slotId) const {
        if (!validSlot(slotId)) return 0.0f;
        return slots_[slotId]->postLevelL.load(std::memory_order_relaxed);
    }

    float EffectBank::GetPostLevelR(int slotId) const {
        if (!validSlot(slotId)) return 0.0f;
        return slots_[slotId]->postLevelR.load(std::memory_order_relaxed);
    }



    Json::Value EffectBank::GetSlotConfigJson(int slotId) const {
        Json::Value root;
        if (!validSlot(slotId)) {
            root["error"] = "Invalid slot";
            return root;
        }

        const Slot& s = *slots_[slotId];
        root["active"] = s.active;
        root["meterDecaySec"] = s.lastCfg.meterDecaySec;

        auto chain = s.chain.load(std::memory_order_acquire);
        Json::Value effectsArray(Json::arrayValue);
        if (chain) {
            for (const auto& eff : chain->effects) {
                Json::Value effJson;
                if (auto* lim = dynamic_cast<FFmpegLimiter*>(eff.get())) {
                    effJson["type"] = (int)EffectType::Limiter;
                    effJson["limit"] = s.lastCfg.limiter.limit;
                    effJson["attackMs"] = s.lastCfg.limiter.attackMs;
                    effJson["releaseMs"] = s.lastCfg.limiter.releaseMs;
                    effJson["autoLevel"] = s.lastCfg.limiter.autoLevel;
                    effJson["asc"] = s.lastCfg.limiter.asc;
                } else if (auto* echo = dynamic_cast<FFmpegEcho*>(eff.get())) {
                    effJson["type"] = (int)EffectType::Echo;
                    effJson["delayMs"] = s.lastCfg.echo.delayMs;
                    effJson["decay"] = s.lastCfg.echo.decay;
                    effJson["outAmplitude"] = s.lastCfg.echo.outAmplitude;
                } else if (auto* tempo = dynamic_cast<FFmpegAtempo*>(eff.get())) {
                    effJson["type"] = (int)EffectType::Atempo;
                    effJson["tempo"] = s.lastCfg.atempo.tempo;
                }
                effectsArray.append(effJson);
            }
        }
        root["effects"] = effectsArray;
        return root;
    }

} // namespace audio_utils