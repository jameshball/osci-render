#pragma once

#include "MotionStyle.h"

#include "../model/Effects.h"
#include "../render/PreparedEffects.h"

// The effects to drag onto clips, tracks and groups. Each tile shows what
// its effect does to a grid, so the list reads without trying each one.
class MotionEffectLibrary : public juce::Component {
public:
    MotionEffectLibrary() {
        setName("Effect library");
        for (const auto& definition : motion::effectCatalog()) {
            auto tile = std::make_unique<Tile>(*this, definition);
            content.addAndMakeVisible(*tile);
            tiles.push_back(std::move(tile));
        }
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(6);
        addAndMakeVisible(viewport);
    }
    std::function<void(const std::string&)> onInsert;
    void resized() override {
        auto area = getLocalBounds();
        viewport.setBounds(area);
        const auto width = viewport.getMaximumVisibleWidth();
        int y = motion::style::gap;
        for (auto& tile : tiles) {
            tile->setBounds(motion::style::gap, y, width - 2 * motion::style::gap, tileHeight);
            y += tileHeight + motion::style::gap;
        }
        content.setSize(width, y);
    }

private:
    static constexpr int tileHeight = 44;
    class Tile final : public juce::Component, public juce::SettableTooltipClient {
    public:
        Tile(MotionEffectLibrary& library, const motion::EffectDefinition& effect) : owner(library), definition(effect) {
            setName(juce::String(definition.name));
            setTitle(juce::String(definition.name) + " effect");
            setDescription("Drag onto an object, a track or the Scene. Double-click adds it to what Properties shows.");
            setTooltip(getDescription());
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            buildThumbnail();
        }
        void paint(juce::Graphics& g) override {
            const auto hover = fade.value();
            const auto bounds = getLocalBounds().toFloat();
            g.setColour(motion::style::field().interpolatedWith(motion::style::raised(), hover));
            g.fillRoundedRectangle(bounds, motion::style::radius + 1);
            g.setColour(motion::style::accent().withAlpha(.1f + .35f * hover));
            g.drawRoundedRectangle(bounds.reduced(.5f), motion::style::radius + 1, 1.0f);
            auto area = getLocalBounds().reduced(motion::style::gap);
            const auto thumb = area.removeFromLeft(area.getHeight()).toFloat();
            g.setColour(juce::Colours::black.withAlpha(.35f));
            g.fillRoundedRectangle(thumb, motion::style::radius);
            const auto transform = juce::AffineTransform::scale(thumb.getWidth() * .5f, -thumb.getHeight() * .5f).translated(thumb.getCentre());
            g.setColour(tint.withAlpha(.55f + .45f * hover));
            g.strokePath(thumbnail, juce::PathStrokeType(1.0f), transform);
            area.removeFromLeft(motion::style::padding);
            g.setColour(motion::style::text());
            g.setFont(motion::style::body());
            g.drawText(getName(), area, juce::Justification::centredLeft, true);
        }
        void mouseEnter(const juce::MouseEvent&) override { fade.setTarget(true); }
        void mouseExit(const juce::MouseEvent&) override { fade.setTarget(false); }
        void mouseDrag(const juce::MouseEvent& event) override {
            if (event.getDistanceFromDragStart() < 4) { return; }
            auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
            if (container != nullptr && !container->isDragAndDropActive()) { container->startDragging("motion-effect:" + juce::String(definition.id), this); }
        }
        void mouseDoubleClick(const juce::MouseEvent&) override {
            if (owner.onInsert) { owner.onInsert(definition.id); }
        }
    private:
        // A grid run through the effect at its defaults, with a few values
        // that are neutral by default turned up so every tile shows a change.
        void buildThumbnail() {
            auto instance = motion::makeEffect(1, definition);
            for (const auto& [property, value] : std::initializer_list<std::pair<const char*, double>> {{"rotateZ", .12}, {"rotateY", .08}, {"skewX", .35}, {"hue", 150}, {"rippleDepth", .6}, {"rippleAmount", .4}, {"crush", .97}, {"fov", 110}}) {
                const auto found = instance.properties.find(property);
                if (found != instance.properties.end()) { found->second = motion::Curve(value); }
            }
            const auto effects = motion::prepareEffects({instance});
            // Colour effects show in the stroke.
            const auto key = motion::style::key();
            osci::Point sample(0, 0, 0);
            sample.r = key.getFloatRed();
            sample.g = key.getFloatGreen();
            sample.b = key.getFloatBlue();
            const auto coloured = motion::applyEffects(effects, sample, .5);
            tint = juce::Colour::fromFloatRGBA(std::clamp(coloured.r, 0.0f, 1.0f), std::clamp(coloured.g, 0.0f, 1.0f), std::clamp(coloured.b, 0.0f, 1.0f), 1.0f);
            constexpr int lines = 5, steps = 24;
            for (int line = 0; line < lines; ++line) {
                const auto across = -.6f + 1.2f * static_cast<float>(line) / (lines - 1);
                for (const bool vertical : {false, true}) {
                    for (int step = 0; step <= steps; ++step) {
                        const auto along = -.6f + 1.2f * static_cast<float>(step) / steps;
                        // A grid tilted away at the top shows depth effects.
                        auto input = vertical ? osci::Point(across, along, 0) : osci::Point(along, across, 0);
                        if (definition.id == "perspective") { input.z = input.y * 1.4f; }
                        auto point = motion::applyEffects(effects, input, .5);
                        point.x = std::clamp(point.x, -.95f, .95f);
                        point.y = std::clamp(point.y, -.95f, .95f);
                        if (step == 0) { thumbnail.startNewSubPath(point.x, point.y); } else { thumbnail.lineTo(point.x, point.y); }
                    }
                }
            }
        }
        MotionEffectLibrary& owner;
        const motion::EffectDefinition& definition;
        juce::Path thumbnail;
        juce::Colour tint;
        motion::style::Fade fade {*this};
    };
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Tile>> tiles;
};
