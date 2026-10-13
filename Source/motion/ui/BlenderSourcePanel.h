#pragma once

#include "Sheet.h"
#include "../model/BlenderSourceSettings.h"
#include "../live/BlenderCapture.h"

// Record, with the red dot every recorder uses before its name.
class MotionRecordButton final : public juce::TextButton {
public:
    static constexpr int inset = 10, dot = 8, gap = 6;
    int bestWidth() { return inset + dot + gap + juce::roundToInt(std::ceil(juce::TextLayout::getStringWidth(motion::style::buttonFont(*this), getButtonText()))) + inset; }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        getLookAndFeel().drawButtonBackground(g, *this, findColour(buttonColourId), highlighted, down);
        auto area = getLocalBounds().reduced(inset, 0);
        const auto alpha = isEnabled() ? 1.0f : .4f;
        g.setColour(motion::style::error().withMultipliedAlpha(alpha));
        g.fillEllipse(area.removeFromLeft(dot).withSizeKeepingCentre(dot, dot).toFloat());
        area.removeFromLeft(gap);
        g.setColour(osci::Colours::text().withMultipliedAlpha(isEnabled() ? 1.0f : .5f));
        g.setFont(motion::style::buttonFont(*this));
        g.drawText(getButtonText(), area, juce::Justification::centredLeft, false);
    }
};

// A live source fed by Blender's add-on: its name, the local port it listens
// on and what it shows when Blender goes away, then the connection's state
// with a switch to listen, and capture to a portable recording. Each state
// sits on the left of its row and what changes it on the right, so the
// footer holds only Cancel and the sheet's own action.
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
        for (auto* label : {&nameLabel, &portLabel, &policyLabel, &connectionLabel, &captureLabel}) { styleCaption(*label); }
        status.setName("Blender connection status");
        captureState.setName("Blender capture state");
        for (auto* label : {&status, &captureState}) {
            label->setFont(motion::style::body());
            label->setBorderSize({});
            label->setJustificationType(juce::Justification::centredLeft);
        }
        captureState.setTooltip("Records what Blender sends, up to " + juce::String(juce::roundToInt(motion::BlenderCapture::maximumSeconds / 60)) + " minutes, as a new source.");
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
        record.setButtonText("Record");
        record.setName("Record Blender capture");
        record.setTitle("Record Blender capture");
        saveCapture.setButtonText("Save");
        saveCapture.setName("Save Blender capture");
        saveCapture.setTitle("Save Blender capture");
        saveCapture.setTooltip("Stop recording and add the capture as a source");
        cancelCapture.setButtonText("Discard");
        cancelCapture.setName("Cancel capture");
        cancelCapture.setTitle("Cancel capture");
        motion::style::makeQuiet(cancelCapture, true);
        record.onClick = [this] { capture(); };
        saveCapture.onClick = [this] { capture(); };
        cancelCapture.onClick = [this] { if (onCancelCapture) { onCancelCapture(); } error.clear(); timerCallback(); };
        for (auto* component : std::initializer_list<juce::Component*>{&name, &port, &policy, &nameLabel, &portLabel, &policyLabel, &connectionLabel, &status}) { addAndMakeVisible(component); }
        for (auto* component : std::initializer_list<juce::Component*>{&listen, &captureLabel, &captureState, &record, &saveCapture, &cancelCapture}) { addChildComponent(component); }
        for (auto* component : std::initializer_list<juce::Component*>{&listen, &captureLabel, &captureState, &record}) { component->setVisible(existing); }
        setSize(widthFor() + 128, heightFor(existing ? 5 : 4));
        startTimerHz(4);
        timerCallback();
    }
    ~MotionBlenderSourcePanel() override { if (onCancelCapture) { onCancelCapture(); } }
    std::function<juce::Result(juce::String, motion::BlenderSourceSettings, bool)> onApply;
    std::function<void()> onStop, onCancelCapture;
    std::function<juce::Result()> onRecord;
    std::function<bool()> isCapturing;
    std::function<bool()> isListening;
    std::function<juce::String()> connectionStatus;
    // Why the running capture stopped taking frames (too long, too big).
    std::function<juce::String()> captureError;

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
        // Recording: the recorder's red dot, dimming every other half second.
        if (recordingSince > 0) {
            const auto phase = std::fmod((juce::Time::getMillisecondCounterHiRes() - recordingSince) / 1000.0, 1.0);
            g.setColour(motion::style::error().withAlpha(phase < .5 ? 1.0f : .35f));
            g.fillEllipse(juce::Rectangle<float>(7, 7).withCentre(recordingDot.toFloat()));
        }
    }

