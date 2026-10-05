#pragma once

#include "Document.h"
#include "../import/SourceDecoding.h"
#include "PropertySchema.h"
#include "PreparedSource.h"
#include "PropertyTarget.h"
#include <JuceHeader.h>
#include <functional>
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
    PropertyMap sliders;
    // Sliders driven by modulator routes or property links.
    std::vector<std::string> driven;
    double bpm = 120;
};

inline void writeCurveKey(juce::MemoryOutputStream& key, const Curve& curve) {
    key.writeDouble(curve.base);
    for (const auto& point : curve.keyframes()) {
        for (const auto value : {point.time, point.value, point.incomingSlope, point.outgoingSlope, point.incomingInfluence, point.outgoingInfluence}) { key.writeDouble(value); }
        key.writeInt(static_cast<int>(point.interpolation));
    }
}

// The bake a Lua clip needs, or nothing when it has no slider curves: the
// script, its bake settings stretched over the clip's content, and a key over
// everything that shapes the frames, including the routes, their modulators
// (and MIDI sources) and the links that drive its sliders. Soundtrack
// loudness is not available at bake time.
inline std::optional<LuaSliderPlan> luaSliderPlan(const Asset& asset, const Clip& clip, const Composition& composition) {
    if (!asset.extension.equalsIgnoreCase(".lua") || asset.bakeKey.isEmpty()) { return std::nullopt; }
    const auto tempo = composition.tempo();
    LuaSliderPlan plan;
    for (const auto& spec : luaSliderSpecs) {
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
    const auto longest = static_cast<double>(maximumSourceFrames) / plan.settings.frameRate;
    plan.settings.duration = std::clamp(std::max(plan.settings.duration, contentEnd), 1.0 / plan.settings.frameRate, longest);
    plan.settings.duration = std::floor(plan.settings.duration * plan.settings.frameRate) / plan.settings.frameRate;
    juce::MemoryOutputStream key;
    key.writeString(asset.bakeKey);
    key.writeDouble(plan.settings.duration);
    key.writeDouble(plan.bpm);
    for (const auto& [name, curve] : plan.sliders) {
        key.writeString(juce::String(name));
        writeCurveKey(key, curve);
    }
    const auto writeClip = [&](Id id) {
        for (const auto& track : composition.tracks) {
            for (const auto& other : track.clips) {
                if (other.id != id) { continue; }
                const auto placed = other.timing(tempo);
                for (const auto value : {placed.start, placed.end(), placed.offset, placed.rate}) { key.writeDouble(value); }
                // The notes themselves, so a saved bake still matches after loading.
                if (other.midi == nullptr) { continue; }
                for (const auto& note : other.midi->notes()) {
                    for (const auto value : {note.start, note.duration}) { key.writeDouble(value); }
                    for (const auto value : {note.pitch, note.velocity, note.channel}) { key.writeInt(value); }
                }
                for (const auto& control : other.midi->controls()) {
                    key.writeDouble(control.beat);
                    for (const auto value : {control.channel, control.number, control.value}) { key.writeInt(value); }
                }
            }
        }
    };
    // Routes on (owner, property), then its link, following link sources as
    // deep as the drivers do.
    std::function<void(Id, const std::string&, const Curve*, int)> writeDrivers = [&](Id owner, const std::string& property, const Curve* curve, int depth) {
        for (const auto& route : composition.routes) {
            if (route.target != owner || route.property != property) { continue; }
            key.writeDouble(route.amount);
            key.writeInt(static_cast<int>(route.mode));
            for (const auto& modulator : composition.modulators) {
                if (modulator.id != route.modulator) { continue; }
                key.writeInt(static_cast<int>(modulator.kind));
                key.writeInt(static_cast<int>(modulator.shape.waveform));
                for (const auto value : {modulator.shape.rateHz, modulator.shape.phase, modulator.shape.beatsPerCycle}) { key.writeDouble(value); }
                key.writeBool(modulator.shape.tempoSync);
                key.writeInt64(modulator.shape.seed);
                for (const auto value : {modulator.attack, modulator.decay, modulator.sustain, modulator.release, modulator.velocity}) { key.writeDouble(value); }
                for (const auto value : {modulator.lowestPitch, modulator.highestPitch, modulator.controller, modulator.controllerChannel}) { key.writeInt(value); }
                writeClip(modulator.source);
            }
        }
        if (curve == nullptr || !curve->link.has_value() || depth > 64) { return; }
        key.writeInt64(static_cast<juce::int64>(curve->link->source));
        key.writeString(juce::String(curve->link->property));
        for (const auto value : {curve->link->scale, curve->link->offset, curve->link->delay}) { key.writeDouble(value); }
        const auto* source = findPropertyCurve(composition, curve->link->source, curve->link->property);
        if (source != nullptr) { writeCurveKey(key, *source); }
        writeClip(curve->link->source);
        writeDrivers(curve->link->source, curve->link->property, source, depth + 1);
    };
    for (const auto& [name, curve] : plan.sliders) {
        const auto routed = std::any_of(composition.routes.begin(), composition.routes.end(), [&](const auto& route) { return route.target == clip.id && route.property == name; });
        if (!routed && !curve.link.has_value()) { continue; }
        plan.driven.push_back(name);
        key.writeString(juce::String(name));
        writeDrivers(clip.id, name, &curve, 0);
    }
    if (!plan.driven.empty()) {
        // Drivers run in composition time: the clip's placement and the tempo
        // map decide where they land in the bake.
        writeClip(clip.id);
        key.writeDouble(composition.bpm);
        if (composition.tempoChanges != nullptr) {
            for (const auto& change : *composition.tempoChanges) {
                for (const auto value : {change.beat, change.bpm}) { key.writeDouble(value); }
                key.writeBool(change.ramp);
            }
        }
    }
    plan.key = juce::SHA256(key.getData(), key.getDataSize()).toHexString().toStdString();
    return plan;
}
}
