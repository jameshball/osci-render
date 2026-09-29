#pragma once

#include "Document.h"
#include "PropertySchema.h"
#include "PreparedSource.h"
#include <JuceHeader.h>
#include <map>
#include <optional>
#include <string>

namespace motion {
// One Lua clip's frames with its animated sliders baked in. Immutable; shared
// by undo snapshots. The archive is saved with the project so loading never
// executes scripts; `key` identifies the script, bake settings, slider curves
// and bake length that produced it.
struct LuaClipBake {
    std::string key;
    std::shared_ptr<const PreparedSource> source;
    juce::MemoryBlock archive;
};

struct LuaSliderPlan {
    Id clip = 0;
    std::string key;
    juce::String name, script;
    BakeSettings settings;
    std::map<std::string, Curve> sliders;
    double bpm = 120;
};

// The bake a Lua clip needs, or nothing when it has no slider curves: the
// script, its bake settings stretched over the clip's content, and a key over
// everything that shapes the frames.
inline std::optional<LuaSliderPlan> luaSliderPlan(const Asset& asset, const Clip& clip, const Tempo& tempo) {
    if (!asset.extension.equalsIgnoreCase(".lua") || asset.bakeKey.isEmpty()) { return std::nullopt; }
    LuaSliderPlan plan;
    for (const auto& spec : luaSliderSpecs()) {
        const auto found = clip.properties.find(std::string(spec.id));
        if (found != clip.properties.end()) { plan.sliders.emplace(found->first, found->second); }
    }
    if (plan.sliders.empty()) { return std::nullopt; }
    plan.clip = clip.id;
    plan.name = asset.name;
    plan.script = juce::String::fromUTF8(static_cast<const char*>(asset.data.getData()), static_cast<int>(asset.data.getSize()));
    plan.settings = asset.bakeSettings;
    plan.bpm = clip.curveBpm(tempo);
    // Cover the clip's content, within the source frame budget.
    const auto timing = clip.timing(tempo);
    const auto contentEnd = timing.localTime(timing.end());
    const auto longest = static_cast<double>(Document::maximumSourceFrames) / plan.settings.frameRate;
    plan.settings.duration = std::clamp(std::max(plan.settings.duration, contentEnd), 1.0 / plan.settings.frameRate, longest);
    plan.settings.duration = std::floor(plan.settings.duration * plan.settings.frameRate) / plan.settings.frameRate;
    juce::MemoryOutputStream key;
    key.writeString(asset.bakeKey);
    key.writeDouble(plan.settings.duration);
    key.writeDouble(plan.bpm);
    for (const auto& [name, curve] : plan.sliders) {
        key.writeString(juce::String(name));
        key.writeDouble(curve.base);
        key.writeBool(curve.modulation.enabled);
        key.writeInt(static_cast<int>(curve.modulation.waveform));
        for (const auto value : {curve.modulation.amount, curve.modulation.rateHz, curve.modulation.phase, curve.modulation.beatsPerCycle}) { key.writeDouble(value); }
        key.writeBool(curve.modulation.tempoSync);
        key.writeInt64(curve.modulation.seed);
        key.writeInt(static_cast<int>(curve.modulation.mode));
        for (const auto& point : curve.keyframes()) {
            for (const auto value : {point.time, point.value, point.incomingSlope, point.outgoingSlope, point.incomingInfluence, point.outgoingInfluence}) { key.writeDouble(value); }
            key.writeInt(static_cast<int>(point.interpolation));
        }
    }
    plan.key = juce::SHA256(key.getData(), key.getDataSize()).toHexString().toStdString();
    return plan;
}
}
