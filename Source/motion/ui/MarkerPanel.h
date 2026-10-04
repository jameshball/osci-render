#pragma once

#include "MotionStyle.h"

#include "../model/TimeGrid.h"
#include <osci_gui/osci_gui.h>

class MotionMarkerPanel final : public juce::Component {
public:
    MotionMarkerPanel(juce::String initialName, double seconds, motion::TimeGrid grid, double duration) : grid(grid), duration(duration), originalTime(seconds) {
        name.setName("Marker name");
        position.setName("Marker position");
        name.setText(initialName, false);
        initialPosition = juce::String(grid.positionLabel(seconds));
        position.setText(initialPosition, false);
        position.setTooltip("Use the current time display, or append s for seconds or f for frames.");
        for (auto* field : {&name, &position}) {
            field->setFont(motion::style::body());
            field->setColour(juce::TextEditor::backgroundColourId, osci::Colours::veryDark());
            field->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            field->setSelectAllWhenFocused(true);
            field->onTextChange = [this] { refresh(); };
            field->onReturnKey = [this] { apply.triggerClick(); };
            addAndMakeVisible(*field);
        }
        nameLabel.setText("Name", juce::dontSendNotification);
        positionLabel.setText("Position", juce::dontSendNotification);
        motion::style::dialog::caption(nameLabel);
        motion::style::dialog::caption(positionLabel);
        status.setFont(motion::style::body());
        status.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        apply.onClick = [this] { if (time.has_value() && apply.isEnabled() && onApply) { onApply(name.getText().trim(), *time); } };
        for (auto* component : std::initializer_list<juce::Component*>{&nameLabel, &positionLabel, &status, &apply}) { addAndMakeVisible(component); }
        refresh();
    }
    std::function<void(juce::String, double)> onApply;
    void setError(const juce::String& message) { status.setText(message, juce::dontSendNotification); }
    void resized() override {
        auto bounds = getLocalBounds().reduced(motion::style::dialog::margin);
        status.setBounds(motion::style::dialog::footer(bounds, {&apply}));
        motion::style::dialog::formRow(bounds, nameLabel, name, 72);
        motion::style::dialog::formRow(bounds, positionLabel, position, 72);
    }
private:
    void refresh() {
        time = position.getText() == initialPosition ? std::optional<double>(originalTime) : grid.parsePosition(position.getText().toStdString());
        const bool validName = name.getText().trim().isNotEmpty() && name.getText().trim().length() <= 120;
        const bool validTime = time.has_value() && *time >= 0 && *time <= duration;
        apply.setEnabled(validName && validTime);
        status.setText(!validName ? "Name: 1-120 characters" : !validTime ? "Choose a position within the composition." : "", juce::dontSendNotification);
    }
    motion::TimeGrid grid;
    double duration, originalTime;
    juce::String initialPosition;
    std::optional<double> time;
    juce::TextEditor name, position;
    juce::Label nameLabel, positionLabel, status;
    juce::TextButton apply {"Save marker"};
};

// Adds or edits one tempo change: the tempo from a beat onwards.
class MotionTempoPanel final : public juce::Component {
public:
    MotionTempoPanel(double beat, double bpm, int beatsPerBar, bool ramped = false) {
        ramp.setName("Ramp into tempo");
        ramp.setTitle("Ramp into tempo");
        ramp.setButtonText("Glide from the previous tempo");
        ramp.setToggleState(ramped, juce::dontSendNotification);
        ramp.setTooltip("Off: the tempo jumps here. On: it changes smoothly (linearly in beats) from the previous tempo point and arrives here.");
        const auto bar = std::max(1, beatsPerBar);
        tempo.setName("Tempo change BPM");
        tempo.setTitle("Tempo change BPM");
        tempo.setText(juce::String(bpm, bpm == std::round(bpm) ? 0 : 2), false);
        tempo.setFont(motion::style::body());
        tempo.setColour(juce::TextEditor::backgroundColourId, osci::Colours::veryDark());
        tempo.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        tempo.setSelectAllWhenFocused(true);
        tempo.setInputRestrictions(8, "0123456789.");
        tempo.onTextChange = [this] { refresh(); };
        tempo.onReturnKey = [this] { apply.triggerClick(); };
        tempoLabel.setText("BPM", juce::dontSendNotification);
        where.setText("From bar " + juce::String(static_cast<int>(std::floor(beat / bar)) + 1) + ", beat " + juce::String(beat - bar * std::floor(beat / bar) + 1, beat == std::round(beat) ? 0 : 2) + " onwards", juce::dontSendNotification);
        motion::style::dialog::caption(where);
        motion::style::dialog::caption(tempoLabel);
        ramp.setColour(juce::ToggleButton::textColourId, motion::style::text());
        status.setFont(motion::style::body());
        status.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        apply.setTitle("Save tempo");
        apply.onClick = [this] { if (apply.isEnabled() && onApply) { onApply(tempo.getText().getDoubleValue(), ramp.getToggleState()); } };
        for (auto* component : std::initializer_list<juce::Component*>{&tempo, &tempoLabel, &where, &ramp, &status, &apply}) { addAndMakeVisible(component); }
        refresh();
    }
    std::function<void(double, bool)> onApply;
    void setError(const juce::String& message) { status.setText(message, juce::dontSendNotification); }
    void resized() override {
        auto bounds = getLocalBounds().reduced(motion::style::dialog::margin);
        status.setBounds(motion::style::dialog::footer(bounds, {&apply}));
        where.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(motion::style::dialog::rowGap);
        motion::style::dialog::formRow(bounds, tempoLabel, tempo, 72);
        // The tick box lines up with the field's left edge.
        ramp.setBounds(bounds.removeFromTop(motion::style::dialog::row).withTrimmedLeft(72 - 4));
    }
private:
    void refresh() {
        const auto value = tempo.getText().getDoubleValue();
        const bool valid = std::isfinite(value) && value >= 1 && value <= 1000;
        apply.setEnabled(valid);
        status.setText(valid ? "" : "Tempo: 1-1000 BPM", juce::dontSendNotification);
    }
    juce::TextEditor tempo;
    juce::Label tempoLabel, where, status;
    juce::ToggleButton ramp;
    juce::TextButton apply {"Save tempo"};
};
