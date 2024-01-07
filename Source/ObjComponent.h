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

    EffectComponent perspective{audioProcessor, *audioProcessor.perspective, 0};
    EffectComponent focalLength{audioProcessor, *audioProcessor.perspective, 1};
    EffectComponent distance{audioProcessor, *audioProcessor.perspective, 2};
    EffectComponent rotateSpeed{audioProcessor, *audioProcessor.perspective, 3};
    EffectComponent rotateX{audioProcessor, *audioProcessor.perspective, 4};
    EffectComponent rotateY{audioProcessor, *audioProcessor.perspective, 5};
    EffectComponent rotateZ{audioProcessor, *audioProcessor.perspective, 6};

    juce::TextButton resetRotation{"Reset Rotation"};
    juce::ToggleButton mouseRotate{"Rotate with Mouse (Esc to disable)"};

    std::shared_ptr<SvgButton> fixedRotateX = std::make_shared<SvgButton>("fixedRotateX", juce::String(BinaryData::fixed_rotate_svg), "white", "red", audioProcessor.perspectiveEffect->fixedRotateX);
    std::shared_ptr<SvgButton> fixedRotateY = std::make_shared<SvgButton>("fixedRotateY", juce::String(BinaryData::fixed_rotate_svg), "white", "red", audioProcessor.perspectiveEffect->fixedRotateY);
    std::shared_ptr<SvgButton> fixedRotateZ = std::make_shared<SvgButton>("fixedRotateZ", juce::String(BinaryData::fixed_rotate_svg), "white", "red", audioProcessor.perspectiveEffect->fixedRotateZ);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ObjComponent)
};
