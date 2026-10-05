#pragma once

#include <algorithm>
#include <cmath>

// DAHDSR segment shapes, free of JUCE so model code without it can share them.
namespace osci_audio {
inline float evalCurve01(float curveValue, float pos) {
    pos = std::clamp(pos, 0.0f, 1.0f);
    if (std::abs(curveValue) <= 0.001f) {
        return pos;
    }
    const float denom = 1.0f - std::exp(curveValue);
    const float numer = 1.0f - std::exp(pos * curveValue);
    return denom != 0.0f ? numer / denom : pos;
}

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline float evalSegment(float start, float end, double elapsed, double duration, float curve) {
    if (duration <= 0.0) {
        return end;
    }
    const auto pos = static_cast<float>(std::clamp(elapsed / duration, 0.0, 1.0));
    return lerp(start, end, evalCurve01(curve, pos));
}
}

struct DahdsrParams {
    double delaySeconds = 0.0;
    double attackSeconds = 0.0;
    double attackLevel = 1.0;
    double holdSeconds = 0.0;
    double decaySeconds = 0.0;
    double sustainLevel = 0.0; // [0..1]
    double releaseSeconds = 0.0;

    float attackCurve = 0.0f;
    float decayCurve = 0.0f;
    float releaseCurve = 0.0f;
};

enum class DahdsrStage {
    Delay, Attack, Hold, Decay, Sustain, Release, Done,
};

// Shared segment evaluation for live voices and immutable, seekable voices.
// Advancing a stage and quantizing its duration remain clock responsibilities.
inline float evaluateDahdsrStage(DahdsrStage stage, const DahdsrParams& params, double elapsed, float releaseStart = 0) {
    switch (stage) {
        case DahdsrStage::Delay: return 0;
        case DahdsrStage::Attack: return osci_audio::evalSegment(0, static_cast<float>(params.attackLevel), elapsed, params.attackSeconds, params.attackCurve);
        case DahdsrStage::Hold: return static_cast<float>(params.attackLevel);
        case DahdsrStage::Decay: return osci_audio::evalSegment(static_cast<float>(params.attackLevel), static_cast<float>(params.sustainLevel), elapsed, params.decaySeconds, params.decayCurve);
        case DahdsrStage::Sustain: return static_cast<float>(params.sustainLevel);
        case DahdsrStage::Release: return osci_audio::evalSegment(releaseStart, 0, elapsed, params.releaseSeconds, params.releaseCurve);
        case DahdsrStage::Done: return 0;
    }
    return 0;
}
