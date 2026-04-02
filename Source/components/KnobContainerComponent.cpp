#include "KnobContainerComponent.h"

#ifndef SOSCI
#include "../PluginProcessor.h"
#include "modulation/ModulationHelper.h"
#include "ModulationState.h"
#endif

KnobContainerComponent::~KnobContainerComponent() {
#ifndef SOSCI
    if (modBroadcaster)
        modBroadcaster->removeListener(this);
#endif
    if (boundParam)
        boundParam->removeListener(this);
}

#ifndef SOSCI

void KnobContainerComponent::wireModulation(OscirenderAudioProcessor& processor) {
    modBindings.clear();

    // Remove previous listener if re-wiring (idempotent)
    if (modBroadcaster)
        modBroadcaster->removeListener(this);

    for (auto& src : processor.getModulationSourceBindings()) {
        ModBinding b;
        b.dragPrefix = src.dragPrefix;
        b.propPrefix = src.propPrefix;
        b.onDropped = [addFn = src.addAssignment](int index, const juce::String& paramId) {
            ModAssignment assignment;
            assignment.sourceIndex = index;
            assignment.paramId = paramId;
            assignment.depth = 0.5f;
            addFn(assignment);
        };
        juce::String propPrefix = src.propPrefix;
        b.query = [getFn = src.getAssignments, valFn = src.getCurrentValue, colFn = src.getColour, propPrefix](
                      const juce::String& paramId, juce::Slider& sl, juce::NamedValueSet& props) {
            auto h = ModulationHelper::compute(getFn(), paramId, sl, valFn, colFn);
            props.set(propPrefix + "_active", h.active);
            if (h.active) {
                props.set(propPrefix + "_mod_pos", h.modulatedPos);
                props.set(propPrefix + "_colour", (juce::int64)h.colour.getARGB());
                props.set(propPrefix + "_mod_pos_norm", (double)h.modulatedNorm);
            }
        };
        modBindings.push_back(std::move(b));
    }

    modBroadcaster = &processor.modulationUpdateBroadcaster;
    modBroadcaster->addListener(this, [this]() {
        updateModulationDisplay();

        if (boundParam && ModulationState::needsHighlightRepaint(modDropHighlight, boundParam->paramID))
            repaint();
    });
}

#endif
