#pragma once

#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>
#include <algorithm>
#include <functional>

class MotionFractalSettingsPanel final : public juce::Component {
public:
    explicit MotionFractalSettingsPanel(int initialDepth) {
        setName("Fractal preparation settings");
        depth.setName("Fractal depth");
        depth.setSliderStyle(juce::Slider::LinearHorizontal);
        depth.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 26);
        depth.setRange(0, 15, 1);
        depth.setValue(std::clamp(initialDepth, 0, 15), juce::dontSendNotification);
        depth.setTooltip("Higher values add L-system detail and increase preparation time.");
        depth.setColour(juce::Slider::textBoxBackgroundColourId, osci::Colours::veryDark());
        depth.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        depth.setColour(juce::Slider::textBoxTextColourId, osci::Colours::text());

        depthLabel.setFont(juce::FontOptions(13));
        depthLabel.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        depthLabel.setBorderSize({});

        note.setFont(juce::FontOptions(12));
        note.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        note.setBorderSize({});
        note.setJustificationType(juce::Justification::topLeft);
        note.setText("Prepared once at the chosen depth.\nChange the depth here to rebuild this source.", juce::dontSendNotification);

        prepare.setName("Prepare fractal");
        prepare.setButtonText("Prepare fractal");
        prepare.onClick = [this] {
            if (onPrepare) {
                onPrepare(juce::roundToInt(depth.getValue()));
            }
        };

        for (auto* component : {static_cast<juce::Component*>(&depthLabel), static_cast<juce::Component*>(&depth),
                static_cast<juce::Component*>(&note), static_cast<juce::Component*>(&prepare)}) {
            addAndMakeVisible(component);
        }
        setSize(440, 150);
    }

    std::function<void(int)> onPrepare;

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        auto row = area.removeFromTop(28);
        depthLabel.setBounds(row.removeFromLeft(112));
        depth.setBounds(row);
        area.removeFromTop(10);
        note.setBounds(area.removeFromTop(42));
        area.removeFromTop(8);
        prepare.setBounds(area.removeFromBottom(30));
    }

private:
    juce::Slider depth;
    juce::Label depthLabel {"Fractal depth caption", "Fractal depth"};
    juce::Label note;
    juce::TextButton prepare;
};
