#pragma once

#include <JuceHeader.h>
#include "VoiceContext.h"

inline VoiceEffectMap cloneVoiceEffects(const std::vector<std::shared_ptr<osci::Effect>>& sourceEffects, juce::SpinLock& effectsLock, double sampleRate) {
    // Project restore can reorder effects while a background voice is being built.
    // Snapshot the list under its lock; expensive cloning must happen outside it.
    std::vector<std::shared_ptr<osci::Effect>> globalEffects;
    {
        const juce::SpinLock::ScopedLockType lock(effectsLock);
        globalEffects = sourceEffects;
    }
    VoiceEffectMap voiceEffects;
    for (const auto& globalEffect : globalEffects) {
        auto simpleEffect = std::dynamic_pointer_cast<osci::SimpleEffect>(globalEffect);
        if (simpleEffect) {
            auto cloned = simpleEffect->cloneWithSharedParameters();
            if (sampleRate > 0) {
                cloned->prepareToPlay(sampleRate, 512);
            }
            voiceEffects[globalEffect->getId()] = cloned;
        }
    }
    return voiceEffects;
}
