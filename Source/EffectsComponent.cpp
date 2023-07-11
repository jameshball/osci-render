#include "EffectsComponent.h"
#include "audio/BitCrushEffect.h"
#include "PluginEditor.h"

EffectsComponent::EffectsComponent(OscirenderAudioProcessor& p) : audioProcessor(p), itemData(p), listBoxModel(listBox, itemData) {
	setText("Audio Effects");

    addAndMakeVisible(frequency);
    frequency.setCheckboxVisible(false);

    frequency.slider.setSkewFactorFromMidPoint(500.0);
    frequency.slider.setTextValueSuffix("Hz");
    frequency.slider.setValue(audioProcessor.frequency, juce::dontSendNotification);

    frequency.slider.onValueChange = [this] {
        audioProcessor.frequency = frequency.slider.getValue();
        if (audioProcessor.currentSampleRate > 0.0) {
            audioProcessor.updateAngleDelta();
        }
    };

    {
        juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
        for (int i = 0; i < audioProcessor.allEffects.size(); i++) {
            auto effect = audioProcessor.allEffects[i];
            effect->setValue(effect->getValue());
            itemData.data.push_back(effect);
        }
    }

    /*addBtn.setButtonText("Add Item...");
    addBtn.onClick = [this]()
    {
        itemData.data.push_back(juce::String("Item " + juce::String(1 + itemData.getNumItems())));
        listBox.updateContent();
    };
    addAndMakeVisible(addBtn);*/

    listBox.setModel(&listBoxModel);
    addAndMakeVisible(listBox);
}

EffectsComponent::~EffectsComponent() {
    
}

void EffectsComponent::resized() {
    auto area = getLocalBounds().reduced(20);
    frequency.setBounds(area.removeFromTop(30));

    area.removeFromTop(6);
    listBox.setBounds(area);
}
