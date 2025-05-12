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

    listBox.setModel(&listBoxModel);
    addAndMakeVisible(listBox);
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
    listBox.setBounds(area);
}

void EffectsComponent::changeListenerCallback(juce::ChangeBroadcaster* source) {
    itemData.resetData();
    listBox.updateContent();
}
