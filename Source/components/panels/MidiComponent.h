#pragma once

#include <JuceHeader.h>
#include "../../PluginProcessor.h"
#include "../ParameterBarComponent.h"
#include "../KnobContainerComponent.h"
#include "../ToggleLabelComponent.h"
#include "../SlopeGraphComponent.h"
#include "../DisabledOverlay.h"
#include "../SvgSwitchButton.h"

class OscirenderAudioProcessorEditor;
class MidiComponent : public juce::Component, public juce::AudioProcessorParameter::Listener, public juce::AsyncUpdater {
public:
	MidiComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
	~MidiComponent() override;

	void parameterValueChanged(int parameterIndex, float newValue) override;
	void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
	void handleAsyncUpdate() override;

	void resized() override;
	void paint(juce::Graphics& g) override;
private:
	OscirenderAudioProcessor& audioProcessor;
	OscirenderAudioProcessorEditor& pluginEditor;

    SvgSwitchButton midiSwitch{"midi", juce::String(BinaryData::midi_svg), audioProcessor.midiEnabled};
	ParameterBarComponent voicesBar{audioProcessor.voices, "VOICES"};
#if OSCI_PREMIUM
	ParameterBarComponent bendBar{audioProcessor.pitchBendRange, "BEND"};
	KnobContainerComponent velTrkKnob{"VELOCITY"};
	KnobContainerComponent glideKnob{"GLIDE"};

	SlopeGraphComponent slopeGraph{audioProcessor.glideSlope, "SLOPE"};

	ToggleLabelComponent alwaysGlideToggle{audioProcessor.alwaysGlide};
	ToggleLabelComponent legatoToggle{audioProcessor.legato};
	ToggleLabelComponent octaveScaleToggle{audioProcessor.octaveScale};
	ParameterBarComponent tempoBar{audioProcessor.standaloneBpm, "BPM"};
	DisabledOverlay disabledOverlay;
#endif

	void updateEnabledState();

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiComponent)
};
