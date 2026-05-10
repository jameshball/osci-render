#pragma once

#include <JuceHeader.h>
#include "../../PluginProcessor.h"
#include "../KnobContainerComponent.h"
#include "../ParameterBarComponent.h"
#include "../SvgSwitchButton.h"

class OscirenderAudioProcessorEditor;

// Compact horizontal bar housing miscellaneous parameter knobs (frequency,
// perspective strength, field of view). Laid out similarly to MidiComponent
// but without a toggle section.
class QuickControlsBar : public juce::Component
    , public juce::AudioProcessorParameter::Listener
    , public juce::AsyncUpdater
{
public:
    QuickControlsBar(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor);
    ~QuickControlsBar() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;

private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;

    KnobContainerComponent frequencyKnob{"FREQUENCY"};
    KnobContainerComponent perspectiveKnob{"PERSPECTIVE"};
    KnobContainerComponent fovKnob{"FOV"};

#if !OSCI_PREMIUM
    SvgSwitchButton midiSwitch{"midi", juce::String(BinaryData::midi_svg), audioProcessor.midiEnabled};
    ParameterBarComponent voicesBar{audioProcessor.voices, "VOICES"};
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuickControlsBar)
};
