#pragma once

#include <JuceHeader.h>
#include "../model/Animation.h"

namespace motion::ui {
// How a value moves between two keys, as the Key panel and the key menus
// draw it in `box`: from a key at the lower left to one at the upper right,
// with a flat handle on each eased end. Sizes follow the panel's, a box 14
// across.
struct SegmentGlyph {
    juce::Path segment, handles;
    std::array<juce::Point<float>, 2> keys;
    float stroke, handleStroke, keySize;
};

namespace detail {
inline SegmentGlyph segmentGlyph(juce::Rectangle<float> box) {
    const auto unit = box.getWidth() / 14.0f;
    SegmentGlyph glyph;
    glyph.keys = {juce::Point<float>(box.getX(), box.getBottom()), juce::Point<float>(box.getRight(), box.getY())};
    glyph.segment.startNewSubPath(glyph.keys[0]);
    glyph.stroke = 1.4f * unit;
    glyph.handleStroke = 1.0f * unit;
    glyph.keySize = 3.2f * unit;
    return glyph;
}
// A flat handle reaching into the segment from a key.
inline void addHandle(SegmentGlyph& glyph, std::size_t key, float unit) {
    const auto at = glyph.keys[key];
    glyph.handles.startNewSubPath(at);
    glyph.handles.lineTo(at.translated(key == 0 ? 5.0f * unit : -5.0f * unit, 0));
}
}

// An interpolation's segment: Hold steps, Linear runs straight, Auto rises
// and settles, Bezier is an S with its handles.
inline SegmentGlyph interpolationGlyph(juce::Rectangle<float> box, motion::Interpolation interpolation) {
    auto glyph = detail::segmentGlyph(box);
    const auto unit = box.getWidth() / 14.0f;
    const auto x0 = box.getX(), x1 = box.getRight(), y0 = box.getBottom(), y1 = box.getY();
    switch (interpolation) {
        case motion::Interpolation::hold:
            glyph.segment.lineTo(box.getCentreX(), y0);
            glyph.segment.lineTo(box.getCentreX(), y1);
            glyph.segment.lineTo(x1, y1);
            break;
        case motion::Interpolation::linear: glyph.segment.lineTo(x1, y1); break;
        case motion::Interpolation::smooth: glyph.segment.cubicTo(x0 + 3.0f * unit, y1 + 1.0f * unit, x1 - 6.0f * unit, y1, x1, y1); break;
        case motion::Interpolation::cubic:
            glyph.segment.cubicTo(x1 - 2.0f * unit, y0, x0 + 2.0f * unit, y1, x1, y1);
            detail::addHandle(glyph, 0, unit);
            detail::addHandle(glyph, 1, unit);
            break;
    }
    return glyph;
}

// An Easy ease: flat leaving the first key, arriving at the second, or both,
// each eased end with its handle.
inline SegmentGlyph easeGlyph(juce::Rectangle<float> box, bool leaving, bool arriving) {
    auto glyph = detail::segmentGlyph(box);
    const auto unit = box.getWidth() / 14.0f;
    const auto x0 = box.getX(), x1 = box.getRight(), y0 = box.getBottom(), y1 = box.getY();
    const auto out = leaving ? juce::Point<float>(x1 - 2.0f * unit, y0) : juce::Point<float>(x0 + 4.0f * unit, y0 - 4.0f * unit);
    const auto in = arriving ? juce::Point<float>(x0 + 2.0f * unit, y1) : juce::Point<float>(x1 - 4.0f * unit, y1 + 4.0f * unit);
    glyph.segment.cubicTo(out, in, {x1, y1});
    if (leaving) { detail::addHandle(glyph, 0, unit); }
    if (arriving) { detail::addHandle(glyph, 1, unit); }
    return glyph;
}

// Draws a glyph in `colour`, its handles fainter.
inline void drawSegmentGlyph(juce::Graphics& g, const SegmentGlyph& glyph, juce::Colour colour) {
    g.setColour(colour);
    g.strokePath(glyph.segment, juce::PathStrokeType(glyph.stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (!glyph.handles.isEmpty()) {
        g.setColour(colour.withMultipliedAlpha(.55f));
        g.strokePath(glyph.handles, juce::PathStrokeType(glyph.handleStroke, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        g.setColour(colour);
    }
    for (const auto end : glyph.keys) { g.fillEllipse(juce::Rectangle<float>(glyph.keySize, glyph.keySize).withCentre(end)); }
}
}
