#pragma once

#include <JuceHeader.h>
#include "LookAndFeel.h"
#include "audio/BitCrushEffect.h"
#include "PluginProcessor.h"
#include "components/DraggableListBox.h"
#include "components/EffectsListComponent.h"
#include "components/ComponentList.h"
#include "components/LuaListComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

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
	
	EffectComponent frequency = EffectComponent(*audioProcessor.frequencyEffect, false);

	juce::TextButton loadPresetButton { "Load" };
	juce::TextButton savePresetButton { "Save" };
	std::unique_ptr<juce::FileChooser> presetChooser;

	void loadPresetClicked();
	void savePresetClicked();

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsComponent)
};