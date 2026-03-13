#pragma once

#include <JuceHeader.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <functional>

// Generic flow trail / marker renderer for node graph components.
// Renders decaying "flow" markers that sweep across a graph fill region,
// leaving a luminous trail that fades over time.
//
// Used by both the DAHDSR envelope and the LFO waveform editor.
class FlowTrailRenderer {
public:
    // Configuration
    static constexpr double kDefaultTauMs = 180.0;
    static constexpr double kDefaultMaxBridgeTimeSeconds = 0.05;

    // domainToPixelFn: converts a domain value to pixel X coordinate (including handle offset)
    // componentWidthFn / componentHeightFn: return current component dimensions
    FlowTrailRenderer(std::function<float(double)> domainToPixelFn,
                      std::function<int()> componentWidthFn,
                      std::function<int()> componentHeightFn)
        : domainToPixel(std::move(domainToPixelFn)),
          getWidth(std::move(componentWidthFn)),
          getHeight(std::move(componentHeightFn)) {}

    bool hasTrailData() const { return !flowTrailLastSeenMs.empty(); }

    // Returns true if the trail has fully decayed and repainting can stop.
    bool shouldStopRepaint(double nowMs, double tauMs = kDefaultTauMs) {
        const double maxAgeMs = tauMs * 6.0;
        if (numFlowMarkers == 0 && flowTrailNewestMs > 0.0 && (nowMs - flowTrailNewestMs) > maxAgeMs) {
            std::fill(flowTrailLastSeenMs.begin(), flowTrailLastSeenMs.end(), 0.0);
            flowTrailNewestMs = 0.0;
            return true;
        }
        return false;
    }

    void resized() {
        const int width = getWidth();
        if (width > 0)
            flowTrailLastSeenMs.assign((size_t)width, 0.0);
        flowTrailNewestMs = 0.0;
        std::fill(prevFlowMarkerValid.begin(), prevFlowMarkerValid.end(), 0);
        std::fill(prevFlowMarkerTimeSeconds.begin(), prevFlowMarkerTimeSeconds.end(), 0.0);
        std::fill(prevFlowMarkerUpdateMs.begin(), prevFlowMarkerUpdateMs.end(), 0.0);
    }

    void setFlowMarkerTimes(const double* timesSeconds, int numTimes,
                            double maxBridgeTimeSeconds = kDefaultMaxBridgeTimeSeconds) {
        numFlowMarkers = juce::jmax(0, numTimes);
        if ((int)flowMarkerTimesSeconds.size() < numFlowMarkers)
            flowMarkerTimesSeconds.resize((size_t)numFlowMarkers, -1.0);
        for (int i = 0; i < numFlowMarkers; ++i)
            flowMarkerTimesSeconds[(size_t)i] = timesSeconds[i];

        if ((int)prevFlowMarkerX.size() < numFlowMarkers) {
            prevFlowMarkerX.resize((size_t)numFlowMarkers, 0.0f);
            prevFlowMarkerValid.resize((size_t)numFlowMarkers, 0);
            prevFlowMarkerTimeSeconds.resize((size_t)numFlowMarkers, 0.0);
            prevFlowMarkerUpdateMs.resize((size_t)numFlowMarkers, 0.0);
        }

        const int w = getWidth();
        if (w > 0) {
            if ((int)flowTrailLastSeenMs.size() != w)
                flowTrailLastSeenMs.assign((size_t)w, 0.0);

            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            flowTrailNewestMs = juce::jmax(flowTrailNewestMs, nowMs);
            for (int i = 0; i < numFlowMarkers; ++i) {
                const double t = flowMarkerTimesSeconds[i];
                if (t < 0.0) {
                    prevFlowMarkerValid[(size_t)i] = 0;
                    prevFlowMarkerTimeSeconds[(size_t)i] = 0.0;
                    prevFlowMarkerUpdateMs[(size_t)i] = 0.0;
                    continue;
                }

                const float x = domainToPixel(t);
                const int xi = (int)juce::jlimit(0.0f, (float)(w - 1), x);

                const bool canBridge = prevFlowMarkerValid[(size_t)i] != 0
                    && (wrapping || (t >= prevFlowMarkerTimeSeconds[(size_t)i]))
                    && (wrapping || ((t - prevFlowMarkerTimeSeconds[(size_t)i]) <= maxBridgeTimeSeconds));

                if (canBridge) {
                    const double prevUpdateMs = prevFlowMarkerUpdateMs[(size_t)i] > 0.0
                        ? prevFlowMarkerUpdateMs[(size_t)i] : nowMs;
                    const float prevX = prevFlowMarkerX[(size_t)i];

                    if (wrapping && t < prevFlowMarkerTimeSeconds[(size_t)i]) {
                        // Phase wrapped: bridge prev→max, then min→current
                        const float maxX = domainToPixel(wrapDomainMax);
                        const float minX = domainToPixel(wrapDomainMin);
                        bridgePixels(prevX, maxX, w, nowMs);
                        bridgePixels(minX, x, w, nowMs);
                    } else {
                        bridgePixelsInterpolated(prevX, x, w, prevUpdateMs, nowMs);
                    }
                } else {
                    flowTrailLastSeenMs[(size_t)xi] = juce::jmax(flowTrailLastSeenMs[(size_t)xi], nowMs);
                }

                prevFlowMarkerX[(size_t)i] = x;
                prevFlowMarkerValid[(size_t)i] = 1;
                prevFlowMarkerTimeSeconds[(size_t)i] = t;
                prevFlowMarkerUpdateMs[(size_t)i] = nowMs;
            }
        }
    }

