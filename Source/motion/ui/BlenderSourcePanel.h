#pragma once

#include <JuceHeader.h>
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
            editor->setFont(juce::FontOptions(13));
            editor->setColour(juce::TextEditor::backgroundColourId, osci::Colours::veryDark());
            editor->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            editor->setColour(juce::TextEditor::textColourId, osci::Colours::text());
        }
        for (auto* label : {&nameLabel, &portLabel, &policyLabel, &status, &note}) {
            label->setFont(juce::FontOptions(13)); label->setBorderSize({});
            label->setColour(juce::Label::textColourId, osci::Colours::textMuted());
        }
        status.setName("Blender connection status"); status.setJustificationType(juce::Justification::centredLeft);
        note.setFont(juce::FontOptions(12)); note.setJustificationType(juce::Justification::topLeft);
        note.setText("Use the same port in Blender's osci-render add-on.\nReceives camera-framed Grease Pencil line art.\nCapture creates a portable source for editing and export.", juce::dontSendNotification);
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
        setSize(460, existing ? 340 : 292);
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
        auto area = getLocalBounds().reduced(16);
        for (auto pair : {std::pair<juce::Component*, juce::Component*>{&nameLabel, &name}, {&portLabel, &port}, {&policyLabel, &policy}}) {
            auto row = area.removeFromTop(28); pair.first->setBounds(row.removeFromLeft(116)); pair.second->setBounds(row); area.removeFromTop(8);
        }
        status.setBounds(area.removeFromTop(32)); area.removeFromTop(6);
        note.setBounds(area.removeFromTop(58)); area.removeFromTop(10);
        if (existing) {
            auto captureRow = area.removeFromBottom(30);
            cancel.setBounds(captureRow.removeFromRight(150)); captureRow.removeFromRight(8);
            record.setBounds(captureRow.removeFromRight(150)); area.removeFromBottom(8);
        }
        auto buttons = area.removeFromBottom(30);
        listen.setBounds(buttons.removeFromRight(150)); buttons.removeFromRight(8); save.setBounds(buttons.removeFromRight(150));
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
            note.setText(recording ? "Closing this panel cancels the capture.\nRecording source geometry before effects.\nUp to 10 minutes, 20,000 updates and 1 million segments."
                : "Use the same port in Blender's osci-render add-on.\nReceives camera-framed Grease Pencil line art.\nCapture creates a portable source for editing and export.", juce::dontSendNotification);
        }
    }
    bool existing;
    juce::TextEditor name, port;
    juce::ComboBox policy;
    juce::Label nameLabel{"", "Name"}, portLabel{"", "Local port"}, policyLabel{"", "On disconnect"}, status, note;
    juce::TextButton save, listen, record, cancel;
    juce::String error;
};