protected:
    void layoutBody(juce::Rectangle<int> area) override {
        formRow(area, nameLabel, name, 0);
        formRow(area, portLabel, port, number);
        formRow(area, policyLabel, policy);
        auto line = area.removeFromTop(row);
        connectionLabel.setBounds(line.removeFromLeft(caption));
        if (listen.isVisible()) {
            listen.setBounds(line.removeFromRight(listen.getBestWidthForHeight(row) + 16).reduced(0, 1));
            line.removeFromRight(8);
        }
        dot = {line.getX() + 4, line.getCentreY()};
        status.setBounds(line.withTrimmedLeft(14));
        if (!existing) { return; }
        area.removeFromTop(rowGap);
        line = area.removeFromTop(row);
        captureLabel.setBounds(line.removeFromLeft(caption));
        if (recordingSince > 0) {
            saveCapture.setBounds(line.removeFromRight(saveCapture.getBestWidthForHeight(row) + 16).reduced(0, 1));
            line.removeFromRight(4);
            cancelCapture.setBounds(line.removeFromRight(cancelCapture.getBestWidthForHeight(row) + 12).reduced(0, 1));
            recordingDot = {line.getX() + 4, line.getCentreY()};
            captureState.setBounds(line.withTrimmedLeft(14));
        } else {
            record.setBounds(line.removeFromRight(record.bestWidth()).reduced(0, 1));
            captureState.setBounds(line);
        }
    }

private:
    void submit(bool start) {
        const motion::BlenderSourceSettings settings{port.getText().getIntValue(), policy.getSelected() == 0};
        if (!settings.valid() || name.getText().trim().isEmpty()) { error = "Enter a name and a port from 51600 to 51699."; timerCallback(); return; }
        if (onApply) { const auto result = onApply(name.getText(), settings, start); error = result.getErrorMessage(); }
        timerCallback();
    }
    void capture() {
        if (onRecord) { error = onRecord().getErrorMessage(); }
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
            if (recording != (recordingSince > 0)) { recordingSince = recording ? juce::Time::getMillisecondCounterHiRes() : 0.0; }
            const auto seconds = recording ? static_cast<int>((juce::Time::getMillisecondCounterHiRes() - recordingSince) / 1000.0) : 0;
            const auto problem = recording && captureError ? captureError() : juce::String();
            captureState.setText(problem.isNotEmpty() ? problem
                : recording ? "Recording" + motion::style::dot() + juce::String(seconds / 60) + ":" + juce::String(seconds % 60).paddedLeft('0', 2)
                : listening ? "Not recording" : "Listen to record", juce::dontSendNotification);
            captureState.setColour(juce::Label::textColourId, problem.isNotEmpty() ? motion::style::error() : recording ? osci::Colours::text() : osci::Colours::textMuted());
            record.setVisible(!recording);
            record.setEnabled(listening);
            saveCapture.setVisible(recording);
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
    juce::Label nameLabel{"", "Name"}, portLabel{"", "Port"}, policyLabel{"", "On disconnect"}, connectionLabel{"", "Connection"}, captureLabel{"", "Capture"}, status, captureState;
    juce::TextButton listen, saveCapture, cancelCapture;
    MotionRecordButton record;
    juce::Point<int> recordingDot;
    // When the capture began (the hi-res millisecond counter), or 0.
    double recordingSince = 0;
    juce::String error;
};
