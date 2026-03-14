#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include "GraphNode.h"
#include "ModAssignment.h"

// Number of global LFOs available.
namespace lfo {
    inline constexpr int NUM_LFOS = 8;
}
using lfo::NUM_LFOS;

// LfoNode is the same struct as GraphNode.
using LfoNode = GraphNode;

// Represents a single LFO waveform as a set of breakpoints over one normalized cycle.
struct LfoWaveform {
    std::vector<LfoNode> nodes;

    // Evaluate the waveform at a given phase [0, 1] -> value [0, 1].
    float evaluate(float phase) const {
        phase = phase - std::floor(phase);
        return evaluateGraphCurve(nodes, phase);
    }

    void saveToXml(juce::XmlElement* parent) const {
        for (const auto& node : nodes) {
            auto* nodeXml = parent->createNewChildElement("node");
            nodeXml->setAttribute("time", node.time);
            nodeXml->setAttribute("value", node.value);
            nodeXml->setAttribute("curve", (double)node.curve);
        }
    }

    void loadFromXml(const juce::XmlElement* parent) {
        nodes.clear();
        for (auto* nodeXml : parent->getChildWithTagNameIterator("node")) {
            LfoNode node;
            node.time = nodeXml->getDoubleAttribute("time", 0.0);
            node.value = nodeXml->getDoubleAttribute("value", 0.0);
            node.curve = (float)nodeXml->getDoubleAttribute("curve", 0.0);
            nodes.push_back(node);
        }
    }
};

// Preset LFO waveform shapes.
enum class LfoPreset {
    Sine,
    Triangle,
    Sawtooth,
    ReverseSawtooth,
    Square,
    Custom
};

inline juce::String lfoPresetToString(LfoPreset preset) {
    switch (preset) {
        case LfoPreset::Sine: return "Sine";
        case LfoPreset::Triangle: return "Triangle";
        case LfoPreset::Sawtooth: return "Sawtooth";
        case LfoPreset::ReverseSawtooth: return "Reverse Sawtooth";
        case LfoPreset::Square: return "Square";
        case LfoPreset::Custom: return "Custom";
    }
    return "Custom";
}

inline LfoPreset stringToLfoPreset(const juce::String& s) {
    if (s == "Sine") return LfoPreset::Sine;
    if (s == "Triangle") return LfoPreset::Triangle;
    if (s == "Sawtooth") return LfoPreset::Sawtooth;
    if (s == "Reverse Sawtooth") return LfoPreset::ReverseSawtooth;
    if (s == "Square") return LfoPreset::Square;
    return LfoPreset::Custom;
}

// Generate preset waveform node sets.
inline LfoWaveform createLfoPreset(LfoPreset preset) {
    LfoWaveform waveform;
    switch (preset) {
        case LfoPreset::Sine:
            // Approximate sine with 5 nodes + curve shaping.
            // curve ≈ ±1.75 closely matches sin(π/2 · t) via the exponential shaper.
            waveform.nodes = {
                { 0.0,  0.5,  0.0f },
                { 0.25, 1.0, -1.75f },
                { 0.5,  0.5,  1.75f },
                { 0.75, 0.0, -1.75f },
                { 1.0,  0.5,  1.75f },
            };
            break;
        case LfoPreset::Triangle:
            waveform.nodes = {
                { 0.0,  0.0, 0.0f },
                { 0.5,  1.0, 0.0f },
                { 1.0,  0.0, 0.0f },
            };
            break;
        case LfoPreset::Sawtooth:
            waveform.nodes = {
                { 0.0,  0.0, 0.0f },
                { 1.0,  1.0, 0.0f },
            };
            break;
        case LfoPreset::ReverseSawtooth:
            waveform.nodes = {
                { 0.0,  1.0, 0.0f },
                { 1.0,  0.0, 0.0f },
            };
            break;
        case LfoPreset::Square:
            // Square wave: high for first half, drops to low for second half
            waveform.nodes = {
                { 0.0,    1.0, 0.0f },
                { 0.499,  1.0, 0.0f },
                { 0.5,    0.0, 0.0f },
                { 0.999,  0.0, 0.0f },
                { 1.0,    1.0, 0.0f },
            };
            break;
        case LfoPreset::Custom:
            // Default to triangle
            waveform.nodes = {
                { 0.0,  0.0, 0.0f },
                { 0.5,  1.0, 0.0f },
                { 1.0,  0.0, 0.0f },
            };
            break;
    }
    return waveform;
}

