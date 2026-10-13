#pragma once

#include "MotionStyle.h"

// One quiet line at the foot of the window: the latest message on the left
// (errors stay until dismissed, notices fade after a few seconds), with a
// button to act on it when there is something to do, and live beam
// statistics on the right.
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
        stats.setMinimumHorizontalScale(1.0f);
        stats.setColour(juce::Label::textColourId, osci::Colours::textSubtle());
        addAndMakeVisible(stats);
        action.setName("Status action");
        action.getProperties().set(motion::style::captionButton, true);
        action.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        action.setColour(juce::TextButton::textColourOffId, motion::style::accent());
        action.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        action.onClick = [this] {
            auto act = std::move(onAction);
            clear();
            if (act) { act(); }
        };
        addChildComponent(action);
    }

    // `actionLabel` and `act`, when given: a button after the message that
    // does what it suggests, and clears it.
    void show(const juce::String& text, Kind kind = Kind::error, const juce::String& actionLabel = {}, std::function<void()> act = {}) {
        if (text.isEmpty()) { clear(); return; }
        current = kind;
        onAction = std::move(act);
        action.setButtonText(actionLabel);
        action.setTitle(actionLabel);
        action.setVisible(actionLabel.isNotEmpty() && onAction != nullptr);
        message.setText(text, juce::dontSendNotification);
        message.setTooltip(text);
        message.setColour(juce::Label::textColourId, kind == Kind::notice ? osci::Colours::textMuted() : osci::Colours::text().withAlpha(.9f));
        dismiss.setVisible(true);
        // A notice with something to do waits longer for it.
        if (kind == Kind::notice) { startTimer(action.isVisible() ? 15000 : 6000); } else { stopTimer(); }
        resized();
        repaint();
    }
    void clear() {
        stopTimer();
        message.setText({}, juce::dontSendNotification);
        dismiss.setVisible(false);
        action.setVisible(false);
        onAction = nullptr;
        repaint();
    }
    juce::String text() const { return message.getText(); }
    // Shown whole while it fits in half the bar; its tooltip always has it whole.
    void setStatistics(const juce::String& text) {
        if (stats.getText() == text) { return; }
        stats.setText(text, juce::dontSendNotification);
        stats.setTooltip(text);
        resized();
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
        const auto statsWidth = juce::roundToInt(juce::TextLayout::getStringWidth(stats.getFont(), stats.getText())) + 8;
        stats.setBounds(area.removeFromRight(std::min(statsWidth, area.getWidth() / 2)));
        area.removeFromRight(motion::style::padding);
        // The action keeps its whole label; the message gives way to it.
        constexpr int actionHeight = 20;
        const auto actionWidth = action.isVisible() ? std::min(area.getWidth(), juce::roundToInt(juce::TextLayout::getStringWidth(motion::style::buttonFont(action), action.getButtonText())) + 16) : 0;
        const auto problem = current != Kind::notice;
        // A problem's pill holds its close button; a notice with an action
        // ends in it, after the action, so the action follows the words.
        const auto closeAtEnd = !problem && action.isVisible() && dismiss.isVisible();
        const auto messageArea = area.withTrimmedRight((actionWidth > 0 ? actionWidth + motion::style::gap : 0) + (closeAtEnd ? 26 : 0));
        const auto text = juce::roundToInt(juce::TextLayout::getStringWidth(motion::style::caption(), message.getText())) + 2;
        const auto lead = problem ? 20 : 0, trail = dismiss.isVisible() && !closeAtEnd ? 22 : 0;
        const auto pillWidth = std::max(0, std::min(messageArea.getWidth(), lead + text + trail + (problem ? 4 : 0)));
        pill = messageArea.withWidth(pillWidth).withSizeKeepingCentre(pillWidth, 18).withX(messageArea.getX());
        auto inside = pill;
        inside.removeFromLeft(lead);
        if (dismiss.isVisible() && !closeAtEnd) { dismiss.setBounds(inside.removeFromRight(trail).withSizeKeepingCentre(14, 14)); }
        message.setBounds(inside.withY(area.getY()).withHeight(area.getHeight()));
        if (action.isVisible()) { action.setBounds(pill.getRight() + motion::style::gap, area.getY() + (area.getHeight() - actionHeight) / 2, actionWidth, actionHeight); }
        if (closeAtEnd) { dismiss.setBounds(juce::Rectangle<int>(action.getRight() + 4, area.getY(), 22, area.getHeight()).withSizeKeepingCentre(14, 14)); }
    }

private:
    void timerCallback() override { clear(); }

    juce::Label message, stats;
    juce::TextButton action;
    std::function<void()> onAction;
    juce::Rectangle<int> pill;
    osci::CloseButton dismiss {"Dismiss message"};
    Kind current = Kind::notice;
};
