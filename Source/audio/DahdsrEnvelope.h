#pragma once

#include <JuceHeader.h>

// Centralized constants for audio/MIDI quirks.
namespace osci_audio
{
// Tiny epsilon prevents a weird mac glitch at certain exact frequencies.
// Keep this centralized so we don't cargo-cult magic numbers.
inline constexpr double kMacFrequencyEpsilonHz = 1.0e-6;
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
                envValue = evalSegment(0.0f, 1.0f, stageElapsed, params.attackSeconds, params.attackCurve);
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
                envValue = evalSegment(1.0f, (float) params.sustainLevel, stageElapsed, params.decaySeconds, params.decayCurve);
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
                envValue = evalSegment(releaseStartValue, 0.0f, stageElapsed, params.releaseSeconds, params.releaseCurve);
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
    static inline float evalCurve01(float curveValue, float pos)
    {
        pos = juce::jlimit(0.0f, 1.0f, pos);

        if (std::abs(curveValue) <= 0.001f)
            return pos;

        const float denom = 1.0f - std::exp(curveValue);
        const float numer = 1.0f - std::exp(pos * curveValue);
        return (denom != 0.0f) ? (numer / denom) : pos;
    }

    static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

    static inline float evalSegment(float start, float end, double elapsed, double duration, float curve)
    {
        if (duration <= 0.0)
            return end;
        const float pos = (float) juce::jlimit(0.0, 1.0, elapsed / duration);
        const float shaped = evalCurve01(curve, pos);
        return lerp(start, end, shaped);
    }

    DahdsrParams params;
    Stage stage = Stage::Delay;
    double stageElapsed = 0.0;
    float currentValue = 0.0f;
    float releaseStartValue = 0.0f;
};
