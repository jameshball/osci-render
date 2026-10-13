#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

// The shapes culling, lighting and reflecting make of what they keep: parts
// of a drawing's shapes, lines coloured at their ends, and shapes lit or
// dimmed.
namespace motion::hidden {
// The part of a shape between two points of its progress. The shape itself
// is kept alive by its source.
class ShapePart final : public osci::Shape {
public:
    // `owner` keeps `whole` alive: its source, or the drawing it is of.
    ShapePart(osci::Shape& whole, std::shared_ptr<const void> owner, float from, float to) : shape(&whole), owner(std::move(owner)), start(from), end(to) {}
    osci::Point nextVector(float progress) override { return shape->nextVector(start + (end - start) * progress); }
    void scale(float, float, float) override {}
    void translate(float, float, float) override {}
    float length() override {
        if (len < 0) { len = shape->length() * (end - start); }
        return len;
    }
    std::unique_ptr<osci::Shape> clone() override { return std::make_unique<ShapePart>(*this); }
    osci::Shape& whole() const { return *shape; }
    std::string type() override { return "Part"; }

private:
    osci::Shape* shape;
    std::shared_ptr<const void> owner;
    float start, end;
};

// A line whose ends keep their own colours: a run of a point source.
class ColouredLine final : public osci::Shape {
public:
    ColouredLine(osci::Point from, osci::Point to) : a(from), b(to) {}
    osci::Point nextVector(float progress) override {
        const auto mix = [progress](float x, float y) { return x + (y - x) * progress; };
        const auto coloured = a.r >= 0 && b.r >= 0;
        return {mix(a.x, b.x), mix(a.y, b.y), mix(a.z, b.z), coloured ? mix(a.r, b.r) : -1.0f, coloured ? mix(a.g, b.g) : -1.0f, coloured ? mix(a.b, b.b) : -1.0f};
    }
    void scale(float, float, float) override {}
    void translate(float, float, float) override {}
    float length() override {
        if (len < 0) { len = std::hypot(b.x - a.x, b.y - a.y, b.z - a.z); }
        return len;
    }
    std::unique_ptr<osci::Shape> clone() override { return std::make_unique<ColouredLine>(*this); }
    std::string type() override { return "Line"; }

private:
    osci::Point a, b;
};

// A point's own colour (white where it has none) times the light reaching
// it. Lit brighter than the beam can draw, it is as bright as it can be in
// its own colour: every channel scaled alike, not each cut off at full,
// which would turn any strong coloured light white.
inline osci::Point litColour(osci::Point point, const std::array<float, 3>& light) {
    point.r = (point.r < 0 ? 1.0f : point.r) * light[0];
    point.g = (point.g < 0 ? 1.0f : point.g) * light[1];
    point.b = (point.b < 0 ? 1.0f : point.b) * light[2];
    const auto brightest = std::max({point.r, point.g, point.b});
    if (brightest > 1) {
        point.r /= brightest;
        point.g /= brightest;
        point.b /= brightest;
    }
    return point;
}

// A shape, or part of one, lit: it draws its own colours (white where it has
// none) times the light reaching it, blended between the light at stops along
// it. One shape for a whole run of light, however its light changes, so the
// beam walks it in even steps: every shape's end takes a sample of its own,
// and many short pieces of one line would bunch samples into beads.
class LitPart final : public osci::Shape {
public:
    struct Stop {
        float at;
        std::array<float, 3> light;
    };
    LitPart(std::shared_ptr<osci::Shape> whole, float from, float to, std::vector<Stop> stops) : shape(std::move(whole)), start(from), end(to), lights(std::move(stops)) {}
    osci::Point nextVector(float progress) override {
        const auto next = std::upper_bound(lights.begin(), lights.end(), progress, [](float value, const Stop& stop) { return value < stop.at; });
        const auto& after = next == lights.end() ? lights.back() : *next;
        const auto& before = next == lights.begin() ? lights.front() : *(next - 1);
        const auto span = after.at - before.at;
        const auto blend = span > 0 ? std::clamp((progress - before.at) / span, 0.0f, 1.0f) : 0.0f;
        std::array<float, 3> light {};
        for (std::size_t channel = 0; channel < 3; ++channel) { light[channel] = before.light[channel] + (after.light[channel] - before.light[channel]) * blend; }
        return litColour(shape->nextVector(start + (end - start) * progress), light);
    }
    void scale(float, float, float) override {}
    void translate(float, float, float) override {}
    float length() override {
        if (len < 0) { len = shape->length() * (end - start); }
        return len;
    }
    std::unique_ptr<osci::Shape> clone() override { return std::make_unique<LitPart>(*this); }
    std::string type() override { return "Lit"; }

private:
    std::shared_ptr<osci::Shape> shape;
    float start, end;
    std::vector<Stop> lights;
};

// A shape drawn dimmer: a mirror's own lines, as it shows the scene in their
// place, or a surface's, as bright as its Lines say.
class DimmedShape final : public osci::Shape {
public:
    DimmedShape(std::unique_ptr<osci::Shape> whole, float gain) : shape(std::move(whole)), factor(gain) {}
    osci::Point nextVector(float progress) override {
        auto point = shape->nextVector(progress);
        point.r = (point.r < 0 ? 1.0f : point.r) * factor;
        point.g = (point.g < 0 ? 1.0f : point.g) * factor;
        point.b = (point.b < 0 ? 1.0f : point.b) * factor;
        return point;
    }
    void scale(float, float, float) override {}
    void translate(float, float, float) override {}
    float length() override { return shape->length(); }
    std::unique_ptr<osci::Shape> clone() override { return std::make_unique<DimmedShape>(shape->clone(), factor); }
    float gain() const { return factor; }
    osci::Shape& whole() const { return *shape; }
    std::string type() override { return "Dimmed"; }

private:
    std::unique_ptr<osci::Shape> shape;
    float factor;
};

// Straight wherever it is drawn: a line, or one dimmed.
inline bool isStraight(osci::Shape& shape) {
    auto* dimmed = dynamic_cast<DimmedShape*>(&shape);
    return dynamic_cast<osci::Line*>(dimmed != nullptr ? &dimmed->whole() : &shape) != nullptr;
}
}
