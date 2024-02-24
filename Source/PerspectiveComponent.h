#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "components/EffectComponent.h"
#include "components/SvgButton.h"

class OscirenderAudioProcessorEditor;
class PerspectiveComponent : public juce::GroupComponent {
public:
    PerspectiveComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
    ~PerspectiveComponent();

    void resized() override;
private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;

    EffectComponent perspective{audioProcessor, *audioProcessor.perspective, 0};
    EffectComponent focalLength{audioProcessor, *audioProcessor.perspective, 1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PerspectiveComponent)
};
