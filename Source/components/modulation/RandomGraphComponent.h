#pragma once

#include <JuceHeader.h>
#include <vector>
#include "../../audio/GraphNode.h"

// A time-series graph component that displays modulation output history as a
// scrolling waveform trace. Uses the same line/fill styling as NodeGraphComponent.
// Unlike NodeGraphComponent, this is NOT node-based — it draws from a circular
// buffer of recent values.
class RandomGraphComponent : public juce::Component {
public:
    RandomGraphComponent();
    ~RandomGraphComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Push a new value (0..1) and its active state into the history buffer.
    void pushValue(float value, bool isActive);

    // Push without triggering repaint (for batching). Call repaint() manually after.
    void pushValueSilent(float value, bool isActive);

    // Clear all history.
    void clearHistory();

    // Set the accent colour for the line and fill.
    void setAccentColour(juce::Colour colour);

    // Set the corner radius for rounded clipping.
    void setCornerRadius(float r) { cornerRadius = r; repaint(); }

    // Colour IDs matching NodeGraphComponent for consistency.
    enum ColourIds {
        backgroundColourId = 0x7002000,
        gridLineColourId   = 0x7002001,
        lineColourId       = 0x7002002,
        fillColourId       = 0x7002003,
    };

private:
    static constexpr int kMaxHistory = 512;
    static constexpr float kCurveStrokeThickness = 1.75f;

    struct HistPoint { float x, y; bool isActive; };

    std::vector<float> history;
    std::vector<bool> activeHistory;
    std::vector<HistPoint> paintPoints;  // reused across paint calls
    int writeIndex = 0;
    int historyCount = 0;
    float cornerRadius = 6.0f;

    void paintGrid(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RandomGraphComponent)
};
