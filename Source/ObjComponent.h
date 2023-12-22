#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "components/EffectComponent.h"
#include "components/SvgButton.h"

class OscirenderAudioProcessorEditor;
class ObjComponent : public juce::GroupComponent {
public:
    ObjComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
    ~ObjComponent();

    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void disableMouseRotation();
private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;

    EffectComponent focalLength{audioProcessor, *audioProcessor.focalLength, false};
    EffectComponent rotateX{audioProcessor, *audioProcessor.rotateX, false};
    EffectComponent rotateY{audioProcessor, *audioProcessor.rotateY, false};
    EffectComponent rotateZ{audioProcessor, *audioProcessor.rotateZ, false};
    EffectComponent rotateSpeed{audioProcessor, *audioProcessor.rotateSpeed, false};

    juce::TextButton resetRotation{"Reset Rotation"};
    juce::ToggleButton mouseRotate{"Rotate with Mouse (Esc to disable)"};

    std::shared_ptr<SvgButton> fixedRotateX = std::make_shared<SvgButton>("fixedRotateX", juce::String(BinaryData::fixed_rotate_svg), "white", "red", audioProcessor.fixedRotateX);
    std::shared_ptr<SvgButton> fixedRotateY = std::make_shared<SvgButton>("fixedRotateY", juce::String(BinaryData::fixed_rotate_svg), "white", "red", audioProcessor.fixedRotateY);
    std::shared_ptr<SvgButton> fixedRotateZ = std::make_shared<SvgButton>("fixedRotateZ", juce::String(BinaryData::fixed_rotate_svg), "white", "red", audioProcessor.fixedRotateZ);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ObjComponent)
};
