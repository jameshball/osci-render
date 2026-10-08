#pragma once

#include "Sheet.h"
#include "TypedNumber.h"

#include "../model/TimeGrid.h"

// Names a marker and places it, in the ruler's notation.
class MotionMarkerPanel final : public motion::ui::Popover {
public:
    MotionMarkerPanel(juce::String initialName, double seconds, motion::TimeGrid grid, double duration)
        : Popover("Marker", "Save", "Save marker"), grid(grid), duration(duration), originalTime(seconds) {
        name.setName("Marker name");
        position.setName("Marker position");
        name.setText(initialName, false);
        initialPosition = juce::String(grid.positionLabel(seconds));
        position.setText(initialPosition, false);
        position.setTooltip("Use the current time display, or append s for seconds or f for frames.");
        // Positions read like the other number fields: right-aligned.
        position.setJustification(juce::Justification::centredRight);
        name.setJustification(juce::Justification::centredLeft);
        for (auto* field : {&name, &position}) {
            field->setFont(motion::style::body());
            field->setSelectAllWhenFocused(true);
            field->onTextChange = [this] { refresh(); };
            field->onReturnKey = [this] { primary.triggerClick(); };
            addAndMakeVisible(*field);
        }
        nameLabel.setText("Name", juce::dontSendNotification);
        positionLabel.setText("Position", juce::dontSendNotification);
        motion::ui::Sheet::styleCaption(nameLabel);
        motion::ui::Sheet::styleCaption(positionLabel);
        primary.onClick = [this] { if (time.has_value() && primary.isEnabled() && onApply) { onApply(name.getText().trim(), *time); } };
        addAndMakeVisible(nameLabel);
        addAndMakeVisible(positionLabel);
        setSize(widthFor(256), heightFor(2));
        refresh();
    }
    std::function<void(juce::String, double)> onApply;

protected:
    void layoutBody(juce::Rectangle<int> area) override {
        formRow(area, nameLabel, name);
        formRow(area, positionLabel, position, number);
    }

private:
    void refresh() {
        time = position.getText() == initialPosition ? std::optional<double>(originalTime) : grid.parsePosition(position.getText().toStdString());
        const bool validName = name.getText().trim().isNotEmpty() && name.getText().trim().length() <= 120;
        const bool validTime = time.has_value() && *time >= 0 && *time <= duration;
        primary.setEnabled(validName && validTime);
        setError(!validName ? "Name: 1-120 characters" : !validTime ? "Outside the composition" : "");
    }
    motion::TimeGrid grid;
    double duration, originalTime;
    juce::String initialPosition;
    std::optional<double> time;
    juce::TextEditor name, position;
    juce::Label nameLabel, positionLabel;
};

// Adds or edits one tempo change: the tempo from a beat onwards.
class MotionTempoPanel final : public motion::ui::Popover {
public:
    MotionTempoPanel(double beat, double bpm, int beatsPerBar, bool ramped = false) : Popover("Tempo change", "Save", "Save tempo") {
        const auto bar = std::max(1, beatsPerBar);
        detail = "Bar " + juce::String(static_cast<int>(std::floor(beat / bar)) + 1) + ", beat " + juce::String(beat - bar * std::floor(beat / bar) + 1, beat == std::round(beat) ? 0 : 2);
        ramp.setToggleState(ramped, juce::dontSendNotification);
        ramp.setTooltip("Off: the tempo jumps here. On: it changes smoothly (linearly in beats) from the previous tempo point and arrives here.");
        tempo.setName("Tempo change BPM");
        tempo.setTitle("Tempo change BPM");
        tempo.setText(juce::String(bpm, bpm == std::round(bpm) ? 0 : 2), false);
        tempo.setFont(motion::style::body());
        tempo.setJustification(juce::Justification::centredRight);
        // The unit sits inside the field, after the number.
        tempo.setBorder({1, 1, 1, unitWidth});
        tempo.setSelectAllWhenFocused(true);
        tempo.setInputRestrictions(8, "0123456789.");
        tempo.onTextChange = [this] { refresh(); };
        tempo.onReturnKey = [this] { primary.triggerClick(); };
        tempoLabel.setText("Tempo", juce::dontSendNotification);
        rampLabel.setText("Glide in", juce::dontSendNotification);
        unit.setText("BPM", juce::dontSendNotification);
        for (auto* label : {&tempoLabel, &rampLabel, &unit}) { motion::ui::Sheet::styleCaption(*label); }
        unit.setInterceptsMouseClicks(false, false);
        primary.onClick = [this] {
            const auto value = motion::ui::parseNumber(tempo.getText());
            if (primary.isEnabled() && value.has_value() && onApply) { onApply(*value, ramp.getToggleState()); }
        };
        for (auto* component : std::initializer_list<juce::Component*>{&tempo, &tempoLabel, &unit, &rampLabel, &ramp}) { addAndMakeVisible(component); }
        setSize(widthFor(256), heightFor(2));
        refresh();
    }
    std::function<void(double, bool)> onApply;

protected:
    void layoutBody(juce::Rectangle<int> area) override {
        auto line = area.removeFromTop(row);
        tempoLabel.setBounds(line.removeFromLeft(caption));
        tempo.setBounds(line.removeFromLeft(number));
        unit.setBounds(tempo.getBounds().removeFromRight(unitWidth - 2));
        area.removeFromTop(rowGap);
        line = area.removeFromTop(row);
        rampLabel.setBounds(line.removeFromLeft(caption));
        ramp.setBounds(line.removeFromLeft(motion::ui::Switch::width + 4));
    }

private:
    void refresh() {
        const auto value = motion::ui::parseNumber(tempo.getText()).value_or(0);
        const bool valid = value >= 1 && value <= 1000;
        primary.setEnabled(valid);
        setError(valid ? "" : "Tempo: 1-1000 BPM");
    }
    static constexpr int unitWidth = 36;
    juce::TextEditor tempo;
    juce::Label tempoLabel, rampLabel, unit;
    motion::ui::Switch ramp {"Ramp into tempo"};
};
