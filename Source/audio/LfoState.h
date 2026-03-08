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
    Square,
    Custom
};

inline juce::String lfoPresetToString(LfoPreset preset) {
    switch (preset) {
        case LfoPreset::Sine: return "Sine";
        case LfoPreset::Triangle: return "Triangle";
        case LfoPreset::Sawtooth: return "Sawtooth";
        case LfoPreset::Square: return "Square";
        case LfoPreset::Custom: return "Custom";
    }
    return "Custom";
}

inline LfoPreset stringToLfoPreset(const juce::String& s) {
    if (s == "Sine") return LfoPreset::Sine;
    if (s == "Triangle") return LfoPreset::Triangle;
    if (s == "Sawtooth") return LfoPreset::Sawtooth;
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

// LfoAssignment is now a type alias for the generic ModAssignment.
// XML serialisation uses "lfo" as the index attribute name.
using LfoAssignment = ModAssignment;

// Audio-thread state for a single LFO.
struct LfoAudioState {
    float phase = 0.0f; // [0, 1)

    // Advance phase by one sample and return the waveform value.
    float advance(float rateHz, float sampleRate, const LfoWaveform& waveform) {
        if (sampleRate <= 0.0f) return 0.0f;
        phase += rateHz / sampleRate;
        if (phase >= 1.0f) phase -= std::floor(phase);
        return waveform.evaluate(phase);
    }

    void reset() {
        phase = 0.0f;
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
