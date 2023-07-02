#pragma once

#include <JuceHeader.h>
#include "audio/BitCrushEffect.h"
#include "PluginProcessor.h"
#include "components/DraggableListBox.h"
#include "components/MyListComponent.h"

class EffectsComponent : public juce::GroupComponent {
public:
	EffectsComponent(OscirenderAudioProcessor&);
	~EffectsComponent() override;

	void resized() override;

private:
	OscirenderAudioProcessor& audioProcessor;

	// juce::TextButton addBtn;

	MyListBoxItemData itemData;
	MyListBoxModel listBoxModel;
	DraggableListBox listBox;
	
	EffectComponent frequency = EffectComponent(0.0, 12000.0, 0.1, 400, "Frequency", "frequency");

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsComponent)
};