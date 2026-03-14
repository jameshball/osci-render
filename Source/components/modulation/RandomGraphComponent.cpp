#include "RandomGraphComponent.h"
#include "../../LookAndFeel.h"

RandomGraphComponent::RandomGraphComponent() {
    history.resize(kMaxHistory, 0.5f);
    activeHistory.resize(kMaxHistory, false);

    setColour(backgroundColourId, Colours::veryDark);
    setColour(gridLineColourId, juce::Colours::white.withAlpha(0.1f));
    setColour(lineColourId, juce::Colour(0xFF00E5FF));
    setColour(fillColourId, juce::Colour(0xFF00E5FF).withAlpha(0.15f));
}

void RandomGraphComponent::pushValue(float value, bool isActive) {
    pushValueSilent(value, isActive);
    repaint();
}

void RandomGraphComponent::pushValueSilent(float value, bool isActive) {
    history[(size_t)writeIndex] = juce::jlimit(0.0f, 1.0f, value);
    activeHistory[(size_t)writeIndex] = isActive;
    writeIndex = (writeIndex + 1) % kMaxHistory;
    if (historyCount < kMaxHistory)
        historyCount++;
}

void RandomGraphComponent::clearHistory() {
    std::fill(history.begin(), history.end(), 0.5f);
    std::fill(activeHistory.begin(), activeHistory.end(), false);
    writeIndex = 0;
    historyCount = 0;
    repaint();
}

void RandomGraphComponent::setAccentColour(juce::Colour colour) {
    setColour(lineColourId, colour);
    setColour(fillColourId, colour.withAlpha(0.15f));
    repaint();
}

void RandomGraphComponent::paintGrid(juce::Graphics& g) {
    int w = getWidth();
    int h = getHeight();
    auto gridColour = findColour(gridLineColourId);

    // Horizontal midline
    g.setColour(gridColour);
    g.drawHorizontalLine(h / 2, 0.0f, (float)w);

    // Subtle quarter lines
    g.setColour(gridColour.withMultipliedAlpha(0.5f));
    g.drawHorizontalLine(h / 4, 0.0f, (float)w);
    g.drawHorizontalLine(3 * h / 4, 0.0f, (float)w);
}

void RandomGraphComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    if (cornerRadius > 0) {
        juce::Path clip;
        clip.addRoundedRectangle(bounds, cornerRadius);
        g.reduceClipRegion(clip);
    }

    // Background
    g.setColour(findColour(backgroundColourId));
    g.fillRect(bounds);

    paintGrid(g);

    if (historyCount < 2) {
        // Draw midline when no data
        g.setColour(findColour(lineColourId).withAlpha(0.3f));
        g.drawHorizontalLine(getHeight() / 2, 0.0f, (float)getWidth());
        return;
    }

    int w = getWidth();
    int h = getHeight();
    float padding = 2.0f;
    float drawH = (float)h - padding * 2.0f;

    // Build path from history buffer (oldest on left, newest on right)
    int numPoints = juce::jmin(historyCount, w);
    float xStep = (float)w / (float)juce::jmax(1, numPoints - 1);

    auto lineColour = findColour(lineColourId);
    auto fillColour = findColour(fillColourId);

    // Reuse class member to avoid per-frame allocation
    paintPoints.clear();
    paintPoints.reserve(numPoints);

    for (int i = 0; i < numPoints; ++i) {
        int readIdx = (writeIndex - numPoints + i + kMaxHistory) % kMaxHistory;
        float value = history[(size_t)readIdx];
        bool ptActive = activeHistory[(size_t)readIdx];

        float x = (float)i * xStep;
        float y = padding + drawH * (1.0f - value);
        paintPoints.push_back({ x, y, ptActive });
    }

    // Build line and fill paths for active regions only
    {
        int i = 0;
        while (i < numPoints) {
            // Skip inactive points
            if (!paintPoints[i].isActive) { ++i; continue; }

            // Found start of an active region
            juce::Path regionLine;
            regionLine.startNewSubPath(paintPoints[i].x, paintPoints[i].y);

            juce::Path fillPath;
            fillPath.startNewSubPath(paintPoints[i].x, paintPoints[i].y);

            int regionStart = i;
            ++i;
            while (i < numPoints && paintPoints[i].isActive) {
                regionLine.lineTo(paintPoints[i].x, paintPoints[i].y);
                fillPath.lineTo(paintPoints[i].x, paintPoints[i].y);
                ++i;
            }
            int regionEnd = i - 1;

            // Close the fill path downward
            fillPath.lineTo(paintPoints[regionEnd].x, (float)h);
            fillPath.lineTo(paintPoints[regionStart].x, (float)h);
            fillPath.closeSubPath();

            g.setColour(fillColour);
            g.fillPath(fillPath);

            g.setColour(lineColour.withAlpha(0.90f));
            g.strokePath(regionLine, juce::PathStrokeType(kCurveStrokeThickness, juce::PathStrokeType::beveled, juce::PathStrokeType::rounded));
        }
    }
}

void RandomGraphComponent::resized() {
}
