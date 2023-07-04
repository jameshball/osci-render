#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "components/EffectComponent.h"

class OscirenderAudioProcessorEditor;
class ObjComponent : public juce::GroupComponent {
public:
    ObjComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);

    void resized() override;
private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;

    EffectComponent focalLength{0, 2, 0.01, audioProcessor.focalLength, false};
    EffectComponent rotateX{-1, 1, 0.01, audioProcessor.rotateX, false};
    EffectComponent rotateY{-1, 1, 0.01, audioProcessor.rotateY, false};
    EffectComponent rotateZ{-1, 1, 0.01, audioProcessor.rotateZ, false};
    EffectComponent rotateSpeed{-1, 1, 0.01, audioProcessor.rotateSpeed, false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ObjComponent)
};