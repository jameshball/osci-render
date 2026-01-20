#pragma once

#include <JuceHeader.h>

/**
 * Custom TooltipWindow without drop shadow support to prevent Windows DirectComposition errors.
 * This is a simplified version that always adds tooltips to the desktop (no parent component).
 */
class CustomTooltipWindow : public juce::Component, private juce::Timer {
public:
    explicit CustomTooltipWindow(int millisecondsBeforeTipAppears = 700);
    ~CustomTooltipWindow() override;

    void setMillisecondsBeforeTipAppears(int newTimeMs = 700) noexcept;
    void displayTip(juce::Point<int> screenPosition, const juce::String& text);
    void hideTip();
    virtual juce::String getTipFor(juce::Component& c);

    enum ColourIds {
        backgroundColourId = 0x1001b00,
        textColourId = 0x1001c00,
        outlineColourId = 0x1001c10
    };

    float getDesktopScaleFactor() const override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    juce::Point<float> lastMousePos;
    juce::Component::SafePointer<juce::Component> lastComponentUnderMouse;
    juce::String tipShowing, lastTipUnderMouse, manuallyShownTip;
    int millisecondsBeforeTipAppears;
    juce::uint32 lastCompChangeTime = 0, lastHideTime = 0;
    bool reentrant = false, dismissalMouseEventOccurred = false;

    enum ShownManually { yes, no };
    void displayTipInternal(juce::Point<int>, const juce::String&, ShownManually);

    void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void timerCallback() override;
    void updatePosition(const juce::String&, juce::Point<int>, juce::Rectangle<int>);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomTooltipWindow)
};
