#pragma once

#include "MotionStyle.h"
#include "FormControls.h"
#include <functional>

// A 3D object's one setting, beside its row in Sources: fitted to the
// screen (centred, scaled to fill it) or kept at its file's own size and
// place. It applies at once, so the Scene shows the change as it is made.
class MotionModelSizePanel final : public juce::Component {
public:
    MotionModelSizePanel(const juce::String& name, bool original, double fittedScale) : scale(fittedScale) {
        setName("3D object settings");
        title.setText(name, juce::dontSendNotification);
        title.setFont(motion::style::heading());
        title.setColour(juce::Label::textColourId, osci::Colours::text());
        title.setBorderSize({});
        sizeLabel.setFont(motion::style::body());
        sizeLabel.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        sizeLabel.setBorderSize({});
        note.setFont(motion::style::caption());
        note.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        note.setBorderSize({});
        note.setJustificationType(juce::Justification::topLeft);
        size.setSelected(original ? 1 : 0);
        size.setTooltip("Fit to screen centres the model and scales it to fill the screen. Original keeps the size and place its file gives it.");
        size.onChange = [this](int index) {
            describe();
            if (onChange) { onChange(index == 1); }
        };
        for (auto* component : std::initializer_list<juce::Component*> {&title, &sizeLabel, &size, &note}) { addAndMakeVisible(component); }
        describe();
        setSize(300, 112);
    }

    std::function<void(bool original)> onChange;

    void resized() override {
        auto area = getLocalBounds().reduced(14, 12);
        title.setBounds(area.removeFromTop(20));
        area.removeFromTop(10);
        auto line = area.removeFromTop(28);
        sizeLabel.setBounds(line.removeFromLeft(44));
        size.setBounds(line);
        area.removeFromTop(8);
        note.setBounds(area);
    }

private:
    // What the choice does to this model, in its own numbers.
    void describe() {
        const auto times = juce::String::fromUTF8("\xc3\x97");
        const auto factor = scale >= 10 ? juce::String(scale, 0) : scale >= 1 ? juce::String(scale, 1)
            : juce::String(scale, std::clamp(1 - static_cast<int>(std::floor(std::log10(std::max(scale, 1.0e-9)))), 2, 6));
        note.setText(size.getSelected() == 1 ? juce::String("At its file's own size and place, unscaled.")
                                             : "Centred and scaled " + times + factor + " to fill the screen.", juce::dontSendNotification);
    }

    double scale = 1;
    juce::Label title, sizeLabel {"Size caption", "Size"}, note;
    motion::ui::SegmentedControl size {"3D object size", {"Fit to screen", "Original"}};
};
