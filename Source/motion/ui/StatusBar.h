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
        dismiss.setName("Dismiss message");
        dismiss.setTooltip("Dismiss");
        dismiss.onClick = [this] { clear(); };
        addChildComponent(dismiss);
        message.setName("Status message");
        message.setFont(motion::style::small());
        message.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(message);
        stats.setFont(motion::style::small());
        stats.setJustificationType(juce::Justification::centredRight);
        stats.setColour(juce::Label::textColourId, motion::style::subtle());
        addAndMakeVisible(stats);
    }

    void show(const juce::String& text, Kind kind = Kind::error) {
        if (text.isEmpty()) { clear(); return; }
        current = kind;
        message.setText(text, juce::dontSendNotification);
        message.setTooltip(text);
        message.setColour(juce::Label::textColourId, kind == Kind::error ? motion::style::danger() : kind == Kind::warning ? motion::style::warning() : motion::style::muted());
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
            g.setColour((current == Kind::error ? motion::style::danger() : motion::style::warning()).withAlpha(.08f));
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
    class Close final : public juce::Button {
    public:
        Close() : juce::Button("Dismiss") {}
        void paintButton(juce::Graphics& g, bool highlighted, bool) override {
            const auto b = getLocalBounds().toFloat().reduced(4.5f);
            g.setColour(motion::style::muted().withAlpha(highlighted ? 1.0f : .6f));
            g.drawLine(b.getX(), b.getY(), b.getRight(), b.getBottom(), 1.3f);
            g.drawLine(b.getRight(), b.getY(), b.getX(), b.getBottom(), 1.3f);
        }
    };
    void timerCallback() override { clear(); }

    juce::Label message, stats;
    Close dismiss;
    Kind current = Kind::notice;
};
