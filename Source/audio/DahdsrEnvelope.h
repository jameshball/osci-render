#pragma once

#include <JuceHeader.h>
#include <cmath>

// Centralized constants for audio/MIDI quirks.
namespace osci_audio
{
// Tiny epsilon prevents a weird mac glitch at certain exact frequencies.
// Keep this centralized so we don't cargo-cult magic numbers.
inline constexpr double kMacFrequencyEpsilonHz = 1.0e-6;

// DAHDSR time parameter bounds and step.
inline constexpr float kDahdsrTimeMinSeconds = 0.0f;
inline constexpr float kDahdsrTimeMaxSeconds = 30.0f;
inline constexpr float kDahdsrTimeStepSeconds = 0.00001f;

// Envelope time-axis zoom bounds (seconds).
inline constexpr double kEnvelopeZoomMinSeconds = 0.05;
inline constexpr double kEnvelopeZoomMaxSeconds = 30.0;

inline float evalCurve01(float curveValue, float pos)
{
    pos = juce::jlimit(0.0f, 1.0f, pos);

    if (std::abs(curveValue) <= 0.001f)
        return pos;

    const float denom = 1.0f - std::exp(curveValue);
    const float numer = 1.0f - std::exp(pos * curveValue);
    return (denom != 0.0f) ? (numer / denom) : pos;
}

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline float evalSegment(float start, float end, double elapsed, double duration, float curve)
{
    if (duration <= 0.0)
        return end;
    const float pos = (float) juce::jlimit(0.0, 1.0, elapsed / duration);
    const float shaped = evalCurve01(curve, pos);
    return lerp(start, end, shaped);
}

// Maximum absolute power value for smooth/power segments (matches Vital's kMaxPower).
inline constexpr float kMaxPower = 20.0f;

// Sinusoidal smooth transition (same as Vital's smoothTransition).
// Maps t in [0,1] through an S-curve: 0.5 * sin((t - 0.5) * PI) + 0.5
inline float smoothTransition(float t) {
    return 0.5f * std::sin((t - 0.5f) * juce::MathConstants<float>::pi) + 0.5f;
}

// Exponential power scaling (same as Vital's futils::powerScale).
// When power ~= 0, returns t unchanged (linear).
// Positive power accelerates the curve; negative power decelerates.
inline float powerScale(float t, float power) {
    constexpr float kMinPower = 0.01f;
    if (std::abs(power) < kMinPower)
        return t;
    float numerator = std::exp(power * t) - 1.0f;
    float denominator = std::exp(power) - 1.0f;
    return (denominator != 0.0f) ? (numerator / denominator) : t;
}

// Vital-style smooth+power segment evaluation.
// When smooth=true, applies sinusoidal S-curve to t before power scaling.
// The power parameter shapes the interpolation curve between start and end.
// power=0 gives linear (or smooth S-curve if smooth=true).
inline float evalSmoothPowerSegment(float start, float end, double elapsed, double duration, float power, bool smooth)
{
    if (duration <= 0.0)
        return end;
    float t = (float) juce::jlimit(0.0, 1.0, elapsed / duration);
    if (smooth)
        t = smoothTransition(t);
    t = juce::jlimit(0.0f, 1.0f, powerScale(t, power));
    return start + t * (end - start);
}
}

struct DahdsrParams
{
    double delaySeconds = 0.0;
    double attackSeconds = 0.0;
    double holdSeconds = 0.0;
    double decaySeconds = 0.0;
    double sustainLevel = 0.0; // [0..1]
    double releaseSeconds = 0.0;

    float attackCurve = 0.0f;
    float decayCurve = 0.0f;
    float releaseCurve = 0.0f;
};
// Lightweight per-voice envelope evaluator (hot path).
class DahdsrState
{
public:
    enum class Stage
    {
        Delay,
        Attack,
        Hold,
        Decay,
        Sustain,
        Release,
        Done,
    };

    void reset(const DahdsrParams& p)
    {
        params = p;
        stage = (params.delaySeconds > 0.0) ? Stage::Delay : Stage::Attack;
        stageElapsed = 0.0;
        currentValue = 0.0f;
        releaseStartValue = 0.0f;
    }

    void beginRelease()
    {
        if (stage == Stage::Release || stage == Stage::Done)
            return;
        releaseStartValue = currentValue;
        stage = Stage::Release;
        stageElapsed = 0.0;
    }

    float advance(const double dtSeconds)
    {
        const bool midiEnabled = dtSeconds > 0.0;
        if (!midiEnabled)
            return 1.0f;

        float envValue = currentValue;

        switch (stage)
        {
            case Stage::Delay:
                envValue = 0.0f;
                stageElapsed += dtSeconds;
                if (stageElapsed >= params.delaySeconds)
                {
                    stage = Stage::Attack;
                    stageElapsed = 0.0;
                }
                break;

            case Stage::Attack:
                envValue = osci_audio::evalSegment(0.0f, 1.0f, stageElapsed, params.attackSeconds, params.attackCurve);
                stageElapsed += dtSeconds;
                if (stageElapsed >= params.attackSeconds)
                {
                    stage = (params.holdSeconds > 0.0) ? Stage::Hold : Stage::Decay;
                    stageElapsed = 0.0;
                }
                break;

            case Stage::Hold:
                envValue = 1.0f;
                stageElapsed += dtSeconds;
                if (stageElapsed >= params.holdSeconds)
                {
                    stage = Stage::Decay;
                    stageElapsed = 0.0;
                }
                break;

            case Stage::Decay:
                envValue = osci_audio::evalSegment(1.0f, (float) params.sustainLevel, stageElapsed, params.decaySeconds, params.decayCurve);
                stageElapsed += dtSeconds;
                if (stageElapsed >= params.decaySeconds)
                {
                    stage = Stage::Sustain;
                    stageElapsed = 0.0;
                }
                break;

            case Stage::Sustain:
                envValue = (float) params.sustainLevel;
                break;

            case Stage::Release:
                envValue = osci_audio::evalSegment(releaseStartValue, 0.0f, stageElapsed, params.releaseSeconds, params.releaseCurve);
                stageElapsed += dtSeconds;
                if (stageElapsed >= params.releaseSeconds)
                {
                    stage = Stage::Done;
                    envValue = 0.0f;
                }
                break;

            case Stage::Done:
                envValue = 0.0f;
                break;
        }

        currentValue = envValue;
        return envValue;
    }

    Stage getStage() const { return stage; }
    double getStageElapsed() const { return stageElapsed; }

    double getUiTimeSeconds() const
    {
        const double delayEnd = params.delaySeconds;
        const double attackEnd = delayEnd + params.attackSeconds;
        const double holdEnd = attackEnd + params.holdSeconds;
        const double decayEnd = holdEnd + params.decaySeconds;

        switch (stage)
        {
            case Stage::Delay: return stageElapsed;
            case Stage::Attack: return delayEnd + stageElapsed;
            case Stage::Hold: return attackEnd + stageElapsed;
            case Stage::Decay: return holdEnd + stageElapsed;
            case Stage::Sustain: return decayEnd;
            case Stage::Release: return decayEnd + stageElapsed;
            case Stage::Done: return decayEnd + params.releaseSeconds;
        }

        return 0.0;
    }

    float getCurrentValue() const { return currentValue; }

private:
    DahdsrParams params;
    Stage stage = Stage::Delay;
    double stageElapsed = 0.0;
    float currentValue = 0.0f;
    float releaseStartValue = 0.0f;
};
