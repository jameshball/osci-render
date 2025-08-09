#include "EffectsComponent.h"
#include "audio/BitCrushEffect.h"
#include "PluginEditor.h"

EffectsComponent::EffectsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), itemData(p, editor), listBoxModel(listBox, itemData) {
	setText("Audio Effects");

    addAndMakeVisible(frequency);

    frequency.slider.setSkewFactorFromMidPoint(500.0);
    frequency.slider.setTextValueSuffix("Hz");
    frequency.slider.setValue(audioProcessor.frequencyEffect->getValue(), juce::dontSendNotification);

    /*addBtn.setButtonText("Add Item...");
    addBtn.onClick = [this]()
    {
        itemData.data.push_back(juce::String("Item " + juce::String(1 + itemData.getNumItems())));
        listBox.updateContent();
    };
    addAndMakeVisible(addBtn);*/

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
        if (grid)
            grid->setVisible(true);
        listBox.setVisible(false);
        resized();
        repaint();
    };

    // Start with grid visible by default
    showingGrid = true;
    grid = std::make_unique<EffectTypeGridComponent>(audioProcessor);
    grid->onEffectSelected = [this](const juce::String& effectId) {
        DBG("Effect selected from grid: " + effectId);
        // Mark the chosen effect as selected and enabled (no instance creation for now)
        {
            juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
            for (auto& eff : audioProcessor.toggleableEffects) {
                if (eff->getId() == effectId) {
                    eff->markSelectable(true);
                    break;
                }
            }
        }
        // Refresh list content
        itemData.resetData();
        listBox.updateContent();
        showingGrid = false;
        listBox.setVisible(true);
        if (grid)
            grid->setVisible(false);
        resized();
        repaint();
    };
    grid->onCanceled = [this]() {
        // If canceled while default grid, just show list
        showingGrid = false;
        listBox.setVisible(true);
        if (grid)
            grid->setVisible(false);
        resized();
        repaint();
    };

    listBox.setModel(&listBoxModel);
    addAndMakeVisible(listBox);
    addAndMakeVisible(*grid);
    listBox.setVisible(false); // grid shown first
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
        if (grid)
            grid->setBounds(area);
    } else {
        listBox.setBounds(area);
    }
}

void EffectsComponent::changeListenerCallback(juce::ChangeBroadcaster* source) {
    itemData.resetData();
    listBox.updateContent();
}