    void clear() { numFlowMarkers = 0; }

    // Set wrapping mode for cyclic domains (e.g. LFO phase 0–1).
    // When enabled, backward jumps in position are treated as domain wrapping
    // and the trail bridges across the wrap boundary.
    void setWrapping(bool shouldWrap, double domainMin = 0.0, double domainMax = 1.0) {
        wrapping = shouldWrap;
        wrapDomainMin = domainMin;
        wrapDomainMax = domainMax;
    }

    void reset() {
        numFlowMarkers = 0;
        std::fill(flowTrailLastSeenMs.begin(), flowTrailLastSeenMs.end(), 0.0);
        flowTrailNewestMs = 0.0;
        std::fill(prevFlowMarkerValid.begin(), prevFlowMarkerValid.end(), 0);
        std::fill(prevFlowMarkerTimeSeconds.begin(), prevFlowMarkerTimeSeconds.end(), 0.0);
        std::fill(prevFlowMarkerUpdateMs.begin(), prevFlowMarkerUpdateMs.end(), 0.0);
    }

    // Paint the decaying trail inside the given fill path.
    // outGlowStrength receives the max alpha for glow effects.
    void paintTrail(juce::Graphics& g, const juce::Path& fillPath,
                    juce::Colour trailColour, float& outGlowStrength,
                    double tauMs = kDefaultTauMs) {
        const float maxTrailAlpha = 1.0f;
        const float alphaFloor = 0.0f;

        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(fillPath);
        const int w = getWidth();
        const int h = getHeight();
        if ((int)flowTrailLastSeenMs.size() == w && w > 0) {
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            const float invTau = 1.0f / (float)juce::jmax(1.0, tauMs);

            if (flowTrailStripWidth != w || !flowTrailStrip.isValid()) {
                flowTrailStrip = juce::Image(juce::Image::ARGB, w, 1, true);
                flowTrailStripWidth = w;
            }

            juce::Image::BitmapData bd(flowTrailStrip, juce::Image::BitmapData::writeOnly);
            for (int x = 0; x < w; ++x) {
                const double lastSeen = flowTrailLastSeenMs[(size_t)x];
                if (lastSeen <= 0.0) {
                    bd.setPixelColour(x, 0, juce::Colours::transparentBlack);
                    continue;
                }
                const double ageMs = nowMs - lastSeen;
                const float a = (ageMs <= 0.0)
                    ? 1.0f
                    : juce::jlimit(0.0f, 1.0f, 1.0f - ((float)ageMs * invTau));
                const float shaped = a * (2.0f - a);
                const float finalA = juce::jlimit(0.0f, 1.0f, maxTrailAlpha * (alphaFloor + (1.0f - alphaFloor) * shaped));
                outGlowStrength = juce::jmax(outGlowStrength, finalA);
                bd.setPixelColour(x, 0, trailColour.withAlpha(finalA));
            }

            g.drawImage(flowTrailStrip, 0, 0, w, h, 0, 0, w, 1);
        }
    }

