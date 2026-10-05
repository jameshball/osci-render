#pragma once

#include "../visualiser/RecordingParameters.h"
#include <osci_gui/visualiser/osci_VisualiserParameters.h>

// The Scope's authored output state, saved with a project. Deliberately
// excludes synth, transport, device routing and processor-owned external
// modulation state.
namespace VisualiserState {
inline void save(juce::XmlElement& project, VisualiserParameters& parameters, RecordingParameters& recording) {
    auto* beam = project.createNewChildElement("beam");
    const auto saveGroup = [beam](const char* name, const auto& values) {
        auto* group = beam->createNewChildElement(name);
        for (const auto& value : values) { value->save(group->createNewChildElement("parameter")); }
    };
    saveGroup("effects", parameters.effects);
    saveGroup("audioEffects", parameters.audioEffects);
    saveGroup("booleans", parameters.booleans);
    saveGroup("integers", parameters.integers);
    recording.save(project.createNewChildElement("recording"));
}

inline void load(juce::XmlElement& project, VisualiserParameters& parameters, RecordingParameters& recording) {
    auto* beam = project.getChildByName("beam");
    if (beam != nullptr) {
        const auto loadGroup = [beam](const char* name, const auto& values, const auto& identity) {
            auto* group = beam->getChildByName(name);
            if (group == nullptr) { return; }
            for (auto* saved : group->getChildWithTagNameIterator("parameter")) {
                const auto id = saved->getStringAttribute("id");
                for (const auto& value : values) {
                    if (identity(value) == id) { value->load(saved); break; }
                }
            }
        };
        const auto effectId = [](const auto& effect) { return effect->getId(); };
        const auto parameterId = [](const auto& parameter) { return parameter->paramID; };
        loadGroup("effects", parameters.effects, effectId);
        loadGroup("audioEffects", parameters.audioEffects, effectId);
        loadGroup("booleans", parameters.booleans, parameterId);
        loadGroup("integers", parameters.integers, parameterId);
    }
    auto* savedRecording = project.getChildByName("recording");
    if (savedRecording != nullptr) { recording.load(savedRecording); }
}
}