// Map beginner-mode per-parameter LfoType to global LfoPreset.
// Returns {preset, negateDepth}.  negateDepth is true for ReverseSawtooth
// (modelled as a Sawtooth with negative depth in the global system, unless
// we now have a native ReverseSawtooth preset).
inline LfoPreset lfoTypeToLfoPreset(osci::LfoType type) {
    switch (type) {
        case osci::LfoType::Sine:            return LfoPreset::Sine;
        case osci::LfoType::Triangle:        return LfoPreset::Triangle;
        case osci::LfoType::Sawtooth:        return LfoPreset::Sawtooth;
        case osci::LfoType::ReverseSawtooth: return LfoPreset::ReverseSawtooth;
        case osci::LfoType::Square:          return LfoPreset::Square;
        case osci::LfoType::Seesaw:          return LfoPreset::Triangle; // closest match
        default:                             return LfoPreset::Sine;
    }
}

// LfoAssignment is now a type alias for the generic ModAssignment.
// XML serialisation uses "lfo" as the index attribute name.
using LfoAssignment = ModAssignment;

// LFO playback modes.
enum class LfoMode {
    Free,            // Always running, no note dependency (default)
    Trigger,         // Reset to phase offset on each note, cycle while note held
    Sync,            // Lock to host tempo, no reset on note
    Envelope,        // One-shot from phase offset to end
    SustainEnvelope, // Play from start to phase offset, hold until release
    LoopPoint,       // Play from start, then loop between phase offset and end
    LoopHold         // Play from start, treat phase offset as loop end point (loop 0→offset)
};

inline juce::String lfoModeToString(LfoMode mode) {
    switch (mode) {
        case LfoMode::Free:            return "Free";
        case LfoMode::Trigger:         return "Trigger";
        case LfoMode::Sync:            return "Sync";
        case LfoMode::Envelope:        return "Envelope";
        case LfoMode::SustainEnvelope: return "Sustain Envelope";
        case LfoMode::LoopPoint:       return "Loop Point";
        case LfoMode::LoopHold:        return "Loop Hold";
    }
    return "Free";
}

inline LfoMode stringToLfoMode(const juce::String& s) {
    if (s == "Free")             return LfoMode::Free;
    if (s == "Trigger")          return LfoMode::Trigger;
    if (s == "Sync")             return LfoMode::Sync;
    if (s == "Envelope")         return LfoMode::Envelope;
    if (s == "Sustain Envelope") return LfoMode::SustainEnvelope;
    if (s == "Loop Point")       return LfoMode::LoopPoint;
    if (s == "Loop Hold")        return LfoMode::LoopHold;
    return LfoMode::Free;
}

// Canonical list of all LFO modes with display names, used by UI components.
inline const std::vector<std::pair<int, juce::String>>& getAllLfoModePairs() {
    static const std::vector<std::pair<int, juce::String>> modes = {
        { static_cast<int>(LfoMode::Free),            "Free" },
        { static_cast<int>(LfoMode::Trigger),         "Trigger" },
        { static_cast<int>(LfoMode::Sync),            "Sync" },
        { static_cast<int>(LfoMode::Envelope),        "Envelope" },
        { static_cast<int>(LfoMode::SustainEnvelope), "Sustain Envelope" },
        { static_cast<int>(LfoMode::LoopPoint),       "Loop Point" },
        { static_cast<int>(LfoMode::LoopHold),        "Loop Hold" },
    };
    return modes;
}

// Audio-thread state for a single LFO.
struct LfoAudioState {
    float phase = 0.0f;     // [0, 1)
    bool finished = false;   // For one-shot modes (Envelope)
    bool noteHeld = false;   // Tracks whether a note is currently held
    bool released = false;   // Tracks whether note has been released (for SustainEnvelope)
    float holdValue = 0.0f;  // Value to hold in SustainEnvelope mode

