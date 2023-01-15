#pragma once

#include <JuceHeader.h>

class EffectComponentGroup : public juce::Component {
public:
	EffectComponentGroup(const juce::String& id, const juce::String& name);
	~EffectComponentGroup() override;

	void resized() override;

	juce::Slider slider;
	juce::Label label;
	juce::String id;

private:
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectComponentGroup)
};

