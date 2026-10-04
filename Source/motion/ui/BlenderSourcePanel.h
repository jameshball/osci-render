#pragma once

#include <JuceHeader.h>
#include "MotionStyle.h"
#include <osci_gui/osci_gui.h>
#include "../live/BlenderSourceSettings.h"

class MotionBlenderSourcePanel final : public juce::Component, private juce::Timer {
public:
    MotionBlenderSourcePanel(juce::String initialName, motion::BlenderSourceSettings settings, bool existing) : existing(existing) {
        setName("Blender source settings");
        name.setName("Blender source name"); name.setText(initialName);
        port.setName("Blender port"); port.setInputRestrictions(5, "0123456789"); port.setText(juce::String(settings.port));
        policy.setName("Blender disconnect policy");
        policy.addItem("Freeze last frame", 1); policy.addItem("Blank output", 2);
        policy.setSelectedId(settings.freezeOnDisconnect ? 1 : 2, juce::dontSendNotification);
        for (auto* editor : {&name, &port}) {
            editor->setFont(motion::style::body());
            editor->setColour(juce::TextEditor::backgroundColourId, osci::Colours::veryDark());
            editor->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            editor->setColour(juce::TextEditor::textColourId, osci::Colours::text());
        }
        for (auto* label : {&nameLabel, &portLabel, &policyLabel, &status, &note}) {
            label->setFont(motion::style::body()); label->setBorderSize({});
            label->setColour(juce::Label::textColourId, osci::Colours::textMuted());
        }
        status.setName("Blender connection status"); status.setJustificationType(juce::Justification::centredLeft);
        note.setFont(motion::style::body()); note.setJustificationType(juce::Justification::topLeft);
        note.setText("Use the same port in Blender's osci-render add-on.", juce::dontSendNotification);
        save.setButtonText(existing ? "Apply settings" : "Add source");
        save.onClick = [this] { submit(false); };
        listen.setButtonText(existing ? "Start listening" : "Add & listen");
        listen.onClick = [this] {
            if (this->existing && isListening && isListening()) { if (onStop) { onStop(); error.clear(); } }
            else { submit(true); }
            timerCallback();
        };
        for (auto* component : std::initializer_list<juce::Component*>{&name, &port, &policy, &nameLabel, &portLabel, &policyLabel, &status, &note, &save, &listen}) { addAndMakeVisible(component); }
        record.setButtonText("Record capture"); record.setName("Record Blender capture");
        cancel.setButtonText("Cancel capture");
        record.onClick = [this] { if (onRecord) { error = onRecord().getErrorMessage(); } timerCallback(); };
        cancel.onClick = [this] { if (onCancelCapture) { onCancelCapture(); } error.clear(); timerCallback(); };
        addAndMakeVisible(record); addAndMakeVisible(cancel);
        record.setVisible(existing); cancel.setVisible(false);
        setSize(460, existing ? 254 : 222);
        startTimerHz(5);
    }
    std::function<juce::Result(juce::String, motion::BlenderSourceSettings, bool)> onApply;
    ~MotionBlenderSourcePanel() override { if (onCancelCapture) { onCancelCapture(); } }
    std::function<void()> onStop, onCancelCapture;
    std::function<juce::Result()> onRecord;
    std::function<bool()> isCapturing;
    std::function<bool()> isListening;
    std::function<juce::String()> connectionStatus;
    void resized() override {
        auto area = getLocalBounds().reduced(motion::style::dialog::margin);
        for (auto pair : {std::pair<juce::Label*, juce::Component*>{&nameLabel, &name}, {&portLabel, &port}, {&policyLabel, &policy}}) {
            motion::style::dialog::formRow(area, *pair.first, *pair.second, 116);
        }
        motion::style::dialog::footer(area, {&listen, &save}, 128);
        if (existing) {
            // Capturing sits on its own line, apart from the connection buttons.
            auto capture = area.removeFromBottom(motion::style::dialog::buttonHeight);
            area.removeFromBottom(motion::style::dialog::rowGap);
            record.setBounds(capture.removeFromLeft(150)); capture.removeFromLeft(8);
            cancel.setBounds(capture.removeFromLeft(128));
        }
        status.setBounds(area.removeFromTop(20)); area.removeFromTop(4);
        note.setBounds(area);
    }
private:
    void submit(bool start) {
        const motion::BlenderSourceSettings settings{port.getText().getIntValue(), policy.getSelectedId() == 1};
        if (!settings.valid() || name.getText().trim().isEmpty()) { error = "Enter a name and a port from 51600 to 51699."; timerCallback(); return; }
        if (onApply) { const auto result = onApply(name.getText(), settings, start); error = result.getErrorMessage(); }
        timerCallback();
    }
    void timerCallback() override {
        status.setColour(juce::Label::textColourId, error.isNotEmpty() ? juce::Colours::orange : osci::Colours::textMuted());
        status.setText(error.isNotEmpty() ? error : connectionStatus ? connectionStatus() : "Not connected", juce::dontSendNotification);
        if (existing) {
            const bool recording = isCapturing && isCapturing();
            listen.setButtonText(isListening && isListening() ? "Stop listening" : "Start listening");
            record.setButtonText(recording ? "Stop & save capture" : "Record capture");
            record.setEnabled(recording || (isListening && isListening()));
            cancel.setVisible(recording);
            for (auto* control : std::initializer_list<juce::Component*>{&name, &port, &policy, &save, &listen}) { control->setEnabled(!recording); }
            note.setColour(juce::Label::textColourId, recording ? osci::Colours::text() : osci::Colours::textMuted());
            note.setText(recording ? "Closing this panel cancels the capture." : "Use the same port in Blender's osci-render add-on.", juce::dontSendNotification);
        }
    }
    bool existing;
    juce::TextEditor name, port;
    juce::ComboBox policy;
    juce::Label nameLabel{"", "Name"}, portLabel{"", "Local port"}, policyLabel{"", "On disconnect"}, status, note;
    juce::TextButton save, listen, record, cancel;
    juce::String error;
};
