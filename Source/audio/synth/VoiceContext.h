#pragma once

#include <JuceHeader.h>
#include "../modulation/DahdsrEnvelope.h"
#include "../modulation/EnvState.h"
#include <unordered_map>

class ShapeSound;
using VoiceEffectMap = std::unordered_map<juce::String, std::shared_ptr<osci::SimpleEffect>>;

struct VoiceParameters {
    osci::BooleanParameter* midiEnabled;
    osci::Effect* frequency;
    osci::FloatParameter* velocityTracking;
#if OSCI_PREMIUM
    osci::IntParameter* pitchBendRange;
    osci::FloatParameter* glideTime;
    osci::FloatParameter* glideSlope;
    osci::BooleanParameter* alwaysGlide;
    osci::BooleanParameter* octaveScale;
#endif
};

// Single-writer audio telemetry; consumers read these atomics without calling
// back into a voice. The product owns this storage and outlives its voices.
struct VoiceTelemetry {
    static constexpr int kMaxUiVoices = 16;
    std::atomic<double> uiVoiceEnvelopeTimeSeconds[kMaxUiVoices] {};
    std::atomic<bool> uiVoiceActive[kMaxUiVoices] {};
    std::atomic<double> uiVoiceEnvTimeSeconds[NUM_ENVELOPES][kMaxUiVoices] {};
    std::atomic<bool> uiVoiceEnvActive[NUM_ENVELOPES][kMaxUiVoices] {};
    std::atomic<float> uiVoiceEnvValue[NUM_ENVELOPES][kMaxUiVoices] {};
};

// Product-independent services for the shared voice engine. Parameters and
// returned storage must outlive voices; render callbacks must be realtime-safe.
class VoiceContext {
public:
    virtual ~VoiceContext() = default;
    virtual VoiceParameters getVoiceParameters() = 0;
    virtual VoiceTelemetry& getVoiceTelemetry() = 0;
    virtual VoiceEffectMap cloneVoiceEffectInstances() = 0;
    virtual ShapeSound* getActiveShapeSound() const = 0;
    virtual std::shared_ptr<osci::SimpleEffect> getCachedPreviewEffect() = 0;
    virtual DahdsrParams getCurrentDahdsrParams(int envelope = 0) const = 0;
    virtual double getVoiceSampleRate() = 0;
    virtual double noteToFrequency(int note, int channel) = 0;
    virtual int getNumPressedNotes() const = 0;
    virtual const osci::DawPosition& getVoiceTransport() const = 0;
    virtual const std::vector<std::shared_ptr<osci::Effect>>& getVoiceScriptParameters() const = 0;
    virtual void processVoiceEffects(juce::AudioBuffer<float>& buffer, juce::AudioBuffer<float>& envelope,
        juce::AudioBuffer<float>& frequency, juce::AudioBuffer<float>& frameSync,
        const VoiceEffectMap& effects, const std::shared_ptr<osci::SimpleEffect>& preview) = 0;
};
