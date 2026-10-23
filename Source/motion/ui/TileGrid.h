#pragma once

#include <JuceHeader.h>
#include "MotionIcons.h"
#include "MotionStyle.h"
#include "StylePictures.h"

namespace motion::ui {
// Choices in a grid, each on or off, clicked to change, or (`oneOf`) one of
// them chosen at a time: as pictures above their names, where seeing each
// helps choose, or as chips, a small icon beside each name. An on tile is lit
// in the accent; its corner says how much when part-way, or that its value
// moves (keyed, or driven by a modulator), as a property's row does.
class TileGrid final : public juce::Component {
public:
    struct Choice {
        juce::String name, tooltip;
        pictures::Picture picture;
    };
    enum class Look { pictures, chips };

    TileGrid(const juce::String& name, std::vector<Choice> choices, int columns, Look look = Look::pictures, bool oneOf = false) : columnCount(columns), look(look) {
        setName(name);
        for (auto& choice : choices) {
            auto tile = std::make_unique<Tile>(std::move(choice), look);
            // Named for automation and access by its grid too: "Surface styles: Spiral".
            tile->setName(name + ": " + tile->choice.name);
            tile->setTitle(tile->getName());
            // Read out as a choice of one, or a switch each.
            tile->setToggleable(true);
            if (oneOf) { tile->setRadioGroupId(1, juce::dontSendNotification); }
            const auto index = tiles.size();
            tile->onClick = [this, index] {
                if (onClick) { onClick(index); }
            };
            addAndMakeVisible(*tile);
            tiles.push_back(std::move(tile));
        }
    }

    std::function<void(std::size_t)> onClick;

    // How a tile's value changes as the composition plays.
    enum class Moves { no, keyed, driven };
    // Whether a tile is on, and what its corner says: that its value moves,
    // or else (when part-way) how much.
    void setState(std::size_t index, bool on, const juce::String& amount = {}, Moves moves = Moves::no) {
        auto& tile = *tiles[index];
        if (tile.getToggleState() == on && tile.amount == amount && tile.moves == moves) { return; }
        tile.amount = amount;
        tile.moves = moves;
        tile.setToggleState(on, juce::dontSendNotification);
        tile.repaint();
    }
    void setLocked(bool locked) {
        for (auto& tile : tiles) { tile->setEnabled(!locked); }
    }
    juce::Button& tile(std::size_t index) { return *tiles[index]; }
    const juce::String& nameOf(std::size_t index) const { return tiles[index]->choice.name; }

    // Pictures no taller than `pictureHeight`, closer to the tile's edge and
    // their names in a shorter line, so the grid stays low; below
    // `narrowWidth` a tile, `narrowColumns` columns instead.
    void setCompact(int pictureHeight, int narrowWidth, int narrowColumns) {
        pictureLimit = pictureHeight;
        narrowTileWidth = narrowWidth;
        narrowColumnCount = narrowColumns;
        for (auto& tile : tiles) { tile->compact = true; }
        resized();
    }

    int heightFor(int width) const {
        const auto columns = columnsFor(width);
        const auto rows = (static_cast<int>(tiles.size()) + columns - 1) / columns;
        return rows * tileHeight(width) + (rows - 1) * motion::style::gap;
    }
    void resized() override {
        const auto width = getWidth();
        const auto columns = columnsFor(width);
        const auto tileWidth = (width - (columns - 1) * motion::style::gap) / columns;
        // The spare pixels go to the first columns, so the grid fills the width exactly.
        const auto spare = width - (tileWidth * columns + (columns - 1) * motion::style::gap);
        const auto height = tileHeight(width);
        for (std::size_t index = 0; index < tiles.size(); ++index) {
            const auto column = static_cast<int>(index) % columns, row = static_cast<int>(index) / columns;
            const auto x = column * (tileWidth + motion::style::gap) + std::min(column, spare);
            tiles[index]->setBounds(x, row * (height + motion::style::gap), tileWidth + (column < spare ? 1 : 0), height);
        }
    }

private:
    // A picture tile's picture is as tall as it is wide less its margins
    // (or its limit), with its name below; a chip is a control's height.
    static constexpr int nameHeight = 18, compactNameHeight = 14, compactInset = 2, chipHeight = motion::style::controlHeight + 2;
    int columnsFor(int width) const {
        if (narrowColumnCount <= 0) { return columnCount; }
        const auto tileWidth = (width - (columnCount - 1) * motion::style::gap) / columnCount;
        return tileWidth < narrowTileWidth ? narrowColumnCount : columnCount;
    }
    int tileHeight(int width) const {
        if (look == Look::chips) { return chipHeight; }
        const auto columns = columnsFor(width);
        const auto tileWidth = (width - (columns - 1) * motion::style::gap) / columns;
        if (pictureLimit > 0) { return std::min(tileWidth - 2 * compactInset, pictureLimit) + compactNameHeight + 2 * compactInset; }
        return tileWidth - 2 * motion::style::gap + nameHeight + motion::style::gap;
    }

