#pragma once

#include <JuceHeader.h>
#include "LookAndFeel.h"
#include "audio/BitCrushEffect.h"
#include "PluginProcessor.h"
#include "components/DraggableListBox.h"
#include "components/EffectsListComponent.h"
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
	std::unique_ptr<EffectTypeGridComponent> grid;
	bool showingGrid = true; // show grid by default
	
	EffectComponent frequency = EffectComponent(*audioProcessor.frequencyEffect, false);

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsComponent)
};