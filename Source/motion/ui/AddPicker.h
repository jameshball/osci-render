#pragma once

#include <JuceHeader.h>
#include "GeneratorIcons.h"
#include "MotionStyle.h"
#include "TileGrid.h"

namespace motion::ui {
// A popover of things to add, as pictures under a heading, in sections of
// their own: choosing one adds it and closes the popover.
class AddPicker final : public juce::Component {
public:
    static constexpr int width = 280;
    struct Section {
        juce::String heading;
        std::vector<TileGrid::Choice> choices;
        int columns;
        TileGrid::Look look = TileGrid::Look::chips;
    };

    // `chosen(section, index)` adds what was chosen.
    AddPicker(const juce::String& heading, std::vector<Section> sections, std::function<void(std::size_t, std::size_t)> chosen) : title(heading) {
        setName(heading);
        int height = headingHeight;
        for (std::size_t index = 0; index < sections.size(); ++index) {
            auto& section = sections[index];
            auto grid = std::make_unique<TileGrid>(section.heading.isEmpty() ? heading : section.heading, std::move(section.choices), section.columns, section.look);
            grid->onClick = [this, chosen, index](std::size_t choice) {
                // Closed first: what is added may open something of its own.
                if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) { box->dismiss(); }
                if (chosen) { chosen(index, choice); }
            };
            addAndMakeVisible(*grid);
            headings.push_back(section.heading);
            height += (section.heading.isEmpty() ? 0 : subheadingHeight) + grid->heightFor(width - 2 * motion::style::padding) + motion::style::padding;
            grids.push_back(std::move(grid));
        }
        setSize(width, height);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(osci::Colours::text());
        g.setFont(motion::style::title());
        g.drawText(title, getLocalBounds().removeFromTop(headingHeight).reduced(motion::style::padding, 0), juce::Justification::centredLeft, true);
        g.setFont(motion::style::heading());
        g.setColour(osci::Colours::text().withAlpha(.6f));
        for (std::size_t index = 0; index < grids.size(); ++index) {
            if (headings[index].isEmpty()) { continue; }
            const auto& bounds = grids[index]->getBounds();
            g.drawText(headings[index], bounds.getX(), bounds.getY() - subheadingHeight, bounds.getWidth(), subheadingHeight - motion::style::gap, juce::Justification::bottomLeft, true);
        }
    }
    void resized() override {
        const auto inner = getWidth() - 2 * motion::style::padding;
        int y = headingHeight;
        for (std::size_t index = 0; index < grids.size(); ++index) {
            if (headings[index].isNotEmpty()) { y += subheadingHeight; }
            const auto height = grids[index]->heightFor(inner);
            grids[index]->setBounds(motion::style::padding, y, inner, height);
            y += height + motion::style::padding;
        }
    }

private:
    static constexpr int headingHeight = 32, subheadingHeight = 20;
    std::vector<std::unique_ptr<TileGrid>> grids;
    std::vector<juce::String> headings;
    juce::String title;
};

namespace pictures {
// A generator's picture in the square from -1 to 1.
inline Picture ofGenerator(const GeneratorDefinition& definition) {
    auto path = generatorPicture(definition, 2, .08f);
    path.applyTransform(juce::AffineTransform::translation(-1, -1));
    return {path, {}};
}
// A mirror ball, small: round, a horizon across it and a glint.
inline Picture mirrorBall() {
    juce::Path path;
    path.addEllipse(-.9f, -.9f, 1.8f, 1.8f);
    path.startNewSubPath(-.88f, .12f);
    path.quadraticTo(0, .5f, .88f, .12f);
    path.startNewSubPath(-.42f, -.5f);
    path.quadraticTo(-.5f, -.2f, -.3f, -.06f);
    return {path, {}};
}
// A flat mirror: a pane turned to show its face, glints across it.
inline Picture flatMirror() {
    juce::Path pane;
    pane.startNewSubPath(-.72f, -.62f);
    pane.lineTo(.5f, -.86f);
    pane.lineTo(.72f, .62f);
    pane.lineTo(-.5f, .86f);
    pane.closeSubPath();
    juce::Path glints;
    glints.startNewSubPath(-.36f, .08f);
    glints.lineTo(.02f, -.42f);
    glints.startNewSubPath(-.18f, .36f);
    glints.lineTo(.34f, -.32f);
    pane.addPath(glints);
    return {pane, {}};
}
}
}
