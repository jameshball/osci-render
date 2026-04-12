#include "KnobContainerComponent.h"
#include "ParameterContextMenu.h"
#include "ParameterSettingsComponent.h"

#ifndef SOSCI
#include "../PluginProcessor.h"
#include "modulation/ModulationHelper.h"
#include "ModulationState.h"
#endif

KnobContainerComponent::~KnobContainerComponent() {
    if (midiCCManager)
        midiCCManager->removeChangeListener(this);
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

void KnobContainerComponent::wireMidiCC(osci::MidiCCManager& manager) {
    ParameterContextMenu::wireMidiCCListener(midiCCManager, manager, this);
    setupMidiCCContextMenu();
}

void KnobContainerComponent::setupMidiCCContextMenu() {
    knob.onShowContextMenu = [this](juce::Point<int> screenPos) {
        ParameterContextMenu::Context ctx;
        ctx.param = boundParam;
        ctx.effectParam = effectParam;
        ctx.midiCCManager = midiCCManager;
        ctx.canResetToDefault = knob.isDoubleClickReturnEnabled();
        ctx.ccEffectParam = effectParam;

        auto safeThis = juce::Component::SafePointer<KnobContainerComponent>(this);
        ParameterContextMenu::showAsync(ctx, screenPos, this,
            [safeThis]() {
                if (safeThis) safeThis->knob.setValue(safeThis->knob.getDoubleClickReturnValue(), juce::sendNotificationSync);
            },
            [safeThis]() { if (safeThis) safeThis->knob.showValueEditor(); },
            [safeThis]() { if (safeThis) safeThis->showSettingsPopup(); });
    };
}

void KnobContainerComponent::showSettingsPopup() {
    if (!effectParam) return;

    auto settings = std::make_unique<ParameterSettingsComponent>(effectParam, [this]() {
        knob.setRange(effectParam->min, effectParam->max, effectParam->step);
    });
    settings->setLookAndFeel(&getLookAndFeel());
    settings->setSize(settings->getDesiredWidth(), settings->getDesiredHeight());
    auto& myBox = juce::CallOutBox::launchAsynchronously(
        std::move(settings), getScreenBounds(), nullptr);
    juce::ignoreUnused(myBox);
}
