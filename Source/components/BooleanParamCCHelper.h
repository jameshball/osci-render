#pragma once

#include <JuceHeader.h>
#include "ParameterContextMenu.h"
#include <osci_render_core/effect/osci_EffectParameter.h>

// Reusable helper for adding MIDI CC support to any component wrapping a
// BooleanParameter. Embed as a member, call init() in constructor,
// wireMidiCC() to enable CC. Handles change-listener lifecycle,
// learning-state query, and right-click context menu.
struct BooleanParamCCHelper : private juce::ChangeListener {
    BooleanParamCCHelper() = default;

    ~BooleanParamCCHelper() {
        if (midiCCManager != nullptr)
            midiCCManager->removeChangeListener(this);
    }

    void init(osci::BooleanParameter* p, juce::Component* ownerComp) {
        parameter = p;
        owner = ownerComp;
        if (p != nullptr && p->midiCCManager != nullptr)
            wireMidiCC(*p->midiCCManager);
    }

    void wireMidiCC(osci::MidiCCManager& mgr) {
        ParameterContextMenu::wireMidiCCListener(midiCCManager, mgr, this);
    }

    bool isLearning() const {
        return midiCCManager != nullptr && parameter != nullptr
               && midiCCManager->isLearning(parameter);
    }

    void showContextMenu(juce::Point<int> screenPos) {
        if (midiCCManager == nullptr || parameter == nullptr) return;

        ParameterContextMenu::Context ctx;
        ctx.param = parameter;
        ctx.midiCCManager = midiCCManager;
        ctx.canResetToDefault = true;
        ctx.canSetValue = false;

        ParameterContextMenu::showAsync(ctx, screenPos, owner,
            [p = parameter]() { p->setBoolValueNotifyingHost(p->getDefaultValue() >= 0.5f); },
            nullptr, nullptr);
    }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        if (owner) owner->repaint();
    }

    osci::BooleanParameter* parameter = nullptr;
    osci::MidiCCManager* midiCCManager = nullptr;
    juce::Component* owner = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BooleanParamCCHelper)
};
