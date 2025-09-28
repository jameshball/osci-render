#pragma once

#include <JuceHeader.h>
#include "../LookAndFeel.h"

// Standalone overlay component for drawing gradient fades at the top/bottom of a scroll area.
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

    void layoutOver(const juce::Rectangle<int>& listBounds) { setBounds(listBounds); }

    void updateVisibilityFromViewport(juce::Viewport* vp, bool enabled) {
        showTop = showBottom = false;
        strengthTop = strengthBottom = 0.0f;

        if (enabled && vp != nullptr && vp->getVerticalScrollBar().isVisible()) {
            auto& sb = vp->getVerticalScrollBar();
            const double start = sb.getCurrentRangeStart();
            const double size = sb.getCurrentRangeSize();
            const double max = sb.getMaximumRangeLimit();

            const bool atTop = start <= 0.5;
            const double topDist = start;
            strengthTop = (float) juce::jlimit(0.0, 1.0, topDist / (double) juce::jmax(1, fadeHeightTop));
            showTop = enableTop && !atTop && strengthTop > 0.01f;

            const double remaining = (max - (start + size));
            const bool atBottom = remaining <= 0.5;
            strengthBottom = (float) juce::jlimit(0.0, 1.0, remaining / (double) juce::jmax(1, fadeHeightBottom));
            showBottom = enableBottom && !atBottom && strengthBottom > 0.01f;
        }

        const bool anyVisible = (showTop || showBottom);
        setVisible(anyVisible);
        if (anyVisible) repaint();
    }

    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds();
        const auto bg = findColour(scrollFadeOverlayBackgroundColourId, true);

        if (showTop && fadeHeightTop > 0) {
            const int h = juce::jmin(fadeHeightTop, area.getHeight());
            auto topRect = area.removeFromTop(h).expanded(0, 1);
            juce::ColourGradient gradTop(bg.withAlpha(strengthTop),
                                         (float) topRect.getX(), (float) topRect.getY(),
                                         bg.withAlpha(0.0f),
                                         (float) topRect.getX(), (float) topRect.getBottom(),
                                         false);
            g.setGradientFill(gradTop);
            g.fillRect(topRect);
        }

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
