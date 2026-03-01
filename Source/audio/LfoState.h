#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include "DahdsrEnvelope.h"

// Number of global LFOs available.
static constexpr int NUM_LFOS = 4;

// A single node in an LFO waveform (one cycle, domain [0,1], value [0,1]).
struct LfoNode {
    double time = 0.0;   // [0, 1]
    double value = 0.0;  // [0, 1]
    float curve = 0.0f;  // Exponential curvature for incoming segment (-50 to +50)
};

// Represents a single LFO waveform as a set of breakpoints over one normalized cycle.
struct LfoWaveform {
    std::vector<LfoNode> nodes;

    // Evaluate the waveform at a given phase [0, 1] -> value [0, 1].
    float evaluate(float phase) const {
        if (nodes.empty()) return 0.0f;
        if (nodes.size() == 1) return (float)nodes[0].value;

        // Wrap phase to [0, 1]
        phase = phase - std::floor(phase);

        // Before first node
        if (phase <= (float)nodes.front().time) return (float)nodes.front().value;
        // After last node
        if (phase >= (float)nodes.back().time) return (float)nodes.back().value;

        // Find segment
        for (size_t i = 1; i < nodes.size(); ++i) {
            if (phase <= (float)nodes[i].time) {
                float start = (float)nodes[i - 1].value;
                float end = (float)nodes[i].value;
                double elapsed = (double)phase - nodes[i - 1].time;
                double duration = nodes[i].time - nodes[i - 1].time;
                return osci_audio::evalSegment(start, end, elapsed, duration, nodes[i].curve);
            }
        }

        return (float)nodes.back().value;
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

// A single modulation assignment: LFO -> parameter.
struct LfoAssignment {
    int lfoIndex = 0;          // Which LFO (0–3)
    juce::String paramId;      // Target parameter ID
    float depth = 0.5f;        // Modulation depth [0,1] unipolar, [-1,1] bipolar
    bool bipolar = false;      // If true, LFO modulates symmetrically around current value

    void saveToXml(juce::XmlElement* parent) const {
        parent->setAttribute("lfo", lfoIndex);
        parent->setAttribute("param", paramId);
        parent->setAttribute("depth", (double)depth);
        parent->setAttribute("bipolar", bipolar);
    }

    static LfoAssignment loadFromXml(const juce::XmlElement* xml) {
        LfoAssignment a;
        a.lfoIndex = xml->getIntAttribute("lfo", 0);
        a.paramId = xml->getStringAttribute("param");
        a.depth = (float)xml->getDoubleAttribute("depth", 0.5);
        a.bipolar = xml->getBoolAttribute("bipolar", false);
        return a;
    }
};

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
