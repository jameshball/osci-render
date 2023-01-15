#pragma once

#include <JuceHeader.h>
#include "EffectComponentGroup.h"
#include "PluginProcessor.h"

class EffectsComponent : public juce::GroupComponent {
public:
	EffectsComponent(OscirenderAudioProcessor&);
	~EffectsComponent() override;

	void resized() override;

private:
	OscirenderAudioProcessor& audioProcessor;

	EffectComponentGroup frequency = EffectComponentGroup("frequency", "Frequency");

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsComponent)
};