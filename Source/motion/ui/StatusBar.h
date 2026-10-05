#pragma once

#include "MotionStyle.h"

// One quiet line at the foot of the window: the latest message on the left
// (errors stay until dismissed, notices fade after a few seconds) and live
// beam statistics on the right.
class MotionStatusBar final : public juce::Component, private juce::Timer {
public:
    enum class Kind { notice, warning, error };

    MotionStatusBar() {
        setName("Status bar");
        dismiss.setIconPadding(2);
        dismiss.setTooltip("Dismiss");
        dismiss.onClick = [this] { clear(); };
        addChildComponent(dismiss);
        message.setName("Status message");
        message.setFont(motion::style::caption());
        message.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(message);
        stats.setFont(motion::style::caption());
        stats.setJustificationType(juce::Justification::centredRight);
        stats.setColour(juce::Label::textColourId, osci::Colours::textSubtle());
        addAndMakeVisible(stats);
    }

    void show(const juce::String& text, Kind kind = Kind::error) {
        if (text.isEmpty()) { clear(); return; }
        current = kind;
        message.setText(text, juce::dontSendNotification);
        message.setTooltip(text);
        message.setColour(juce::Label::textColourId, kind == Kind::error ? osci::Colours::danger() : kind == Kind::warning ? osci::Colours::warning() : osci::Colours::textMuted());
        dismiss.setVisible(true);
        if (kind == Kind::notice) { startTimer(6000); } else { stopTimer(); }
        resized();
        repaint();
    }
    void clear() {
        stopTimer();
        message.setText({}, juce::dontSendNotification);
        dismiss.setVisible(false);
        repaint();
    }
    juce::String text() const { return message.getText(); }
    void setStatistics(const juce::String& text) {
        if (stats.getText() != text) { stats.setText(text, juce::dontSendNotification); }
    }

    void paint(juce::Graphics& g) override {
        if (message.getText().isNotEmpty() && current != Kind::notice) {
            g.setColour((current == Kind::error ? osci::Colours::danger() : osci::Colours::warning()).withAlpha(.08f));
            g.fillRoundedRectangle(message.getBounds().expanded(4, 0).toFloat(), motion::style::radius);
        }
    }
    void resized() override {
        auto area = getLocalBounds().reduced(motion::style::padding, 0);
        stats.setBounds(area.removeFromRight(360));
        if (dismiss.isVisible()) { dismiss.setBounds(area.removeFromLeft(16).withSizeKeepingCentre(16, 16)); area.removeFromLeft(4); }
        message.setBounds(area);
    }

private:
    void timerCallback() override { clear(); }

    juce::Label message, stats;
    osci::CloseButton dismiss {"Dismiss message"};
    Kind current = Kind::notice;
};
