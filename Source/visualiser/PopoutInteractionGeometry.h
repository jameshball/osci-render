#pragma once

#include <JuceHeader.h>

#include <cstdint>

#include <osci_gui/visualiser/osci_VisualiserGeometry.h>

static constexpr std::uint8_t popoutInteractionAlphaThreshold = 50;

struct PopoutAlphaQuery {
    bool valid = false;
    juce::Point<float> normalisedPoint;
    juce::Point<float> normalisedRadius;
};

inline PopoutAlphaQuery makePopoutAlphaQuery(juce::Rectangle<int> windowBounds, VisualiserRenderSize frameSize,
                                              juce::Point<int> cursor, float padding) {
    if (frameSize.width <= 0 || frameSize.height <= 0) {
        return {};
    }
    const auto fitted = VisualiserGeometry::getAspectFitBounds(windowBounds, frameSize);
    if (fitted.isEmpty() || !fitted.contains(cursor)) {
        return {};
    }
    return {
        true,
        {static_cast<float>(cursor.x - fitted.getX()) / static_cast<float>(fitted.getWidth()),
         static_cast<float>(cursor.y - fitted.getY()) / static_cast<float>(fitted.getHeight())},
        {padding / static_cast<float>(fitted.getWidth()), padding / static_cast<float>(fitted.getHeight())},
    };
}