    // Fill an output buffer for an entire block, dispatching mode once.
    // This avoids a per-sample switch in the hot path.
    void advanceBlock(float* output, int numSamples, float rateHz, float sampleRate,
                      const LfoWaveform& waveform, LfoMode mode, float phaseOffset) {
        if (sampleRate <= 0.0f) {
            std::fill(output, output + numSamples, 0.0f);
            return;
        }
        float phaseInc = rateHz / sampleRate;

        switch (mode) {
            case LfoMode::Free:
            case LfoMode::Sync:
                for (int s = 0; s < numSamples; ++s)
                    output[s] = advanceFreeOrSync(phaseInc, waveform);
                return;

            case LfoMode::Trigger:
                for (int s = 0; s < numSamples; ++s)
                    output[s] = advanceTrigger(phaseInc, waveform);
                return;

            case LfoMode::Envelope:
                for (int s = 0; s < numSamples; ++s)
                    output[s] = advanceEnvelope(phaseInc, waveform);
                return;

            case LfoMode::SustainEnvelope:
                for (int s = 0; s < numSamples; ++s)
                    output[s] = advanceSustainEnvelope(phaseInc, waveform, phaseOffset);
                return;

            case LfoMode::LoopPoint:
                for (int s = 0; s < numSamples; ++s)
                    output[s] = advanceLoopPoint(phaseInc, waveform, phaseOffset);
                return;

            case LfoMode::LoopHold:
                for (int s = 0; s < numSamples; ++s)
                    output[s] = advanceLoopHold(phaseInc, waveform, phaseOffset);
                return;
        }
    }

private:
    // Per-mode sample advance helpers — called from the tight inner loop.
    float advanceFreeOrSync(float phaseInc, const LfoWaveform& waveform) {
        phase += phaseInc;
        if (phase >= 1.0f) phase -= std::floor(phase);
        return waveform.evaluate(phase);
    }

    float advanceTrigger(float phaseInc, const LfoWaveform& waveform) {
        if (finished) return holdValue;
        phase += phaseInc;
        if (phase >= 1.0f) phase -= std::floor(phase);
        holdValue = waveform.evaluate(phase);
        return holdValue;
    }

    float advanceEnvelope(float phaseInc, const LfoWaveform& waveform) {
        if (finished) return holdValue;
        phase += phaseInc;
        if (phase >= 1.0f) {
            phase = 1.0f;
            finished = true;
            holdValue = 0.0f;
            return holdValue;
        }
        holdValue = waveform.evaluate(phase);
        return holdValue;
    }

    float advanceSustainEnvelope(float phaseInc, const LfoWaveform& waveform, float phaseOffset) {
        if (!released) {
            if (phase < phaseOffset) {
                phase += phaseInc;
                if (phase >= phaseOffset)
                    phase = phaseOffset;
                holdValue = waveform.evaluate(phase);
            }
            return holdValue;
        } else {
            if (finished) return holdValue;
            phase += phaseInc;
            if (phase >= 1.0f) {
                phase = 1.0f;
                finished = true;
                holdValue = 0.0f;
                return holdValue;
            }
            return waveform.evaluate(phase);
        }
    }

    float advanceLoopPoint(float phaseInc, const LfoWaveform& waveform, float phaseOffset) {
        if (finished) return holdValue;
        phase += phaseInc;
        if (phase >= 1.0f) {
            float loopLen = 1.0f - phaseOffset;
            if (loopLen <= 0.0f) return waveform.evaluate(phaseOffset);
            phase = phaseOffset + std::fmod(phase - phaseOffset, loopLen);
        }
        holdValue = waveform.evaluate(phase);
        return holdValue;
    }

    float advanceLoopHold(float phaseInc, const LfoWaveform& waveform, float phaseOffset) {
        if (finished) return holdValue;
        if (!released) {
            phase += phaseInc;
            if (phaseOffset <= 0.0f) { holdValue = waveform.evaluate(0.0f); return holdValue; }
            if (phase >= phaseOffset)
                phase = std::fmod(phase, phaseOffset);
            holdValue = waveform.evaluate(phase);
            return holdValue;
        } else {
            phase += phaseInc;
            if (phase >= 1.0f) {
                phase = 1.0f;
                holdValue = waveform.evaluate(1.0f);
                return holdValue;
            }
            holdValue = waveform.evaluate(phase);
            return holdValue;
        }
    }

public:

    void reset() {
        phase = 0.0f;
        finished = false;
        released = false;
        holdValue = 0.0f;
    }

