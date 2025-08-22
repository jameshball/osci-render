#pragma once

#include <JuceHeader.h>
#include "LookAndFeel.h"
#include "audio/BitCrushEffect.h"
#include "PluginProcessor.h"
#include "components/DraggableListBox.h"
#include "components/EffectsListComponent.h"
#include "components/ScrollFadeViewport.h"
#include "components/EffectTypeGridComponent.h"

class OscirenderAudioProcessorEditor;
class EffectsComponent : public juce::GroupComponent, public juce::ChangeListener {
public:
	EffectsComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
	~EffectsComponent() override;

	void resized() override;
	void changeListenerCallback(juce::ChangeBroadcaster* source) override;

private:
	OscirenderAudioProcessor& audioProcessor;

	// juce::TextButton addBtn;

	SvgButton randomiseButton{ "randomise", juce::String(BinaryData::random_svg), Colours::accentColor };

	AudioEffectListBoxItemData itemData;
	EffectsListBoxModel listBoxModel;
	DraggableListBox listBox;
	juce::TextButton addEffectButton { "+ Add new effect" }; // Separate button under the list
	EffectTypeGridComponent grid { audioProcessor };
	bool showingGrid = true; // show grid by default

	const int LIST_SPACER = 4; // Space above the list to show drop indicator
	
	EffectComponent frequency = EffectComponent(*audioProcessor.frequencyEffect, false);

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsComponent)
};