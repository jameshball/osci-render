#pragma once

#include "BeamFrame.h"

namespace motion::cli {
// Frames' stats as JSON, and how two sets of them differ: kept beside a
// project to find what a change to the renderer changed.
inline juce::var toVar(const BeamFrame::Stats& stats, bool withTiming) {
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("time", stats.time);
    object->setProperty("samples", stats.samples);
    object->setProperty("drawn", stats.drawn);
    object->setProperty("travelled", stats.travelled);
    object->setProperty("brightness", stats.brightness);
    object->setProperty("lit", stats.litShare);
    object->setProperty("bounds", juce::Array<juce::var> {stats.bounds.getX(), stats.bounds.getY(), stats.bounds.getRight(), stats.bounds.getBottom()});
    object->setProperty("fingerprint", stats.fingerprint);
    if (withTiming) { object->setProperty("ms", stats.milliseconds); }
    return juce::var(object.release());
}

inline BeamFrame::Stats statsFrom(const juce::var& value) {
    BeamFrame::Stats stats;
    stats.time = value["time"];
    stats.samples = value["samples"];
    stats.drawn = value["drawn"];
    stats.travelled = value["travelled"];
    stats.brightness = value["brightness"];
    stats.litShare = value["lit"];
    stats.fingerprint = value["fingerprint"].toString();
    const auto* box = value["bounds"].getArray();
    if (box != nullptr && box->size() == 4) {
        stats.bounds = juce::Rectangle<float>::leftTopRightBottom(static_cast<float>((*box)[0]), static_cast<float>((*box)[1]), static_cast<float>((*box)[2]), static_cast<float>((*box)[3]));
    }
    return stats;
}

// What changed between two stats of one frame, or nothing when its samples
// are the same.
inline juce::String changeBetween(const BeamFrame::Stats& before, const BeamFrame::Stats& after) {
    if (before.fingerprint == after.fingerprint) { return {}; }
    juce::StringArray changes;
    const auto compare = [&](const char* name, double was, double now) {
        const auto scale = std::max({std::abs(was), std::abs(now), 1.0e-9});
        if (std::abs(now - was) / scale > 1.0e-3) { changes.add(juce::String(name) + " " + juce::String(was, 4) + " -> " + juce::String(now, 4)); }
    };
    compare("samples", before.samples, after.samples);
    compare("drawn", before.drawn, after.drawn);
    compare("travelled", before.travelled, after.travelled);
    compare("brightness", before.brightness, after.brightness);
    compare("lit", before.litShare, after.litShare);
    return changes.isEmpty() ? juce::String("samples moved slightly") : changes.joinIntoString(", ");
}
}
