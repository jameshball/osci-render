#include "EffectsComponent.h"
#include "audio/BitCrushEffect.h"
#include "PluginEditor.h"

EffectsComponent::EffectsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor)
    : audioProcessor(p), itemData(p, editor), listBoxModel(listBox, itemData), grid(p) {
	setText("Audio Effects");

    addAndMakeVisible(frequency);

    frequency.slider.setSkewFactorFromMidPoint(500.0);
    frequency.slider.setTextValueSuffix("Hz");
    frequency.slider.setValue(audioProcessor.frequencyEffect->getValue(), juce::dontSendNotification);

    addAndMakeVisible(randomiseButton);

	randomiseButton.setTooltip("Randomise all effect parameter values, randomise which effects are enabled, and randomise their order.");

	randomiseButton.onClick = [this] {
		itemData.randomise();
		listBox.updateContent();
	};

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
    bool anySelected = false;
    {
        juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
        for (const auto& eff : audioProcessor.toggleableEffects) {
            const bool isSelected = (eff->selected == nullptr) ? true : eff->selected->getBoolValue();
            if (isSelected) { anySelected = true; break; }
        }
    }
    showingGrid = !anySelected;
    grid.onEffectSelected = [this](const juce::String& effectId) {
        {
            juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
            // Mark the chosen effect as selected and enabled, and move it to the end
            std::shared_ptr<osci::Effect> chosen;
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
    area = area.reduced(20);
    frequency.setBounds(area.removeFromTop(30));

    area.removeFromTop(6);
    if (showingGrid) {
        grid.setBounds(area);
        grid.setVisible(true);
        addEffectButton.setVisible(false);
        listBox.setVisible(false);
    } else {
        // Reserve space at bottom for the add button
        auto addBtnHeight = 44;
        auto listArea = area;
        auto buttonArea = listArea.removeFromBottom(addBtnHeight);
        listBox.setBounds(listArea);
        listBox.setVisible(true);
        grid.setVisible(false);
        listBox.updateContent();
        addEffectButton.setVisible(true);
        addEffectButton.setBounds(buttonArea.reduced(0, 4));
    }
}

void EffectsComponent::changeListenerCallback(juce::ChangeBroadcaster* source) {
    itemData.resetData();
    listBox.updateContent();
}
