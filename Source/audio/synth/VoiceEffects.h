#pragma once

#include <JuceHeader.h>
#include <unordered_map>

inline std::unordered_map<juce::String, std::shared_ptr<osci::SimpleEffect>> cloneVoiceEffects(
    const std::vector<std::shared_ptr<osci::Effect>>& sourceEffects, juce::SpinLock& effectsLock, double sampleRate) {
    // Project restore can reorder effects while a background voice is being built.
    // Snapshot the list under its lock; expensive cloning must happen outside it.
    std::vector<std::shared_ptr<osci::Effect>> globalEffects;
    {
        const juce::SpinLock::ScopedLockType lock(effectsLock);
        globalEffects = sourceEffects;
    }
    std::unordered_map<juce::String, std::shared_ptr<osci::SimpleEffect>> voiceEffects;
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
