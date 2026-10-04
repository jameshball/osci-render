#pragma once

#include "MotionStyle.h"

#include "../model/MidiInstrument.h"
#include <osci_gui/osci_gui.h>

class MotionMidiEnvelopePanel final : public juce::Component {
public:
    explicit MotionMidiEnvelopePanel(motion::MidiInstrument initial) {
        const std::array<juce::String, 4> names {"Attack (s)", "Decay (s)", "Sustain (%)", "Release (s)"};
        const std::array<juce::String, 4> accessible {"MIDI attack", "MIDI decay", "MIDI sustain", "MIDI release"};
        const auto values = initial.key();
        for (std::size_t index = 0; index < fields.size(); ++index) {
            labels[index].setText(names[index], juce::dontSendNotification);
            motion::style::dialog::caption(labels[index]);
            fields[index].setName(accessible[index]);
            fields[index].setSliderStyle(juce::Slider::LinearHorizontal);
            fields[index].setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 24);
            fields[index].setRange(0, index == 2 ? 100 : 30, index == 2 ? .1 : .001);
            if (index != 2) { fields[index].setSkewFactor(.3); }
            fields[index].setValue(values[index] * (index == 2 ? 100 : 1), juce::dontSendNotification);
            fields[index].onValueChange = [this] { repaint(); };
            addAndMakeVisible(labels[index]); addAndMakeVisible(fields[index]);
        }

        note.setFont(motion::style::body());
        note.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        note.setJustificationType(juce::Justification::topLeft);
        apply.onClick = [this] { if (onApply) { onApply(value()); } };
        addAndMakeVisible(note); addAndMakeVisible(apply);
    }
    std::function<void(motion::MidiInstrument)> onApply;
    motion::MidiInstrument value() const { return {fields[0].getValue(), fields[1].getValue(), fields[2].getValue() / 100, fields[3].getValue()}; }
    void resized() override {
        auto area = getLocalBounds().reduced(motion::style::dialog::margin);
        motion::style::dialog::footer(area, {&apply});
        preview = area.removeFromTop(68).toFloat();
        area.removeFromTop(motion::style::dialog::rowGap);
        for (std::size_t index = 0; index < fields.size(); ++index) { motion::style::dialog::formRow(area, labels[index], fields[index], 100); }
    }
    void paint(juce::Graphics& g) override {
        const auto settings = value();
        const double held = 1;
        const auto duration = std::max(.001, settings.attack + settings.decay + held + settings.release);
        const auto x = [&](double time) { return preview.getX() + static_cast<float>(time / duration) * preview.getWidth(); };
        const auto y = [&](double level) { return preview.getBottom() - static_cast<float>(level) * preview.getHeight(); };
        g.setColour(osci::Colours::textMuted().withAlpha(.2f));
        g.drawHorizontalLine(juce::roundToInt(preview.getBottom()), preview.getX(), preview.getRight());
        const auto off = settings.attack + settings.decay + held;
        g.drawVerticalLine(juce::roundToInt(x(off)), preview.getY(), preview.getBottom());
        juce::Path path;
        path.startNewSubPath(x(0), y(0)); path.lineTo(x(settings.attack), y(1));
        path.lineTo(x(settings.attack + settings.decay), y(settings.sustain));
        path.lineTo(x(off), y(settings.sustain)); path.lineTo(x(duration), y(0));
        g.setColour(osci::Colours::accentColor()); g.strokePath(path, juce::PathStrokeType(1.5f));
    }
private:
    std::array<juce::Slider, 4> fields;
    std::array<juce::Label, 4> labels;
    juce::Label note;
    juce::TextButton apply {"Apply envelope"};
    juce::Rectangle<float> preview;
};
