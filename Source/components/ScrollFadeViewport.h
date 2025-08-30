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
    overlay.setSidesEnabled(enableTop, enableBottom);
    overlay.setFadeHeight(fadeHeight);

        vScrollListener.owner = this;
        getVerticalScrollBar().addListener(&vScrollListener);

    // Initialise overlay visibility based on current scrollbar state
    updateOverlay();
    }
    void setViewedComponent(juce::Component* newViewedComponent, bool deleteComponentWhenNoLongerNeeded) {
        juce::Viewport::setViewedComponent(newViewedComponent, deleteComponentWhenNoLongerNeeded);
        layoutOverlay();
    }

    ~ScrollFadeViewport() override {
        getVerticalScrollBar().removeListener(&vScrollListener);
    }

    void resized() override {
        juce::Viewport::resized();
        layoutOverlay();
    }

    void visibleAreaChanged(const juce::Rectangle<int>&) override {
        // Called when scroll position changes or content size affects visible area
        updateOverlay();
    }

    void childBoundsChanged(juce::Component* child) override {
        juce::Viewport::childBoundsChanged(child);
        // Content resized; ensure overlay covers and visibility is recomputed
        layoutOverlay();
    }

    // Configure which sides to render
    void setSidesEnabled(bool top, bool bottom) {
        enableTop = top; enableBottom = bottom;
        overlay.setSidesEnabled(top, bottom);
        updateOverlay();
    }

    void setFadeHeight(int height) {
        fadeHeight = juce::jmax(4, height);
        overlay.setFadeHeight(fadeHeight);
        layoutOverlay();
    }

    void setFadeVisible(bool shouldBeVisible) { fadesEnabled = shouldBeVisible; updateOverlay(); }

private:
    ScrollFadeOverlay overlay;
    bool enableTop { true };
    bool enableBottom { true };
    bool fadesEnabled { true };
    int fadeHeight { 48 };

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

    struct VSBListener : juce::ScrollBar::Listener {
        ScrollFadeViewport* owner { nullptr };
        void scrollBarMoved(juce::ScrollBar*, double) override {
            if (owner) owner->updateOverlay();
        }
    } vScrollListener;
};
