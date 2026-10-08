#pragma once

#include "Sheet.h"
#include "../model/BlenderSourceSettings.h"

// A live source fed by Blender's add-on: its name, the local port it listens
// on and what it shows when Blender goes away, then the connection's state
// with a switch to listen, and capture to a portable recording.
class MotionBlenderSourcePanel final : public motion::ui::Sheet, private juce::Timer {
public:
    MotionBlenderSourcePanel(juce::String initialName, motion::BlenderSourceSettings settings, bool existing)
        : Sheet(existing ? "Blender source" : "Add Blender source", {}, existing ? "Apply" : "Add & listen"), existing(existing) {
        setName("Blender source settings");
        nameAction(existing ? "Apply settings" : "Add & listen");
        name.setName("Blender source name");
        name.setText(initialName);
        port.setName("Blender port");
        port.setInputRestrictions(5, "0123456789");
        port.setText(juce::String(settings.port));
        port.setJustification(juce::Justification::centredRight);
        policy.setSelected(settings.freezeOnDisconnect ? 0 : 1);
        policy.setTooltip("What the source shows while Blender is disconnected");
        policy.fill = fieldFill();
        for (auto* editor : {&name, &port}) {
            editor->setFont(motion::style::body());
            editor->setColour(juce::TextEditor::textColourId, osci::Colours::text());
            editor->setSelectAllWhenFocused(true);
        }
        for (auto* label : {&nameLabel, &portLabel, &policyLabel, &connectionLabel}) { styleCaption(*label); }
        status.setName("Blender connection status");
        status.setFont(motion::style::body());
        status.setBorderSize({});
        status.setJustificationType(juce::Justification::centredLeft);
        status.setTooltip("Use the same port in Blender's osci-render add-on.");
        primary.onClick = [this] { submit(!this->existing); };
        listen.setButtonText("Start listening");
        listen.onClick = [this] {
            if (isListening && isListening()) {
                if (onStop) { onStop(); error.clear(); }
            } else {
                submit(true);
            }
            timerCallback();
        };
        record.setButtonText("Record capture");
        record.setName("Record Blender capture");
        record.setTitle("Record Blender capture");
        cancelCapture.setButtonText("Discard");
        cancelCapture.setName("Cancel capture");
        cancelCapture.setTitle("Cancel capture");
        record.onClick = [this] { if (onRecord) { error = onRecord().getErrorMessage(); } timerCallback(); };
        cancelCapture.onClick = [this] { if (onCancelCapture) { onCancelCapture(); } error.clear(); timerCallback(); };
        for (auto* component : std::initializer_list<juce::Component*>{&name, &port, &policy, &nameLabel, &portLabel, &policyLabel, &connectionLabel, &status}) { addAndMakeVisible(component); }
        addChildComponent(listen);
        addChildComponent(record);
        addChildComponent(cancelCapture);
        listen.setVisible(existing);
        record.setVisible(existing);
        setSize(widthFor() + 80, heightFor(4));
        startTimerHz(5);
        timerCallback();
    }
    ~MotionBlenderSourcePanel() override { if (onCancelCapture) { onCancelCapture(); } }
    std::function<juce::Result(juce::String, motion::BlenderSourceSettings, bool)> onApply;
    std::function<void()> onStop, onCancelCapture;
    std::function<juce::Result()> onRecord;
    std::function<bool()> isCapturing;
    std::function<bool()> isListening;
    std::function<juce::String()> connectionStatus;

    void paint(juce::Graphics& g) override {
        Sheet::paint(g);
        // The connection's state as a dot: green connected, amber waiting,
        // grey offline, red on a problem.
        const auto text = status.getText();
        const auto colour = error.isNotEmpty() ? motion::style::error()
            : text.startsWith("Connected") ? osci::Colours::accentColor()
            : text.startsWith("Listening") ? motion::style::marker()
            : osci::Colours::textMuted().withAlpha(.5f);
        g.setColour(colour);
        g.fillEllipse(juce::Rectangle<float>(7, 7).withCentre(dot.toFloat()));
    }

protected:
    void layoutBody(juce::Rectangle<int> area) override {
        formRow(area, nameLabel, name, 0);
        formRow(area, portLabel, port, number);
        formRow(area, policyLabel, policy);
        auto line = area.removeFromTop(row);
        connectionLabel.setBounds(line.removeFromLeft(caption));
        if (listen.isVisible()) { listen.setBounds(line.removeFromRight(listen.getBestWidthForHeight(row) + 16).reduced(0, 1)); }
        dot = {line.getX() + 4, line.getCentreY()};
        status.setBounds(line.withTrimmedLeft(14));
        // Capturing sits on the footer's left, apart from the settings.
        auto capture = footerLeft;
        record.setBounds(capture.removeFromLeft(record.getBestWidthForHeight(capture.getHeight()) + 20));
        capture.removeFromLeft(8);
        cancelCapture.setBounds(capture.removeFromLeft(cancelCapture.getBestWidthForHeight(capture.getHeight()) + 20));
    }

private:
    void submit(bool start) {
        const motion::BlenderSourceSettings settings{port.getText().getIntValue(), policy.getSelected() == 0};
        if (!settings.valid() || name.getText().trim().isEmpty()) { error = "Enter a name and a port from 51600 to 51699."; timerCallback(); return; }
        if (onApply) { const auto result = onApply(name.getText(), settings, start); error = result.getErrorMessage(); }
        timerCallback();
    }
    void timerCallback() override {
        const auto state = !existing ? juce::String("Listens once added") : connectionStatus ? connectionStatus() : "Not connected";
        // Only a real state (listening, connected) or a problem is bright.
        const auto live = state.startsWith("Connected") || state.startsWith("Listening");
        status.setColour(juce::Label::textColourId, error.isNotEmpty() ? motion::style::error() : live ? osci::Colours::text() : osci::Colours::textMuted());
        status.setText(error.isNotEmpty() ? error : state, juce::dontSendNotification);
        if (existing) {
            const bool recording = isCapturing && isCapturing();
            const bool listening = isListening && isListening();
            listen.setButtonText(listening ? "Stop listening" : "Start listening");
            record.setButtonText(recording ? "Stop & save capture" : "Record capture");
            record.setEnabled(recording || listening);
            cancelCapture.setVisible(recording);
            for (auto* control : std::initializer_list<juce::Component*>{&name, &port, &policy, &primary, &listen}) { control->setEnabled(!recording); }
            resized();
        }
        repaint();
    }
    bool existing;
    juce::Point<int> dot;
    juce::TextEditor name, port;
    motion::ui::SegmentedControl policy {"Blender disconnect policy", {"Freeze", "Blank"}};
    juce::Label nameLabel{"", "Name"}, portLabel{"", "Port"}, policyLabel{"", "On disconnect"}, connectionLabel{"", "Connection"}, status;
    juce::TextButton listen, record, cancelCapture;
    juce::String error;
};
