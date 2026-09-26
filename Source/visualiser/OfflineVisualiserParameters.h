#pragma once

#include <osci_render_core/osci_render_core.h>
#include <osci_gui/visualiser/osci_VisualiserParameters.h>
#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>

// Message-thread capture of authored beam settings, with fresh effect DSP
// buffers and built-in LFO phases starting at zero. External processor-owned
// modulation callbacks cannot be cloned safely and are explicitly rejected.
// Never register these privately owned parameters with an AudioProcessor.
class OfflineVisualiserParameters {
private:
    // VisualiserParameters/Effect do not own their raw parameters: normally the
    // product AudioProcessor does. Destroy params/effects BEFORE this registry.
    // The fixed registry avoids allocations while adopting the fresh raw fields.
    std::array<std::unique_ptr<juce::AudioProcessorParameter>, 512> owned;
    std::size_t ownedCount = 0;

    void own(juce::AudioProcessorParameter* parameter) {
        if (parameter == nullptr) {
            return;
        }
        for (std::size_t index = 0; index < ownedCount; ++index) {
            if (owned[index].get() == parameter) {
                return;
            }
        }
        if (ownedCount == owned.size()) {
            throw std::length_error("Offline visualiser parameter ownership capacity exceeded.");
        }
        owned[ownedCount++].reset(parameter);
    }

    void ownEffects(const std::vector<std::shared_ptr<osci::Effect>>& effects) {
        for (const auto& effect : effects) {
            own(effect->enabled);
            own(effect->linked);
            own(effect->selected);
            for (auto* parameter : effect->parameters) {
                // These are exactly EffectParameter::getParameters()'s fields.
                // Their destructors do not recursively own one another.
                own(parameter);
                own(parameter->lfo);
                own(parameter->lfoRate);
                own(parameter->lfoStartPercent);
                own(parameter->lfoEndPercent);
                own(parameter->sidechain);
            }
        }
    }

    static void copyEffects(const std::vector<std::shared_ptr<osci::Effect>>& source, const std::vector<std::shared_ptr<osci::Effect>>& destination) {
        for (const auto& target : destination) {
            const auto found = std::find_if(source.begin(), source.end(), [&](const auto& effect) { return effect->getId() == target->getId(); });
            if (found == source.end()) {
                throw std::invalid_argument("Offline visualiser effect layout does not match its source.");
            }
            juce::XmlElement state("effect");
            (*found)->save(&state);
            target->load(&state);
        }
    }

    template <typename Parameter>
    static void copyParameters(const std::vector<Parameter*>& source, const std::vector<Parameter*>& destination) {
        for (auto* target : destination) {
            const auto found = std::find_if(source.begin(), source.end(), [&](auto* parameter) { return parameter->paramID == target->paramID; });
            if (found == source.end()) {
                throw std::invalid_argument("Offline visualiser parameter layout does not match its source.");
            }
            target->setValue((*found)->getValue());
        }
    }

public:
    VisualiserParameters params;

    OfflineVisualiserParameters() {
        for (auto* parameter : params.booleans) {
            own(parameter);
        }
        for (auto* parameter : params.integers) {
            own(parameter);
        }
        ownEffects(params.effects);
        ownEffects(params.audioEffects);
    }

    explicit OfflineVisualiserParameters(VisualiserParameters& source) : OfflineVisualiserParameters() {
        if (source.applyExternalModulation) {
            throw std::invalid_argument("External processor modulation cannot be captured as an independent offline beam snapshot.");
        }
        copyEffects(source.effects, params.effects);
        copyEffects(source.audioEffects, params.audioEffects);
        copyParameters(source.booleans, params.booleans);
        copyParameters(source.integers, params.integers);
        // No ValueTree, MIDI manager, listeners, animated-value source pointers,
        // external inputs or processor callbacks are shared with the source.
    }

    OfflineVisualiserParameters(const OfflineVisualiserParameters&) = delete;
    OfflineVisualiserParameters& operator=(const OfflineVisualiserParameters&) = delete;
};
