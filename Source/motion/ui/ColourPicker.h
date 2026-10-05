#pragma once

#include "MotionStyle.h"
#include <array>

// The Colour row's picker: a saturation and brightness square above a hue
// strip. Dragging edits live; values stay in 0..1 RGB at full precision.
class MotionColourPicker final : public juce::Component {
public:
    using Rgb = std::array<double, 3>;

    explicit MotionColourPicker(Rgb initial) {
        setName("Colour picker");
        setTitle("Colour picker");
        setRgb(initial);
        setSize(margin * 2 + width, margin * 2 + squareHeight + gap + stripHeight);
    }
    std::function<void()> onBegin, onEnd;
    // Closed mid-drag (Escape dismisses its popover), it still ends the
    // gesture it began.
    ~MotionColourPicker() override {
        if (part != Part::none && onEnd) { onEnd(); }
    }
    std::function<void(Rgb)> onChange;

    void setRgb(Rgb rgb) {
        for (auto& channel : rgb) { channel = std::clamp(channel, 0.0, 1.0); }
        const auto high = std::max({rgb[0], rgb[1], rgb[2]});
        const auto low = std::min({rgb[0], rgb[1], rgb[2]});
        const auto range = high - low;
        brightness = high;
        saturation = high > 0 ? range / high : 0.0;
        // Grey keeps the hue last chosen.
        if (range > 0) {
            double sector = 0;
            if (high == rgb[0]) {
                sector = std::fmod((rgb[1] - rgb[2]) / range + 6.0, 6.0);
            } else if (high == rgb[1]) {
                sector = (rgb[2] - rgb[0]) / range + 2.0;
            } else {
                sector = (rgb[0] - rgb[1]) / range + 4.0;
            }
            hue = sector / 6.0;
        }
        repaint();
    }
    Rgb rgb() const {
        const auto sector = std::fmod(hue, 1.0) * 6.0;
        const auto index = static_cast<int>(sector);
        const auto fraction = sector - index;
        const auto p = brightness * (1 - saturation);
        const auto q = brightness * (1 - saturation * fraction);
        const auto t = brightness * (1 - saturation * (1 - fraction));
        switch (index) {
            case 0: return {brightness, t, p};
            case 1: return {q, brightness, p};
            case 2: return {p, brightness, t};
            case 3: return {p, q, brightness};
            case 4: return {t, p, brightness};
            default: return {brightness, p, q};
        }
    }

    void paint(juce::Graphics& g) override {
        const auto square = squareBounds().toFloat();
        juce::Path squareShape;
        squareShape.addRoundedRectangle(square, motion::style::radius);
        {
            juce::Graphics::ScopedSaveState clip(g);
            g.reduceClipRegion(squareShape);
            g.setColour(juce::Colour::fromHSV(static_cast<float>(hue), 1.0f, 1.0f, 1.0f));
            g.fillRect(square);
            g.setGradientFill(juce::ColourGradient(juce::Colours::white, square.getTopLeft(), juce::Colours::white.withAlpha(0.0f), square.getTopRight(), false));
            g.fillRect(square);
            g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.0f), square.getTopLeft(), juce::Colours::black, square.getBottomLeft(), false));
            g.fillRect(square);
        }
        const auto strip = stripBounds().toFloat();
        juce::ColourGradient hues(juce::Colours::red, strip.getTopLeft(), juce::Colours::red, strip.getTopRight(), false);
        for (int stop = 1; stop < 6; ++stop) { hues.addColour(stop / 6.0, juce::Colour::fromHSV(stop / 6.0f, 1.0f, 1.0f, 1.0f)); }
        g.setGradientFill(hues);
        g.fillRoundedRectangle(strip, motion::style::radius);
        g.setColour(juce::Colours::white.withAlpha(.12f));
        g.drawRoundedRectangle(square.reduced(.5f), motion::style::radius, 1.0f);
        g.drawRoundedRectangle(strip.reduced(.5f), motion::style::radius, 1.0f);
        const auto picked = rgb();
        const auto colour = juce::Colour::fromFloatRGBA(static_cast<float>(picked[0]), static_cast<float>(picked[1]), static_cast<float>(picked[2]), 1.0f);
        marker(g, {square.getX() + static_cast<float>(saturation) * square.getWidth(), square.getY() + static_cast<float>(1 - brightness) * square.getHeight()}, colour);
        marker(g, {strip.getX() + static_cast<float>(hue) * strip.getWidth(), strip.getCentreY()}, juce::Colour::fromHSV(static_cast<float>(hue), 1.0f, 1.0f, 1.0f));
    }
    void mouseDown(const juce::MouseEvent& event) override {
        const auto point = event.getPosition();
        part = squareBounds().expanded(4).contains(point) ? Part::square : stripBounds().expanded(0, 4).contains(point) ? Part::strip : Part::none;
        if (part == Part::none) { return; }
        if (onBegin) { onBegin(); }
        pick(event.position);
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        if (part != Part::none) { pick(event.position); }
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (part == Part::none) { return; }
        part = Part::none;
        if (onEnd) { onEnd(); }
    }

private:
    static constexpr int margin = 10, width = 200, squareHeight = 136, gap = 10, stripHeight = 12;
    enum class Part { none, square, strip };
    juce::Rectangle<int> squareBounds() const { return {margin, margin, width, squareHeight}; }
    juce::Rectangle<int> stripBounds() const { return {margin, margin + squareHeight + gap, width, stripHeight}; }
    static void marker(juce::Graphics& g, juce::Point<float> centre, juce::Colour fill) {
        const auto ring = juce::Rectangle<float>(12.0f, 12.0f).withCentre(centre);
        g.setColour(fill);
        g.fillEllipse(ring.reduced(1.5f));
        g.setColour(juce::Colours::black.withAlpha(.45f));
        g.drawEllipse(ring, 1.0f);
        g.setColour(juce::Colours::white);
        g.drawEllipse(ring.reduced(1.0f), 2.0f);
    }
    void pick(juce::Point<float> point) {
        if (part == Part::square) {
            const auto square = squareBounds().toFloat();
            saturation = std::clamp((point.x - square.getX()) / square.getWidth(), 0.0f, 1.0f);
            brightness = 1.0 - std::clamp((point.y - square.getY()) / square.getHeight(), 0.0f, 1.0f);
        } else {
            const auto strip = stripBounds().toFloat();
            // Full right is red again; keep it just short so the marker stays put.
            hue = std::clamp((point.x - strip.getX()) / strip.getWidth(), 0.0f, 0.9999f);
        }
        repaint();
        if (onChange) { onChange(rgb()); }
    }
    double hue = 0, saturation = 0, brightness = 1;
    Part part = Part::none;
};
