#pragma once

#include "../model/TimeGrid.h"
#include <osci_gui/osci_gui.h>

class MotionMarkerPanel final : public juce::Component {
public:
    MotionMarkerPanel(juce::String initialName, double seconds, motion::TimeGrid grid, double duration) : grid(grid), duration(duration), originalTime(seconds) {
        name.setName("Marker name");
        position.setName("Marker position");
        name.setText(initialName, false);
        initialPosition = juce::String(seconds, 9) + "s";
        position.setText(initialPosition, false);
        position.setTooltip("Use the current time display, or append s for seconds or f for frames.");
        for (auto* field : {&name, &position}) {
            field->setFont(juce::FontOptions(14));
            field->setColour(juce::TextEditor::backgroundColourId, osci::Colours::veryDark());
            field->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            field->setSelectAllWhenFocused(true);
            field->onTextChange = [this] { refresh(); };
            field->onReturnKey = [this] { apply.triggerClick(); };
            addAndMakeVisible(*field);
        }
        nameLabel.setText("Name", juce::dontSendNotification);
        positionLabel.setText("Position", juce::dontSendNotification);
        status.setFont(juce::FontOptions(12));
        status.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        apply.onClick = [this] { if (time.has_value() && apply.isEnabled() && onApply) { onApply(name.getText().trim(), *time); } };
        for (auto* component : std::initializer_list<juce::Component*>{&nameLabel, &positionLabel, &status, &apply}) { addAndMakeVisible(component); }
        refresh();
    }
    std::function<void(juce::String, double)> onApply;
    void setError(const juce::String& message) { status.setText(message, juce::dontSendNotification); }
    void resized() override {
        auto bounds = getLocalBounds().reduced(12);
        auto row = bounds.removeFromTop(32);
        nameLabel.setBounds(row.removeFromLeft(72)); name.setBounds(row.reduced(0, 3));
        row = bounds.removeFromTop(32);
        positionLabel.setBounds(row.removeFromLeft(72)); position.setBounds(row.reduced(0, 3));
        apply.setBounds(bounds.removeFromBottom(30).removeFromRight(110));
        status.setBounds(bounds);
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
