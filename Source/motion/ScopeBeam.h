#pragma once

#include <JuceHeader.h>
#include <osci_gui/visualiser/osci_VisualiserParameters.h>
#include "model/Beam.h"

namespace motion {
// Where each Scope property lives among a visualiser's effects, so the
// document's beam can replace their animated values once per rendered
// block. Only the animated buffers change: parameters, their undo and the
// saved beam state are left alone.
class ScopeBeamSlots {
public:
    explicit ScopeBeamSlots(VisualiserParameters& parameters) {
        for (std::size_t property = 0; property < beamPropertyNames.size(); ++property) {
            const juce::String id(beamPropertyNames[property]);
            for (const auto* list : {&parameters.effects, &parameters.audioEffects}) {
                for (const auto& effect : *list) {
                    for (std::size_t index = 0; index < effect->parameters.size(); ++index) {
                        if (effect->parameters[index]->paramID == id) {
                            slots[property] = {effect.get(), index};
                        }
                    }
                }
            }
        }
    }
    // Fills `samples` animated values of one property; `value(sample)` gives each.
    template <typename Value>
    void write(std::size_t property, int samples, Value&& value) const {
        const auto& slot = slots[property];
        if (slot.effect == nullptr || samples <= 0) { return; }
        auto* buffer = slot.effect->getAnimatedValuesWritePointer(slot.index, static_cast<std::size_t>(samples));
        if (buffer == nullptr) { return; }
        for (int sample = 0; sample < samples; ++sample) { buffer[sample] = value(sample); }
    }

private:
    struct Slot {
        osci::Effect* effect = nullptr;
        std::size_t index = 0;
    };
    std::array<Slot, beamPropertyNames.size()> slots {};
};
}
