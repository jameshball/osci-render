#pragma once

#include "../model/BakeSettings.h"
#include <osci_scripting/osci_scripting.h>
#include <atomic>
#include <numbers>
#include <functional>

namespace motion {

// Worker-only: one fresh VM advances serially through every source sample.
// Each visual frame is one oscillator cycle. The immutable result can then be
// sought in any order without executing Lua on the audio or message thread.
class LuaBaker {
public:
    // sliders(frameSeconds, values[26]) supplies slider_a..slider_z per frame
    // (content seconds from the start of the bake); absent, sliders stay 0.
    using Sliders = std::function<void(double, double*)>;
    static PreparedPointFrames::Result bake(const juce::String& name, const juce::String& script, const BakeSettings& settings,
            const std::atomic<bool>* cancelled = nullptr, std::atomic<double>* progress = nullptr, const Sliders& sliders = {}) {
        if (progress != nullptr) { progress->store(0); }
        const auto error = settings.validate();
        if (!error.empty()) { return {nullptr, error}; }
        if (cancelled != nullptr && cancelled->load()) { return {nullptr, "Bake cancelled."}; }
        try {
            juce::String scriptError;
            LuaParser parser(name, script, [&](int, juce::String, juce::String message) {
                if (scriptError.isEmpty() && message.isNotEmpty()) { scriptError = message; }
            });
            LuaParser::OfflinePolicy policy;
            policy.randomSeed = settings.seed;
            policy.cancelled = cancelled;
            const auto configured = parser.setOfflinePolicy(policy);
            if (configured.failed()) { return {nullptr, configured.getErrorMessage().toStdString()}; }
            LuaState state;
            LuaVariables vars;
            vars.sampleRate = settings.frameRate * static_cast<double>(settings.pointsPerFrame);
            vars.frequency = settings.frameRate;
            vars.bpm = settings.bpm;
            vars.isPlaying = true;
            vars.noteOn = true;
            vars.envelopeStage = 4;
            const auto frames = settings.frameCount();
            const auto total = static_cast<std::size_t>(frames) * settings.pointsPerFrame;
            std::vector<PointSample> samples(total);
            for (std::size_t index = 0; index < total; ++index) {
                if ((index & 255) == 0) {
                    if (cancelled != nullptr && cancelled->load()) { return {nullptr, "Bake cancelled."}; }
                    if (progress != nullptr) { progress->store(static_cast<double>(index) / static_cast<double>(total)); }
                }
                vars.step = static_cast<double>(index) + 1;
                vars.phase = 2 * std::numbers::pi * static_cast<double>(index % settings.pointsPerFrame) / static_cast<double>(settings.pointsPerFrame);
                vars.cycle = static_cast<double>(index / settings.pointsPerFrame) + 1;
                if (sliders && index % settings.pointsPerFrame == 0) { sliders(static_cast<double>(index / settings.pointsPerFrame) / settings.frameRate, vars.sliders); }
                vars.playTime = static_cast<double>(index) / vars.sampleRate;
                vars.playTimeBeats = vars.playTime * settings.bpm / 60;
                const auto result = parser.run(state, vars);
                if (scriptError.isNotEmpty()) { return {nullptr, scriptError.toStdString()}; }
                if (result.count != 2 && result.count != 3 && result.count != 6) {
                    return {nullptr, "Lua must return two (XY), three (XYZ), or six (XYZRGB) numbers."};
                }
                auto& point = samples[index];
                point = {result.values[0], result.values[1], result.count >= 3 ? result.values[2] : 0,
                    result.count == 6 ? result.values[3] : -1, result.count == 6 ? result.values[4] : -1, result.count == 6 ? result.values[5] : -1};
                const auto validColour = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
                const bool inheritedColour = point.r == -1 && point.g == -1 && point.b == -1;
                if (!point.hasFinitePosition() || (result.count == 6 && !inheritedColour && !(validColour(point.r) && validColour(point.g) && validColour(point.b)))) {
                    return {nullptr, "Lua returned invalid coordinates or RGB outside [0,1] (or all -1 for inherited color) at sample " + std::to_string(index + 1) + "."};
                }
            }
            if (cancelled != nullptr && cancelled->load()) { return {nullptr, "Bake cancelled."}; }
            auto result = PreparedPointFrames::create(settings.frameRate, frames, settings.pointsPerFrame, std::move(samples));
            if (result && progress != nullptr) { progress->store(1); }
            return result;
        } catch (const std::bad_alloc&) {
            return {nullptr, "Not enough memory to bake this source."};
        } catch (const std::exception& error) {
            return {nullptr, error.what()};
        }
    }
};
}
