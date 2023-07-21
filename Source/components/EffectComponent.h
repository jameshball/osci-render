#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../audio/Effect.h"
#include "LabelledTextBox.h"

class SmallComboBoxArrow : public juce::LookAndFeel_V4 {
    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override {
        auto cornerSize = box.findParentComponentOfClass<juce::ChoicePropertyComponent>() != nullptr ? 0.0f : 3.0f;
        juce::Rectangle<int> boxBounds{0, 0, width, height};

        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(boxBounds.toFloat(), cornerSize);

        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(boxBounds.toFloat().reduced(0.5f, 0.5f), cornerSize, 1.0f);

        juce::Rectangle<int> arrowZone{width - 15, 0, 10, height};
        juce::Path path;
        path.startNewSubPath((float) arrowZone.getX(), (float) arrowZone.getCentreY() - 3.0f);
        path.lineTo((float) arrowZone.getCentreX(), (float) arrowZone.getCentreY() + 4.0f);
        path.lineTo((float) arrowZone.getRight(), (float) arrowZone.getCentreY() - 3.0f);
        path.closeSubPath();

        g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha((box.isEnabled() ? 0.9f : 0.2f)));
        g.fillPath(path);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
        label.setBounds(1, 1, box.getWidth() - 15, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }
};

class EffectComponent : public juce::Component, public juce::AudioProcessorParameter::Listener, juce::AsyncUpdater {
public:
    EffectComponent(OscirenderAudioProcessor& p, Effect& effect, int index);
    EffectComponent(OscirenderAudioProcessor& p, Effect& effect, int index, bool checkboxVisible);
    EffectComponent(OscirenderAudioProcessor& p, Effect& effect);
    EffectComponent(OscirenderAudioProcessor& p, Effect& effect, bool checkboxVisible);
    ~EffectComponent();

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;

    void setCheckboxVisible(bool visible);
    void setComponent(std::shared_ptr<juce::Component> component);

    juce::Slider slider;
    juce::Slider lfoSlider;
    Effect& effect;
    int index;
    juce::ToggleButton selected;
    SmallComboBoxArrow lfoLookAndFeel;
    juce::ComboBox lfo;

private:
    void setupComponent();
    bool checkboxVisible = true;
    bool lfoEnabled = true;
    juce::Rectangle<int> textBounds;
    std::shared_ptr<juce::Component> component;
    OscirenderAudioProcessor& audioProcessor;

    juce::Label popupLabel;
    LabelledTextBox min{"Min"};
    LabelledTextBox max{"Max"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectComponent)
};
