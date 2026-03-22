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

    void setFadeWidth(int wLeftAndRight) {
        fadeWidthLeft = fadeWidthRight = juce::jmax(4, wLeftAndRight);
    }

    void setFadeWidths(int leftW, int rightW) {
        fadeWidthLeft = juce::jmax(4, leftW);
        fadeWidthRight = juce::jmax(4, rightW);
    }

    void setSidesEnabled(bool top, bool bottom) {
        enableTop = top;
        enableBottom = bottom;
        enableLeft = false;
        enableRight = false;
    }

    void setSidesEnabled(bool top, bool bottom, bool left, bool right) {
        enableTop = top;
        enableBottom = bottom;
        enableLeft = left;
        enableRight = right;
    }

    void layoutOver(const juce::Rectangle<int>& listBounds) { setBounds(listBounds); }

    void updateVisibilityFromViewport(juce::Viewport* vp, bool enabled) {
        showTop = showBottom = showLeft = showRight = false;
        strengthTop = strengthBottom = strengthLeft = strengthRight = 0.0f;

        if (enabled && vp != nullptr) {
            auto* viewed = vp->getViewedComponent();
            const bool scrollableVertically = viewed != nullptr && viewed->getHeight() > vp->getHeight();
            const bool scrollableHorizontally = viewed != nullptr && viewed->getWidth() > vp->getWidth();

            if (scrollableVertically) {
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

            if (scrollableHorizontally) {
                auto& sb = vp->getHorizontalScrollBar();
                const double start = sb.getCurrentRangeStart();
                const double size = sb.getCurrentRangeSize();
                const double max = sb.getMaximumRangeLimit();

                const bool atLeft = start <= 0.5;
                const double leftDist = start;
                strengthLeft = (float) juce::jlimit(0.0, 1.0, leftDist / (double) juce::jmax(1, fadeWidthLeft));
                showLeft = enableLeft && !atLeft && strengthLeft > 0.01f;

                const double remaining = (max - (start + size));
                const bool atRight = remaining <= 0.5;
                strengthRight = (float) juce::jlimit(0.0, 1.0, remaining / (double) juce::jmax(1, fadeWidthRight));
                showRight = enableRight && !atRight && strengthRight > 0.01f;
            }
        }

        const bool anyVisible = (showTop || showBottom || showLeft || showRight);
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

        area = getLocalBounds();
        if (showLeft && fadeWidthLeft > 0) {
            const int w = juce::jmin(fadeWidthLeft, area.getWidth());
            auto leftRect = area.removeFromLeft(w).expanded(1, 0);
            juce::ColourGradient gradLeft(bg.withAlpha(strengthLeft),
                                          (float) leftRect.getX(), (float) leftRect.getY(),
                                          bg.withAlpha(0.0f),
                                          (float) leftRect.getRight(), (float) leftRect.getY(),
                                          false);
            g.setGradientFill(gradLeft);
            g.fillRect(leftRect);
        }

        area = getLocalBounds();
        if (showRight && fadeWidthRight > 0) {
            const int w = juce::jmin(fadeWidthRight, area.getWidth());
            auto rightRect = area.removeFromRight(w).expanded(1, 0);
            juce::ColourGradient gradRight(bg.withAlpha(strengthRight),
                                           (float) rightRect.getRight(), (float) rightRect.getY(),
                                           bg.withAlpha(0.0f),
                                           (float) rightRect.getX(), (float) rightRect.getY(),
                                           false);
            g.setGradientFill(gradRight);
            g.fillRect(rightRect);
        }
    }

private:
    int fadeHeightTop { 48 };
    int fadeHeightBottom { 48 };
    int fadeWidthLeft { 48 };
    int fadeWidthRight { 48 };
    bool enableTop { true };
    bool enableBottom { true };
    bool enableLeft { false };
    bool enableRight { false };
    bool showTop { false };
    bool showBottom { false };
    bool showLeft { false };
    bool showRight { false };
    float strengthTop { 0.0f };
    float strengthBottom { 0.0f };
    float strengthLeft { 0.0f };
    float strengthRight { 0.0f };
};