    struct Tile final : public juce::Button {
        Tile(Choice value, Look shape) : juce::Button(value.name), choice(std::move(value)), look(shape) {
            setTooltip(choice.tooltip);
            setWantsKeyboardFocus(false);
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }
        void paintButton(juce::Graphics& g, bool, bool down) override {
            // Locked: shown as it is, faded.
            if (!isEnabled()) { g.beginTransparencyLayer(.45f); }
            paintTile(g, down);
            if (!isEnabled()) { g.endTransparencyLayer(); }
        }
        void paintTile(juce::Graphics& g, bool down) {
            const auto on = getToggleState();
            const auto hover = fade.value();
            // The same card as the libraries'.
            motion::style::paintCard(g, getLocalBounds().toFloat(), hover, on, down);
            const auto ink = on ? motion::style::key() : osci::Colours::text().withAlpha(.5f + .3f * hover);
            if (look == Look::chips) {
                // A small icon, then the name.
                auto area = getLocalBounds().reduced(motion::style::padding, 0);
                const auto icon = area.removeFromLeft(static_cast<int>(motion::style::iconSmall)).toFloat().withSizeKeepingCentre(motion::style::iconSmall, motion::style::iconSmall).translated(0, down ? .5f : 0);
                g.setColour(ink);
                g.strokePath(choice.picture.lines, juce::PathStrokeType(1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                    juce::AffineTransform::scale(icon.getWidth() * .5f).translated(icon.getCentre()));
                area.removeFromLeft(6);
                if (paintsCorner()) { paintCorner(g, area.removeFromRight(28).toFloat(), juce::Justification::centredRight); }
                g.setFont(motion::style::body());
                g.setColour(on ? osci::Colours::text() : osci::Colours::text().withAlpha(.75f + .25f * hover));
                g.drawText(choice.name, area, juce::Justification::centredLeft, true);
                return;
            }
            auto area = getLocalBounds().reduced(compact ? compactInset : motion::style::gap);
            // Compact, the name takes the tile's whole width: a small tile's
            // longest name still fits.
            const auto name = compact ? area.removeFromBottom(compactNameHeight).withX(1).withWidth(getWidth() - 2) : area.removeFromBottom(nameHeight);
            auto well = area.toFloat();
            well = well.withSizeKeepingCentre(std::min(well.getWidth(), well.getHeight()), std::min(well.getWidth(), well.getHeight()));
            motion::style::fillWell(g, area.toFloat());
            // The square from -1 to 1, a little in from the well's edge.
            const auto scale = well.getWidth() * .5f * .78f;
            const auto transform = juce::AffineTransform::scale(scale).translated(well.getCentre()).translated(0, down ? .5f : 0);
            if (!choice.picture.context.isEmpty()) {
                g.setColour(ink.withMultipliedAlpha(.35f));
                g.strokePath(choice.picture.context, juce::PathStrokeType(1.0f), transform);
            }
            g.setColour(ink);
            g.strokePath(choice.picture.lines, juce::PathStrokeType(on ? 1.25f : 1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), transform);
            g.setFont(motion::style::caption());
            g.setColour(on ? osci::Colours::text() : osci::Colours::text().withAlpha(.7f + .3f * hover));
            g.drawText(choice.name, name, juce::Justification::centred, true);
            if (paintsCorner()) { paintCorner(g, area.reduced(3).toFloat().removeFromTop(12), juce::Justification::topRight); }
        }
        void buttonStateChanged() override { fade.follow(*this); }
        bool paintsCorner() const { return moves != Moves::no || amount.isNotEmpty(); }
        // That it moves, as the modulate wave or the key diamond of a
        // property's row; else how much.
        void paintCorner(juce::Graphics& g, juce::Rectangle<float> area, juce::Justification justification) const {
            const auto colour = osci::Colours::text().withAlpha(.8f);
            constexpr float mark = 12;
            const auto at = justification.appliedToRectangle(juce::Rectangle<float>(mark, mark), area);
            if (moves == Moves::driven) {
                motion::icons::draw(g, motion::icons::Icon::wave, at, colour, mark);
            } else if (moves == Moves::keyed) {
                g.setColour(colour);
                motion::style::drawDiamond(g, at.getCentre(), 3.5f, false, 1.1f);
            } else {
                g.setFont(motion::style::caption());
                g.setColour(colour);
                g.drawText(amount, area, justification, false);
            }
        }

        Choice choice;
        Look look;
        bool compact = false;
        juce::String amount;
        Moves moves = Moves::no;
        motion::style::Fade fade {*this};
    };

    int columnCount, pictureLimit = 0, narrowTileWidth = 0, narrowColumnCount = 0;
    Look look;
    std::vector<std::unique_ptr<Tile>> tiles;
};
}
