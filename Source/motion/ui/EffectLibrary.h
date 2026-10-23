#pragma once

#include "MotionStyle.h"
#include "LibraryCard.h"

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
            names.add(juce::String(definition.name.data(), definition.name.size()));
        }
        // The same search field as Sources'.
        search.setName("Search effects");
        search.setTextToShowWhenEmpty("Search effects", osci::Colours::textMuted());
        search.setFont(motion::style::body());
        motion::style::styleField(search);
        search.onTextChange = [this] { resized(); };
        search.onEscapeKey = [this] { search.clear(); resized(); };
        addAndMakeVisible(search);
        viewport.setViewedComponent(&content, false);
        motion::style::fadeScrolling(viewport);
        addAndMakeVisible(viewport);
    }
    std::function<void(const std::string&)> onInsert;
    void resized() override {
        // The search sits in the top slot every library tab shares, where
        // Add source and Add modulator sit in theirs; the rows line up with
        // Sources'.
        auto area = getLocalBounds();
        area.removeFromTop(6);
        search.setBounds(area.removeFromTop(motion::style::controlHeight).reduced(4, 0));
        area.removeFromTop(motion::style::gap);
        viewport.setBounds(area);
        const auto width = viewport.getMaximumVisibleWidth();
        const auto query = search.getText().trim();
        int y = 0;
        for (std::size_t index = 0; index < tiles.size(); ++index) {
            auto& tile = tiles[index];
            const auto shown = query.isEmpty() || names[static_cast<int>(index)].containsIgnoreCase(query);
            tile->setVisible(shown);
            if (!shown) { continue; }
            tile->setBounds(juce::Rectangle<int>(0, y, width, motion::ui::LibraryCard::pitch).reduced(4, motion::style::gap / 2));
            y += motion::ui::LibraryCard::pitch;
        }
        content.setSize(width, y);
    }

private:
    // A library card: what the effect does to a grid, and its name.
    class Tile final : public motion::ui::LibraryCard {
    public:
        Tile(MotionEffectLibrary& library, const motion::EffectDefinition& effect) : owner(library), definition(effect) {
            setName(juce::String(definition.name));
            setTitle(juce::String(definition.name) + " effect");
            setDescription("Drag onto an object, a track or the Scene. Double-click adds it to what Properties shows.");
            setText(getName());
            buildThumbnail();
        }
        void paintPicture(juce::Graphics& g, juce::Rectangle<float> thumb, float hover) override {
            const auto transform = juce::AffineTransform::scale(thumb.getWidth() * .5f, -thumb.getHeight() * .5f).translated(thumb.getCentre());
            if (!before.isEmpty()) {
                g.setColour(osci::Colours::text().withAlpha(.16f));
                g.strokePath(before, juce::PathStrokeType(1.0f), transform);
            }
            g.setColour(tint.withAlpha(.55f + .45f * hover));
            g.strokePath(thumbnail, juce::PathStrokeType(1.0f), transform);
        }
        void mouseDrag(const juce::MouseEvent& event) override {
            if (event.getDistanceFromDragStart() < 4) { return; }
            auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
            if (container != nullptr && !container->isDragAndDropActive()) { container->startDragging("motion-effect:" + juce::String(definition.id), this); }
        }
        void mouseDoubleClick(const juce::MouseEvent&) override {
            if (owner.onInsert) { owner.onInsert(definition.id); }
        }
    private:
        // Shapes run through the effect at its defaults, with a few values
        // turned up (or, for scale, down) so every tile shows a change.
        void buildThumbnail() {
            auto instance = motion::makeEffect(1, definition);
            for (const auto& [property, value] : std::initializer_list<std::pair<const char*, double>> {{"rotateZ", .12}, {"rotateY", .08}, {"scaleX", .65}, {"scaleY", .65}, {"translateX", .3}, {"translateY", .25}, {"skewX", .35}, {"hue", 150}, {"rippleDepth", .1}, {"rippleAmount", .08}, {"crush", .8}, {"twist", .25}, {"phase", .25}, {"fov", 110}}) {
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
            // What each tile runs through its effect: a grid by default; rows
            // of waves for ripples, which only move points in depth (shown
            // as a slant, like a plotted surface); rings for effects that
            // snap or wind about the centre. A move of the whole grid shows
            // over a faint grid as it was.
            using Kind = motion::EffectKind;
            const auto kind = definition.kind;
            const auto moves = kind == Kind::rotate || kind == Kind::scale || kind == Kind::translate || kind == Kind::skew || kind == Kind::wobble;
            const auto rows = kind == Kind::ripple;
            const auto rings = kind == Kind::polygon || kind == Kind::spiralCrush || kind == Kind::bitCrush;
            std::vector<std::vector<osci::Point>> lines;
            if (rings) {
                for (const auto radius : {.22f, .44f, .66f}) {
                    auto& ring = lines.emplace_back();
                    for (int step = 0; step <= 96; ++step) {
                        const auto angle = juce::MathConstants<float>::twoPi * static_cast<float>(step) / 96;
                        ring.emplace_back(radius * std::sin(angle), radius * std::cos(angle), 0);
                    }
                }
            } else {
                const int count = rows ? 6 : 5, steps = rows ? 48 : 24;
                for (int line = 0; line < count; ++line) {
                    const auto across = -.6f + 1.2f * static_cast<float>(line) / (count - 1);
                    for (const bool vertical : {false, true}) {
                        if (vertical && rows) { continue; }
                        auto& points = lines.emplace_back();
                        for (int step = 0; step <= steps; ++step) {
                            const auto along = -.6f + 1.2f * static_cast<float>(step) / steps;
                            points.push_back(vertical ? osci::Point(across, along, 0) : osci::Point(along, across, 0));
                            // A grid tilted away at the top shows depth effects.
                            if (kind == Kind::perspective) { points.back().z = points.back().y * 1.4f; }
                        }
                    }
                }
            }
            for (const auto& line : lines) {
                for (std::size_t index = 0; index < line.size(); ++index) {
                    auto point = motion::applyEffects(effects, line[index], .5);
                    if (rows) { point.y += point.z * .8f; }
                    point.x = std::clamp(point.x, -.95f, .95f);
                    point.y = std::clamp(point.y, -.95f, .95f);
                    if (index == 0) { thumbnail.startNewSubPath(point.x, point.y); } else { thumbnail.lineTo(point.x, point.y); }
                    if (!moves) { continue; }
                    if (index == 0) { before.startNewSubPath(line[index].x, line[index].y); } else { before.lineTo(line[index].x, line[index].y); }
                }
            }
        }
        MotionEffectLibrary& owner;
        const motion::EffectDefinition& definition;
        juce::Path thumbnail, before;
        juce::Colour tint;
    };
    osci::ScrollFadeViewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Tile>> tiles;
    juce::StringArray names;
    juce::TextEditor search;
};
