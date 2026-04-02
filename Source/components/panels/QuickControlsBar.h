#pragma once

#include <JuceHeader.h>
#include "../../PluginProcessor.h"
#include "../KnobContainerComponent.h"

class OscirenderAudioProcessorEditor;

// Compact horizontal bar housing miscellaneous parameter knobs (frequency,
// perspective strength, field of view). Laid out similarly to MidiComponent
// but without a toggle section.
class QuickControlsBar : public juce::Component {
public:
    QuickControlsBar(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor);
    ~QuickControlsBar() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;

    KnobContainerComponent frequencyKnob{"FREQUENCY"};
    KnobContainerComponent perspectiveKnob{"PERSPECTIVE"};
    KnobContainerComponent fovKnob{"FOV"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuickControlsBar)
};