    void noteOn(LfoMode mode, float phaseOffset) {
        switch (mode) {
            case LfoMode::Free:
            case LfoMode::Sync:
                break;
            case LfoMode::Trigger:
            case LfoMode::Envelope:
                resetForNoteOn(phaseOffset);
                break;
            case LfoMode::SustainEnvelope:
                resetForNoteOn(0.0f);
                break;
            case LfoMode::LoopPoint:
            case LfoMode::LoopHold:
                resetForNoteOn(0.0f);
                break;
        }
        noteHeld = true;
    }

    void noteOff(LfoMode mode) {
        noteHeld = false;
        if (mode == LfoMode::Trigger || mode == LfoMode::LoopPoint
         || mode == LfoMode::LoopHold || mode == LfoMode::SustainEnvelope)
            released = true;
    }

    // Call when all synth voices have finished (including release).
    // Freezes the LFO at zero for modes that depend on voice activity.
    void voicesFinished(LfoMode mode) {
        if (!released) return;
        if (mode == LfoMode::Trigger || mode == LfoMode::LoopPoint || mode == LfoMode::LoopHold) {
            finished = true;
            holdValue = 0.0f;
        }
    }

private:
    void resetForNoteOn(float startPhase) {
        phase = startPhase;
        finished = false;
        released = false;
        holdValue = 0.0f;
    }
};

// ============================================================================
// LFO Rate Modes — Hz, Tempo, Tempo Dotted, Tempo Triplets
// ============================================================================

enum class LfoRateMode {
    Seconds,       // Free-running Hz
    Tempo,         // Synced to BPM (straight)
    TempoDotted,   // Synced to BPM (dotted = 1.5× duration)
    TempoTriplets  // Synced to BPM (triplet = 2/3× duration)
};

inline juce::String lfoRateModeToString(LfoRateMode mode) {
    switch (mode) {
        case LfoRateMode::Seconds:       return "Seconds";
        case LfoRateMode::Tempo:         return "Tempo";
        case LfoRateMode::TempoDotted:   return "Dotted";
        case LfoRateMode::TempoTriplets: return "Triplets";
    }
    return "Seconds";
}

inline LfoRateMode stringToLfoRateMode(const juce::String& s) {
    if (s == "Seconds")  return LfoRateMode::Seconds;
    if (s == "Tempo")    return LfoRateMode::Tempo;
    if (s == "Dotted")   return LfoRateMode::TempoDotted;
    if (s == "Triplets") return LfoRateMode::TempoTriplets;
    return LfoRateMode::Seconds;
}

// Tempo division: represents a note duration as numerator/denominator (e.g. 1/4 = quarter note).
// "Freeze" is encoded as 0/1.
struct TempoDivision {
    int numerator;
    int denominator;

    juce::String toString() const {
        if (numerator == 0) return "Freeze";
        return juce::String(numerator) + "/" + juce::String(denominator);
    }

    // Duration in beats (e.g. 1/4 = 1 beat in 4/4 time, 4/1 = 16 beats)
    double durationInBeats() const {
        if (denominator == 0 || numerator == 0) return 0.0;
        return 4.0 * (double)numerator / (double)denominator;
    }

    // Convert to Hz given BPM.
    // For straight: Hz = bpm / (60 * durationInBeats)
    // For dotted:   multiply duration by 1.5
    // For triplet:  multiply duration by 2/3
    double toHz(double bpm, LfoRateMode mode) const {
        if (numerator == 0 || bpm <= 0.0) return 0.0;
        double beats = durationInBeats();
        if (mode == LfoRateMode::TempoDotted)   beats *= 1.5;
        if (mode == LfoRateMode::TempoTriplets)  beats *= 2.0 / 3.0;
        double seconds = beats * 60.0 / bpm;
        return (seconds > 0.0) ? (1.0 / seconds) : 0.0;
    }

    bool operator==(const TempoDivision& o) const { return numerator == o.numerator && denominator == o.denominator; }
};

// All available tempo divisions in order.
inline const std::vector<TempoDivision>& getTempoDivisions() {
    static const std::vector<TempoDivision> divisions = {
        { 0,  1 },   // Freeze
        { 32, 1 },
        { 16, 1 },
        { 8,  1 },
        { 4,  1 },
        { 2,  1 },
        { 1,  1 },
        { 1,  2 },
        { 1,  4 },
        { 1,  8 },
        { 1,  16 },
        { 1,  32 },
        { 1,  64 },
    };
    return divisions;
}