    // Paint gradient marker heads at current flow positions inside the fill path.
    void paintMarkerHeads(juce::Graphics& g, const juce::Path& fillPath,
                          juce::Colour markerColour) {
        if (numFlowMarkers <= 0)
            return;

        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(fillPath);
        const int h = getHeight();
        for (int i = 0; i < numFlowMarkers; ++i) {
            const double timeSeconds = flowMarkerTimesSeconds[i];
            if (timeSeconds < 0.0)
                continue;
            const float x = domainToPixel(timeSeconds);

            const float outerHalfWidth = 10.0f;
            juce::Rectangle<float> outer(x - outerHalfWidth, 0.0f, outerHalfWidth * 2.0f, (float)h);
            juce::ColourGradient outerGradient(
                markerColour.withAlpha(0.0f), outer.getX(), 0.0f,
                markerColour.withAlpha(0.0f), outer.getRight(), 0.0f,
                false);
            outerGradient.addColour(0.5, markerColour.withAlpha(0.18f));
            g.setGradientFill(outerGradient);
            g.fillRect(outer);

            const float innerHalfWidth = 5.0f;
            juce::Rectangle<float> inner(x - innerHalfWidth, 0.0f, innerHalfWidth * 2.0f, (float)h);
            juce::ColourGradient innerGradient(
                markerColour.withAlpha(0.0f), inner.getX(), 0.0f,
                markerColour.withAlpha(0.0f), inner.getRight(), 0.0f,
                false);
            innerGradient.addColour(0.5, markerColour.withAlpha(0.32f));
            g.setGradientFill(innerGradient);
            g.fillRect(inner);

            g.setColour(markerColour.withAlpha(0.60f));
            g.drawLine(x, 0.0f, x, (float)h, 2.5f);
        }
    }

private:
    std::function<float(double)> domainToPixel;
    std::function<int()> getWidth;
    std::function<int()> getHeight;

    int numFlowMarkers = 0;
    std::vector<double> flowMarkerTimesSeconds;
    std::vector<double> flowTrailLastSeenMs;
    std::vector<float> prevFlowMarkerX;
    std::vector<char> prevFlowMarkerValid;
    std::vector<double> prevFlowMarkerTimeSeconds;
    std::vector<double> prevFlowMarkerUpdateMs;
    double flowTrailNewestMs = 0.0;
    juce::Image flowTrailStrip;
    int flowTrailStripWidth = 0;

    bool wrapping = false;
    double wrapDomainMin = 0.0;
    double wrapDomainMax = 1.0;

    // Bridge a pixel range with a uniform timestamp.
    void bridgePixels(float x1, float x2, int w, double timestampMs) {
        const int a = (int)juce::jlimit(0.0f, (float)(w - 1), juce::jmin(x1, x2));
        const int b = (int)juce::jlimit(0.0f, (float)(w - 1), juce::jmax(x1, x2));
        for (int xx = a; xx <= b; ++xx)
            flowTrailLastSeenMs[(size_t)xx] = juce::jmax(flowTrailLastSeenMs[(size_t)xx], timestampMs);
    }

    // Bridge a pixel range with linearly interpolated timestamps.
    void bridgePixelsInterpolated(float x1, float x2, int w, double startMs, double endMs) {
        const int a = (int)juce::jlimit(0.0f, (float)(w - 1), juce::jmin(x1, x2));
        const int b = (int)juce::jlimit(0.0f, (float)(w - 1), juce::jmax(x1, x2));
        const int denom = juce::jmax(1, b - a);
        for (int xx = a; xx <= b; ++xx) {
            const double u = (double)(xx - a) / (double)denom;
            const double tMs = startMs + u * (endMs - startMs);
            flowTrailLastSeenMs[(size_t)xx] = juce::jmax(flowTrailLastSeenMs[(size_t)xx], tMs);
        }
    }
};
