#include "EffectsComponent.h"
#include "../../audio/effects/BitCrushEffect.h"
#include "../../audio/modulation/LfoState.h"
#include "../../audio/modulation/EnvState.h"
#include "../../PluginEditor.h"

bool EffectsComponent::hasAnySelectedEffects() const {
    juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
    for (const auto& eff : audioProcessor.toggleableEffects) {
#if !OSCI_PREMIUM
        if (eff->isPremiumOnly())
            continue;
#endif
        const bool isSelected = (eff->selected == nullptr) ? true : eff->selected->getBoolValue();
        if (isSelected)
            return true;
    }

    return false;
}

EffectsComponent::EffectsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editorRef)
    : audioProcessor(p), editor(editorRef), itemData(p, editorRef), listBoxModel(listBox, itemData), grid(audioProcessor, editor) {
	setText("Audio Effects");

    addAndMakeVisible(randomiseButton);

	randomiseButton.setTooltip("Randomise all effect parameter values, randomise which effects are enabled, and randomise their order.");

	randomiseButton.onClick = [this] {
		itemData.randomise();
		listBox.updateContent();
	};

	autoLinkButton.setTooltip("Auto-link LFOs to new effects based on their default animation settings.");
	autoLinkButton.setToggleState(audioProcessor.getGlobalBoolValue("autoLinkLfos", true), juce::dontSendNotification);
	autoLinkButton.onClick = [this] {
		audioProcessor.setGlobalValue("autoLinkLfos", autoLinkButton.getToggleState());
	};
	addAndMakeVisible(autoLinkButton);

    {
        juce::MessageManagerLock lock;
        audioProcessor.broadcaster.addChangeListener(this);
    }

    // Wire list model to notify when user wants to add
    itemData.onAddNewEffectRequested = [this]() {
        showingGrid = true;
        grid.setVisible(true);
        grid.refreshDisabledStates();
        listBox.setVisible(false);
        resized();
        repaint();
    };

    // Decide initial view: show grid only if there are no selected effects
    const bool anySelected = hasAnySelectedEffects();
    showingGrid = !anySelected;
    grid.onEffectSelected = [this](const juce::String& effectId) {
        std::shared_ptr<osci::Effect> chosen;
        {
            juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
            // Mark the chosen effect as selected and enabled, and move it to the end
            for (auto& eff : audioProcessor.toggleableEffects) {
                if (eff->getId() == effectId) {
                    eff->selected->setBoolValueNotifyingHost(true);
                    eff->enabled->setBoolValueNotifyingHost(true);
                    chosen = eff;
                    break;
                }
            }
            // Place chosen effect at the end of the visible (selected) list and update precedence
            if (chosen != nullptr) {
                int idx = 0;
                for (auto& e : itemData.data) {
                    if (e != chosen) e->setPrecedence(idx++);
                }
                chosen->setPrecedence(idx++);
                audioProcessor.updateEffectPrecedence();
            }
        }
        // Promote any preview LFO assignments so hover-end won't remove them
        audioProcessor.promotePreviewLfoAssignments();
        // Auto-assign LFOs if the toggle is enabled (always on in beginner mode)
        if (chosen != nullptr && (audioProcessor.isBeginnerMode() || audioProcessor.getGlobalBoolValue("autoLinkLfos", true))) {
            audioProcessor.autoAssignLfosForEffect(*chosen);
            audioProcessor.broadcaster.sendChangeMessage();
        }
        // Refresh list content to include newly selected
        itemData.resetData();
        listBox.updateContent();
        showingGrid = false;
        listBox.setVisible(true);
        grid.setVisible(false);
        resized();
        repaint();
    };
    grid.onCanceled = [this]() {
        // If canceled while default grid, just show list
        showingGrid = false;
        listBox.setVisible(true);
        grid.setVisible(false);
        resized();
        repaint();
    };

    listBox.setModel(&listBoxModel);
    addAndMakeVisible(listBox);
    // Add a small top spacer so the drop indicator can be visible above the first row
    {
        auto spacer = std::make_unique<juce::Component>();
        spacer->setSize(1, LIST_SPACER); // top padding
        listBox.setHeaderComponent(std::move(spacer));
    }
    // Wire "+ Add new effect" button below the list
    addEffectButton.onClick = [this]() {
        if (itemData.onAddNewEffectRequested) itemData.onAddNewEffectRequested();
    };
    addAndMakeVisible(addEffectButton);
    addAndMakeVisible(grid);
    // Keep disabled states in sync whenever grid is shown
    if (showingGrid) {
        grid.setVisible(true);
        grid.refreshDisabledStates();
        listBox.setVisible(false);
    } else {
        grid.setVisible(false);
        listBox.setVisible(true);
        listBox.updateContent();
    }

}

EffectsComponent::~EffectsComponent() {
    juce::MessageManagerLock lock;
    audioProcessor.broadcaster.removeChangeListener(this);
}

void EffectsComponent::resized() {
    auto area = getLocalBounds();
    auto titleBar = area.removeFromTop(30);
    titleBar.removeFromLeft(100);
    
	randomiseButton.setBounds(titleBar.removeFromLeft(20));
	titleBar.removeFromLeft(4);
	autoLinkButton.setBounds(titleBar.removeFromLeft(20));
    area = area.reduced(20);
    if (showingGrid) {
        grid.setBounds(area);
        addEffectButton.setVisible(false);
    } else {
        // Reserve space at bottom for the add button
        auto addBtnHeight = 44;
        auto listArea = area;
        auto buttonArea = listArea.removeFromBottom(addBtnHeight);
        listBox.setBounds(listArea);
        addEffectButton.setVisible(true);
        addEffectButton.setBounds(buttonArea.reduced(0, 4));
    }
}

void EffectsComponent::changeListenerCallback(juce::ChangeBroadcaster* source) {
    // Auto-show the grid when no effects are selected (e.g. fresh project load),
    // but never auto-hide it — the grid gets hidden only by explicit user action
    // (selecting an effect or pressing Cancel). This prevents hover preview
    // broadcasts from closing the grid while the user is browsing.
    if (!showingGrid && !hasAnySelectedEffects()) {
        showingGrid = true;
        grid.setVisible(true);
        listBox.setVisible(false);
    }

    // Always refresh disabled states so opened projects immediately reflect which
    // effects are already in use, and so the Cancel button visibility is correct.
    grid.refreshDisabledStates();

    // Refresh list contents to reflect newly loaded project data
    itemData.resetData();
    listBox.updateContent();

    // Ensure layout updates after visibility changes
    resized();
    repaint();
}


