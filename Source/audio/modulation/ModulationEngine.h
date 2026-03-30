#pragma once

#include <JuceHeader.h>
#include <vector>
#include <unordered_map>
#include "ModulationSource.h"
#include "ModulationTypes.h"

// Maps a parameter ID to its owning effect and position.
// Used by both ModulationEngine and the processor to resolve modulation targets.
struct ParamLocation {
    osci::Effect* effect;
    int paramIndex;
};

// Coordinator that operates on all registered ModulationSource instances
// through their common interface.  Owns the generic "apply buffers to parameters"
// step and cross-source utilities (remove-all, get-bindings).
//
// Type-specific block-buffer generation (LFO waveform evaluation, envelope
// aggregation, etc.) remains in the processor; the engine only handles the
// parts that are identical for every source type.
class ModulationEngine {
public:
    explicit ModulationEngine(std::unordered_map<juce::String, ParamLocation>& paramMap)
        : paramLocationMap(paramMap) {}

    // Register a source.  Order determines the stacking order of modulation
    // (first registered is applied first).
    void addSource(ModulationSource* source) { sources.push_back(source); }

    // Pre-allocate block buffers in each source for the given block size.
    void prepareToPlay(double sampleRate, int samplesPerBlock) {
        for (auto* source : sources)
            source->prepareToPlay(sampleRate, samplesPerBlock);
    }

    // Apply all sources' block buffers to animated values.
    // Call once per processBlock, AFTER all type-specific buffer-fill methods.
    void applyAllModulation(int numSamples) {
        for (auto* source : sources) {
            const auto& assnCopy = source->getAssignmentsAudioThread();
            if (assnCopy.empty()) continue;

            auto* buffers = source->getBlockBuffers();
            int srcCount = source->getSourceCount();
            if (buffers == nullptr || srcCount <= 0) continue;

            applyModulationBuffers(numSamples, assnCopy, buffers, srcCount);
        }
    }

    // Remove every assignment (across all sources) that targets a parameter of the given effect.
    void removeAllAssignmentsForEffect(const osci::Effect& effect) {
        auto belongsToEffect = [&](const juce::String& paramId) {
            for (auto* p : effect.parameters)
                if (p->paramID == paramId) return true;
            return false;
        };
        for (auto* source : sources)
            source->removeAssignmentsIf([&](const ModAssignment& a) { return belongsToEffect(a.paramId); });
    }

    // Build the generic binding list consumed by EffectComponent for drag-and-drop.
    std::vector<ModulationSourceBinding> getModulationSourceBindings() {
        std::vector<ModulationSourceBinding> result;
        for (auto* source : sources) {
            auto colourFn = source->getColourFunction();
            result.push_back({
                source->getTypeLabel(),
                source->getTypeId(),
                [source](const ModAssignment& a) { source->addAssignment(a); },
                [source]() { return source->getAssignments(); },
                [source](int i) { return source->getCurrentValue(i); },
                colourFn ? colourFn : [](int) { return juce::Colours::grey; }
            });
        }
        return result;
    }

    // Iterate registered sources
    const std::vector<ModulationSource*>& getSources() const { return sources; }

    // Serialization helpers — delegates to each source's virtual methods
    void saveToXml(juce::XmlElement* root) const {
        for (auto* source : sources)
            source->saveToXml(root);
    }

    void loadFromXml(const juce::XmlElement* root) {
        for (auto* source : sources)
            source->loadFromXml(root);
    }

private:
    std::vector<ModulationSource*> sources;
    std::unordered_map<juce::String, ParamLocation>& paramLocationMap;

    void applyModulationBuffers(
        int numSamples,
        const std::vector<ModAssignment>& assignments,
        const std::vector<float>* sourceBuffers,
        int maxSourceIndex)
    {
        for (const auto& assignment : assignments) {
            if (assignment.sourceIndex < 0 || assignment.sourceIndex >= maxSourceIndex) continue;

            auto it = paramLocationMap.find(assignment.paramId);
            if (it == paramLocationMap.end()) continue;

            auto& loc = it->second;
            float* buf = loc.effect->getAnimatedValuesWritePointer(loc.paramIndex, numSamples);
            if (buf == nullptr) continue;

            const float* modData = sourceBuffers[assignment.sourceIndex].data();
            float paramMin = loc.effect->parameters[loc.paramIndex]->min;
            float paramMax = loc.effect->parameters[loc.paramIndex]->max;
            float range = paramMax - paramMin;
            float depth = assignment.depth;

            if (assignment.bipolar) {
                for (int s = 0; s < numSamples; ++s)
                    buf[s] = juce::jlimit(paramMin, paramMax,
                        buf[s] + (modData[s] * 2.0f - 1.0f) * depth * range * 0.5f);
            } else {
                for (int s = 0; s < numSamples; ++s)
                    buf[s] = juce::jlimit(paramMin, paramMax,
                        buf[s] + modData[s] * depth * range);
            }
        }
    }
};
