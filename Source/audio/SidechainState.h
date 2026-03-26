#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include <cmath>
#include "ModAssignment.h"
#include "GraphNode.h"

namespace sidechain {
    inline constexpr int NUM_SIDECHAINS = 1;
    inline constexpr float DB_FLOOR = -60.0f;    // Minimum dB value (maps to linear ~0.001)
    inline constexpr float DB_CEILING = 0.0f;     // Maximum dB value (linear 1.0)

    // Convert linear amplitude [0,1] to decibels, clamped to [DB_FLOOR, DB_CEILING]
    inline float linearToDb(float linear) {
        if (linear <= 0.0f) return DB_FLOOR;
        float db = 20.0f * std::log10(linear);
        return juce::jlimit(DB_FLOOR, DB_CEILING, db);
    }

    // Convert decibels to linear amplitude [0,1]
    inline float dbToLinear(float db) {
        if (db <= DB_FLOOR) return 0.0f;
        return std::pow(10.0f, db / 20.0f);
    }

    // Default transfer curve: linear mapping in dB space from silence to full scale.
    inline std::vector<GraphNode> defaultTransferCurve() {
        return { { (double)DB_FLOOR, 0.0, 0.0f }, { (double)DB_CEILING, 1.0, 0.0f } };
    }
}
using sidechain::NUM_SIDECHAINS;

using SidechainAssignment = ModAssignment;

// Audio-thread state for a single sidechain envelope follower.
// Takes raw peak-rectified audio (0..1) and applies attack/release smoothing,
// then maps through a transfer curve defined by breakpoint nodes.
struct SidechainAudioState {
    float smoothedLevel = 0.0f;

    // Advance the envelope follower for one block.
    // volumeIn:  per-sample peak-rectified audio [0,1] (max|L|,|R|)
    // out:       per-sample output [0,1] after attack/release + transfer curve
    void advanceBlock(float* out, const float* volumeIn, int numSamples,
                      float attackSeconds, float releaseSeconds,
                      float sampleRate,
                      const std::vector<GraphNode>& transferCurve) {
        if (numSamples <= 0) return;

        float attackCoeff = attackSeconds > 1e-6f
            ? 1.0f - std::exp(-1.0f / (attackSeconds * sampleRate))
            : 1.0f;
        float releaseCoeff = releaseSeconds > 1e-6f
            ? 1.0f - std::exp(-1.0f / (releaseSeconds * sampleRate))
            : 1.0f;

        for (int s = 0; s < numSamples; ++s) {
            float raw = volumeIn[s];

            // Apply attack/release envelope follower
            float coeff = (raw > smoothedLevel) ? attackCoeff : releaseCoeff;
            smoothedLevel += coeff * (raw - smoothedLevel);

            // Convert to dB for curve evaluation — the transfer curve operates
            // in decibel space so the response is perceptually uniform.
            float dbLevel = sidechain::linearToDb(smoothedLevel);
            out[s] = evaluateGraphCurve(transferCurve, dbLevel);
        }
    }
};
