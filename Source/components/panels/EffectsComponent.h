#pragma once

#include <JuceHeader.h>
#include "../../LookAndFeel.h"
#include "../../audio/effects/BitCrushEffect.h"
#include "../../PluginProcessor.h"
#include "../DraggableListBox.h"
#include "../effects/EffectsListComponent.h"
#include "../ScrollFadeViewport.h"
#include "../effects/EffectTypeGridComponent.h"
#include "../ParameterSyncHelper.h"

class OscirenderAudioProcessorEditor;
class EffectsComponent : public juce::GroupComponent, public juce::ChangeListener {
public:
	EffectsComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
	~EffectsComponent() override;

	void resized() override;
	void changeListenerCallback(juce::ChangeBroadcaster* source) override;

private:
	OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& editor;

	SvgButton randomiseButton{ "randomise", juce::String(BinaryData::random_svg), Colours::accentColor() };
	SvgButton autoLinkButton{ "autoLink", juce::String(BinaryData::link_svg), juce::Colours::white.withAlpha(0.5f), Colours::accentColor() };

	AudioEffectListBoxItemData itemData;
	EffectsListBoxModel listBoxModel;
	DraggableListBox listBox;
	juce::TextButton addEffectButton { "Add new effect" }; // Separate button under the list
	EffectTypeGridComponent grid;
	bool showingGrid = true; // show grid by default

	const int LIST_SPACER = 4; // Space above the list to show drop indicator

	bool hasAnySelectedEffects() const;

	// Syncs the list UI when an effect's selected/enabled param changes (e.g. via undo/redo)
	void refreshListFromParams();
	ParameterSyncHelper paramSync { [this] { refreshListFromParams(); } };

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsComponent)
};