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

    std::unique_ptr<juce::Drawable> fixedRotateWhite;
    std::unique_ptr<juce::Drawable> fixedRotateRed;
    std::shared_ptr<juce::DrawableButton> fixedRotateX = std::make_shared<juce::DrawableButton>("fixedRotateX", juce::DrawableButton::ButtonStyle::ImageFitted);
    std::shared_ptr<juce::DrawableButton> fixedRotateY = std::make_shared<juce::DrawableButton>("fixedRotateY", juce::DrawableButton::ButtonStyle::ImageFitted);
    std::shared_ptr<juce::DrawableButton> fixedRotateZ = std::make_shared<juce::DrawableButton>("fixedRotateZ", juce::DrawableButton::ButtonStyle::ImageFitted);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ObjComponent)
};