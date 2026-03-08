#pragma once

#include <JuceHeader.h>
#include "ScrollFadeOverlay.h"

// Viewport with built-in scroll fade overlay handling.
// Automatically updates fade visibility based on scrollbar position.
class ScrollFadeViewport : public juce::Viewport {
public:
    ScrollFadeViewport() {
        // Ensure vertical scrolling is active by default
        setScrollBarsShown(true, false);

        addAndMakeVisible(overlay);
        overlay.setInterceptsMouseClicks(false, false);
        overlay.setAlwaysOnTop(true);
        overlay.setSidesEnabled(enableTop, enableBottom, enableLeft, enableRight);
        overlay.setFadeHeight(fadeHeight);
        overlay.setFadeWidth(fadeWidth);

        scrollListener.owner = this;
        getVerticalScrollBar().addListener(&scrollListener);
        getHorizontalScrollBar().addListener(&scrollListener);

        // Initialise overlay visibility based on current scrollbar state
        updateOverlay();
    }

    void setViewedComponent(juce::Component* newViewedComponent, bool deleteComponentWhenNoLongerNeeded) {
        juce::Viewport::setViewedComponent(newViewedComponent, deleteComponentWhenNoLongerNeeded);
        layoutOverlay();
    }

    ~ScrollFadeViewport() override {
        getVerticalScrollBar().removeListener(&scrollListener);
        getHorizontalScrollBar().removeListener(&scrollListener);
    }

    void resized() override {
        juce::Viewport::resized();
        layoutOverlay();
    }

    void visibleAreaChanged(const juce::Rectangle<int>&) override {
        // Called when scroll position changes or content size affects visible area
        updateOverlay();
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        auto adjustedWheel = wheel;

        if (getHorizontalScrollBar().isVisible()) {
            const auto horizontalDelta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;

            if (std::abs(horizontalDelta) > 0.0f) {
                adjustedWheel.deltaX = horizontalDelta;
                adjustedWheel.deltaY = 0.0f;
                getHorizontalScrollBar().mouseWheelMove(e, adjustedWheel);
                return;
            }
        }

        juce::Viewport::mouseWheelMove(e, wheel);
    }

    void childBoundsChanged(juce::Component* child) override {
        juce::Viewport::childBoundsChanged(child);
        // Content resized; ensure overlay covers and visibility is recomputed
        layoutOverlay();
    }

    // Configure which sides to render
    void setSidesEnabled(bool top, bool bottom) {
        setSidesEnabled(top, bottom, false, false);
    }

    void setSidesEnabled(bool top, bool bottom, bool left, bool right) {
        enableTop = top;
        enableBottom = bottom;
        enableLeft = left;
        enableRight = right;
        overlay.setSidesEnabled(top, bottom, left, right);
        updateOverlay();
    }

    void setFadeHeight(int height) {
        fadeHeight = juce::jmax(4, height);
        overlay.setFadeHeight(fadeHeight);
        layoutOverlay();
    }

    void setFadeWidth(int width) {
        fadeWidth = juce::jmax(4, width);
        overlay.setFadeWidth(fadeWidth);
        layoutOverlay();
    }

    void setFadeVisible(bool shouldBeVisible) { fadesEnabled = shouldBeVisible; updateOverlay(); }

private:
    ScrollFadeOverlay overlay;
    bool enableTop { true };
    bool enableBottom { true };
    bool enableLeft { false };
    bool enableRight { false };
    bool fadesEnabled { true };
    int fadeHeight { 48 };
    int fadeWidth { 48 };

    void layoutOverlay() {
        // Cover the viewport's content display area.
        overlay.layoutOver(getLocalBounds());
        overlay.toFront(false);
        updateOverlay();
    }

    void updateOverlay() {
        overlay.updateVisibilityFromViewport(this, fadesEnabled);
        overlay.repaint();
    }

    struct ScrollListener : juce::ScrollBar::Listener {
        ScrollFadeViewport* owner { nullptr };
        void scrollBarMoved(juce::ScrollBar*, double) override {
            if (owner) owner->updateOverlay();
        }
    } scrollListener;
};
