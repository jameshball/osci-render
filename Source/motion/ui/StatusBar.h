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
        message.setColour(juce::Label::textColourId, kind == Kind::notice ? osci::Colours::textMuted() : osci::Colours::text().withAlpha(.9f));
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

    // A problem is a pill as long as its words: a dot in its colour, the
    // message, and the dismiss button inside it. A notice is just text.
    void paint(juce::Graphics& g) override {
        if (message.getText().isEmpty() || current == Kind::notice) { return; }
        const auto colour = current == Kind::error ? osci::Colours::danger() : osci::Colours::warning();
        g.setColour(colour.withAlpha(.12f));
        g.fillRoundedRectangle(pill.toFloat(), pill.getHeight() * .5f);
        g.setColour(colour);
        g.fillEllipse(juce::Rectangle<float>(6, 6).withCentre({static_cast<float>(pill.getX()) + 10.0f, static_cast<float>(pill.getCentreY())}));
    }
    void resized() override {
        auto area = getLocalBounds().reduced(motion::style::padding, 0);
        stats.setBounds(area.removeFromRight(360));
        area.removeFromRight(motion::style::padding);
        const auto problem = current != Kind::notice;
        const auto text = juce::roundToInt(juce::TextLayout::getStringWidth(motion::style::caption(), message.getText())) + 2;
        const auto lead = problem ? 20 : 0, trail = dismiss.isVisible() ? 22 : 0;
        pill = area.withWidth(std::min(area.getWidth(), lead + text + trail + (problem ? 4 : 0))).withSizeKeepingCentre(std::min(area.getWidth(), lead + text + trail + (problem ? 4 : 0)), 18).withX(area.getX());
        auto inside = pill;
        inside.removeFromLeft(lead);
        if (dismiss.isVisible()) { dismiss.setBounds(inside.removeFromRight(trail).withSizeKeepingCentre(14, 14)); }
        message.setBounds(inside.withY(area.getY()).withHeight(area.getHeight()));
    }

private:
    void timerCallback() override { clear(); }

    juce::Label message, stats;
    juce::Rectangle<int> pill;
    osci::CloseButton dismiss {"Dismiss message"};
    Kind current = Kind::notice;
};
