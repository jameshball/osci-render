#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "components/EffectComponent.h"

class OscirenderAudioProcessorEditor;
class ObjComponent : public juce::GroupComponent, public juce::MouseListener {
public:
    ObjComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
    ~ObjComponent();

    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;

    EffectComponent focalLength{0, 2, 0.01, audioProcessor.focalLength, false};
    EffectComponent rotateX{-1, 1, 0.01, audioProcessor.rotateX, false};
    EffectComponent rotateY{-1, 1, 0.01, audioProcessor.rotateY, false};
    EffectComponent rotateZ{-1, 1, 0.01, audioProcessor.rotateZ, false};
    EffectComponent rotateSpeed{-1, 1, 0.01, audioProcessor.rotateSpeed, false};

    juce::TextButton resetRotation{"Reset Rotation"};
    juce::ToggleButton mouseRotate{"Rotate with Mouse (Esc to disable)"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ObjComponent)
};