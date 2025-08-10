#pragma once

#include <JuceHeader.h>
#include "VListBox.h"
#include "../LookAndFeel.h"

// Overlay component that can render top and/or bottom scroll fades with adaptive strength.
class ScrollFadeOverlay : public juce::Component {
public:
    ScrollFadeOverlay() {
        setInterceptsMouseClicks(false, false);
        setOpaque(false);
    }

    void setFadeHeight(int hTopAndBottom) {
        fadeHeightTop = fadeHeightBottom = juce::jmax(4, hTopAndBottom);
    }

    void setFadeHeights(int topH, int bottomH) {
        fadeHeightTop = juce::jmax(4, topH);
        fadeHeightBottom = juce::jmax(4, bottomH);
    }

    void setSidesEnabled(bool top, bool bottom) {
        enableTop = top;
        enableBottom = bottom;
    }

    // Position the overlay to fully cover the scrollable viewport area (owner coordinates)
    void layoutOver(const juce::Rectangle<int>& listBounds) {
        setBounds(listBounds);
    }

    // Toggle per-side visibility/strength based on viewport scroll and enable flag.
    void updateVisibilityFromViewport(juce::Viewport* vp, bool enabled) {
        showTop = showBottom = false;
        strengthTop = strengthBottom = 0.0f;

        if (enabled && vp != nullptr && vp->getVerticalScrollBar().isVisible()) {
            auto& sb = vp->getVerticalScrollBar();
            const double start = sb.getCurrentRangeStart();
            const double size = sb.getCurrentRangeSize();
            const double max = sb.getMaximumRangeLimit();

            // Top fade strength scales with how far from top we are
            const bool atTop = start <= 0.5;
            const double topDist = start; // pixels scrolled down
            strengthTop = (float) juce::jlimit(0.0, 1.0, topDist / (double) juce::jmax(1, fadeHeightTop));
            showTop = enableTop && !atTop && strengthTop > 0.01f;

            // Bottom fade strength scales with how far from bottom we are
            const double remaining = (max - (start + size));
            const bool atBottom = remaining <= 0.5;
            strengthBottom = (float) juce::jlimit(0.0, 1.0, remaining / (double) juce::jmax(1, fadeHeightBottom));
            showBottom = enableBottom && !atBottom && strengthBottom > 0.01f;
        }

        const bool anyVisible = (showTop || showBottom);
        setVisible(anyVisible);
        if (anyVisible)
            repaint();
    }

    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds();
        const auto bg = findColour(groupComponentBackgroundColourId);

        if (showTop && fadeHeightTop > 0) {
            const int h = juce::jmin(fadeHeightTop, area.getHeight());
            auto topRect = area.removeFromTop(h);
            juce::ColourGradient gradTop(bg.withAlpha(strengthTop),
                                         (float) topRect.getX(), (float) topRect.getY(),
                                         bg.withAlpha(0.0f),
                                         (float) topRect.getX(), (float) topRect.getBottom(),
                                         false);
            g.setGradientFill(gradTop);
            g.fillRect(topRect);
        }

        // Reset area for bottom drawing
        area = getLocalBounds();
        if (showBottom && fadeHeightBottom > 0) {
            const int h = juce::jmin(fadeHeightBottom, area.getHeight());
            auto bottomRect = area.removeFromBottom(h);
            juce::ColourGradient gradBottom(bg.withAlpha(strengthBottom),
                                            (float) bottomRect.getX(), (float) bottomRect.getBottom(),
                                            bg.withAlpha(0.0f),
                                            (float) bottomRect.getX(), (float) bottomRect.getY(),
                                            false);
            g.setGradientFill(gradBottom);
            g.fillRect(bottomRect);
        }
    }

private:
    int fadeHeightTop { 48 };
    int fadeHeightBottom { 48 };
    bool enableTop { true };
    bool enableBottom { true };
    bool showTop { false };
    bool showBottom { false };
    float strengthTop { 0.0f };
    float strengthBottom { 0.0f };
};

// Mixin to attach a bottom fade overlay for any scrollable area (ListBox or Viewport).
class ScrollFadeMixin {
public:
    virtual ~ScrollFadeMixin() {
        detachScrollListeners();
    }

protected:
    void initScrollFade(juce::Component& owner) {
        if (! scrollFade)
            scrollFade = std::make_unique<ScrollFadeOverlay>();
        if (scrollFade->getParentComponent() != &owner)
            owner.addAndMakeVisible(*scrollFade);

        scrollListener.owner = this;
    }

    void attachToListBox(VListBox& list) {
        detachScrollListeners();
        scrollViewport = list.getViewport();
        attachScrollListeners();
    }

    void attachToViewport(juce::Viewport& vp) {
        detachScrollListeners();
        scrollViewport = &vp;
        attachScrollListeners();
    }

    // Call from owner's resized(). listBounds must be in the owner's coordinate space.
    void layoutScrollFade(const juce::Rectangle<int>& listBounds, bool enabled = true, int fadeHeight = 48) {
        if (! scrollFade)
            return;
        lastListBounds = listBounds;
        lastEnabled = enabled;
        lastFadeHeight = fadeHeight;
        scrollFade->setFadeHeight(fadeHeight);
        scrollFade->layoutOver(listBounds);
        scrollFade->toFront(false);
        scrollFade->updateVisibilityFromViewport(getViewport(), enabled);
    }

    // Explicitly hide/show (e.g., when switching views)
    void setScrollFadeVisible(bool shouldBeVisible) {
        lastEnabled = shouldBeVisible;
        if (scrollFade)
            scrollFade->setVisible(shouldBeVisible);
    }

    // Allow configuring which sides to render (default is both true)
    void setScrollFadeSides(bool enableTop, bool enableBottom) {
        if (scrollFade) {
            scrollFade->setSidesEnabled(enableTop, enableBottom);
            // Recompute since sides changed
            scrollFade->updateVisibilityFromViewport(getViewport(), lastEnabled);
        }
    }

protected:
    std::unique_ptr<ScrollFadeOverlay> scrollFade;
    juce::Component::SafePointer<juce::Viewport> scrollViewport;
    juce::Viewport* getViewport() const noexcept { return static_cast<juce::Viewport*>(scrollViewport.getComponent()); }

private:
    // Listen to vertical scrollbar to update fade visibility while scrolling
    struct VScrollListener : juce::ScrollBar::Listener {
        ScrollFadeMixin* owner { nullptr };
        void scrollBarMoved(juce::ScrollBar*, double) override {
            if (owner && owner->scrollFade) {
                // Recompute visibility using last-known enabled state
                owner->scrollFade->updateVisibilityFromViewport(owner->getViewport(), owner->lastEnabled);
            }
        }
    } scrollListener;

    void attachScrollListeners() {
    if (auto* vp = getViewport()) {
            vp->getVerticalScrollBar().addListener(&scrollListener);
        }
    }

    void detachScrollListeners() {
    if (auto* vp = getViewport()) {
            vp->getVerticalScrollBar().removeListener(&scrollListener);
        }
    }

    juce::Rectangle<int> lastListBounds;
    bool lastEnabled { true };
    int lastFadeHeight { 48 };
};
