#pragma once

#include <JuceHeader.h>
#include "MotionIcons.h"
#include "Chip.h"

// The Graph's channel list (like Blender's or After Effects' graph editor).
// A header names what is being edited; its animatable properties follow,
// grouped under captions ("Position") with each axis in its colour, whether
// it is keyed or driven, and an eye that shows it as a context curve.
// Clicking a row edits that property.
class MotionCurveList final : public juce::Component {
public:
    struct Channel {
        std::string id;
        juce::String label, group;
        juce::Colour colour;
        bool keyed = false, driven = false;
    };
    MotionCurveList() {
        setName("Graph channels");
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(6);
        addAndMakeVisible(viewport);
        animatedOnly.setName("Animated channels only");
        animatedOnly.setTitle(animatedOnly.getName());
        animatedOnly.setTooltip("Show only keyed or modulated channels, like After Effects' U");
        animatedOnly.setToggleState(true, juce::dontSendNotification);
        // A filter, not a status: on reads as pressed, not green.
        animatedOnly.setOnColour(juce::Colours::white.withAlpha(.14f));
        animatedOnly.onClick = [this] { layout(); };
        addAndMakeVisible(animatedOnly);
    }
    std::function<void(const std::string&)> onChoose;
    std::function<void(const std::string&, bool)> onShow;

    // What the channels belong to: its kind ("Clip", "Camera") and name.
    void setOwner(juce::String kind, juce::String name) {
        if (kind == ownerKind && name == ownerName) { return; }
        ownerKind = std::move(kind);
        ownerName = std::move(name);
        repaint();
    }

    void setChannels(std::vector<Channel> next, const std::string& selectedId, const std::set<std::string>& shown) {
        const bool same = next.size() == channels.size() && std::equal(next.begin(), next.end(), channels.begin(), [](const auto& a, const auto& b) {
            return a.id == b.id && a.label == b.label && a.keyed == b.keyed && a.driven == b.driven && a.colour == b.colour;
        });
        channels = std::move(next);
        selected = selectedId;
        visible = shown;
        const auto anyAnimated = std::any_of(channels.begin(), channels.end(), [](const auto& channel) { return channel.keyed || channel.driven; });
        animatedOnly.setEnabled(anyAnimated);
        animatedOnly.setVisible(!channels.empty());
        if (!same) { rebuild(); } else { layout(); }
        for (auto& row : rows) {
            row->selected = row->channel.id == selected;
            row->eye.setToggleState(visible.contains(row->channel.id) || row->selected, juce::dontSendNotification);
            // The edited curve is always drawn: its eye shows open but takes no clicks.
            row->eye.setInterceptsMouseClicks(!row->selected, false);
            row->repaint();
        }
        if (!same) { revealSelected(); }
        repaint();
    }
    void paint(juce::Graphics& g) override {
        // A panel like Properties beside the Graph's darker plot.
        g.fillAll(osci::Colours::surface());
        // The header lines up with the Graph's ruler band beside it.
        auto header = getLocalBounds().removeFromTop(headerHeight);
        g.setColour(osci::Colours::surfaceRaised());
        g.fillRect(header);
        header.removeFromLeft(10);
        header.removeFromRight(animatedOnly.getWidth() + 10);
        if (ownerName.isNotEmpty()) {
            g.setColour(osci::Colours::textMuted());
            g.setFont(motion::style::caption());
            const auto kindWidth = juce::GlyphArrangement::getStringWidthInt(motion::style::caption(), ownerKind) + 6;
            g.drawText(ownerKind, header.removeFromLeft(kindWidth), juce::Justification::centredLeft, false);
            g.setColour(osci::Colours::text());
            g.setFont(motion::style::title());
            g.drawText(ownerName, header, juce::Justification::centredLeft, true);
        } else {
            g.setColour(osci::Colours::textMuted());
            g.setFont(motion::style::body());
            g.drawText("Nothing selected", header, juce::Justification::centredLeft, true);
        }
        // Group captions above their rows.
        g.setFont(motion::style::caption());
        for (const auto& caption : captions) {
            const auto bounds = caption.bounds.translated(viewport.getX(), viewport.getY() - viewport.getViewPositionY());
            if (bounds.getBottom() <= viewport.getY() || bounds.getY() >= viewport.getBottom()) { continue; }
            g.setColour(osci::Colours::textMuted().withAlpha(.85f));
            g.drawText(caption.text, bounds.withTrimmedLeft(10), juce::Justification::centredLeft, false);
        }
    }
    void resized() override {
        auto area = getLocalBounds();
        auto header = area.removeFromTop(headerHeight);
        // A filter chip sized to its words, at the header's right.
        const auto chipWidth = juce::GlyphArrangement::getStringWidthInt(motion::style::caption(), animatedOnly.getButtonText()) + 18;
        animatedOnly.setBounds(header.removeFromRight(chipWidth + 6).withTrimmedRight(6).reduced(0, 4));
        viewport.setBounds(area);
        layout();
    }

private:
    struct Row final : juce::Component, juce::SettableTooltipClient {
        Channel channel;
        juce::String shortLabel;
        bool selected = false, hovered = false;
        struct Eye final : juce::Button {
            Eye() : juce::Button("Show curve") { setClickingTogglesState(false); }
            void paintButton(juce::Graphics& g, bool over, bool) override {
                // Material's eye: open when the curve is drawn, struck through when not.
                const auto shown = getToggleState();
                const auto alpha = isEnabled() ? (over ? 1.0f : shown ? .7f : .3f) : .3f;
                motion::icons::draw(g, shown ? motion::icons::Icon::visibility : motion::icons::Icon::visibilityOff, getLocalBounds().toFloat(), osci::Colours::text().withAlpha(alpha), 13.0f);
            }
        } eye;
        std::function<void()> onClick;
        explicit Row(Channel value) : channel(std::move(value)) {
            setName("Curve " + channel.label);
            setTitle(getName());
            setTooltip(channel.label + (channel.keyed ? " - keyed" : "") + (channel.driven ? " - modulated" : "") + ". Click to edit its curve.");
            // Under its group's caption, "Position X" reads as "X".
            shortLabel = channel.group.isNotEmpty() && channel.label.startsWith(channel.group + " ") ? channel.label.substring(channel.group.length() + 1) : channel.label;
            eye.setName("Show curve " + channel.label);
            eye.setTitle(eye.getName());
            eye.setTooltip("Show this curve behind the one being edited");
            addAndMakeVisible(eye);
        }
        void paint(juce::Graphics& g) override {
            auto bounds = getLocalBounds();
            const auto animated = channel.keyed || channel.driven;
            // Selection is neutral, marked by a bar in the channel's colour.
            if (selected) {
                g.setColour(juce::Colours::white.withAlpha(.07f));
                g.fillRect(bounds);
                g.setColour(channel.colour);
                g.fillRect(bounds.removeFromLeft(2));
            } else if (hovered) {
                g.setColour(juce::Colours::white.withAlpha(.04f));
                g.fillRect(bounds);
            }
            bounds = getLocalBounds().withTrimmedLeft(indent);
            g.setColour(channel.colour.withAlpha(animated ? 1.0f : .4f));
            g.fillEllipse(bounds.removeFromLeft(7).withSizeKeepingCentre(7, 7).toFloat());
            bounds.removeFromLeft(8);
            bounds.removeFromRight(26);
            auto marks = bounds.removeFromRight(26);
            if (channel.keyed) {
                g.setColour(selected ? juce::Colours::white.withAlpha(.9f) : osci::Colours::text().withAlpha(.5f));
                motion::style::drawDiamond(g, {static_cast<float>(marks.getX() + 6), static_cast<float>(marks.getCentreY())}, 4.0f, true);
            }
            // Lilac like the Result line that a modulator or link adds.
            if (channel.driven) { motion::icons::draw(g, motion::icons::Icon::wave, marks.withTrimmedLeft(12).toFloat(), motion::style::result(), 12.0f); }
            // A curve not drawn reads quieter until it is edited.
            const auto drawn = selected || eye.getToggleState();
            g.setColour(selected ? juce::Colours::white : osci::Colours::text().withAlpha(!animated ? .5f : drawn ? .9f : .5f));
            g.setFont(selected ? motion::style::title() : motion::style::body());
            g.drawText(shortLabel, bounds, juce::Justification::centredLeft, true);
        }
        void resized() override { eye.setBounds(getLocalBounds().removeFromRight(28).withSizeKeepingCentre(20, 16)); }
        void mouseEnter(const juce::MouseEvent&) override { hovered = true; repaint(); }
        void mouseExit(const juce::MouseEvent&) override { hovered = false; repaint(); }
        void mouseUp(const juce::MouseEvent& event) override { if (!event.mouseWasDraggedSinceMouseDown() && onClick) { onClick(); } }
        // A selectable list item for assistive tools (and UI automation).
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
            struct Handler final : juce::AccessibilityHandler {
                explicit Handler(Row& owner) : juce::AccessibilityHandler(owner, juce::AccessibilityRole::listItem,
                    juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press, [&owner] { if (owner.onClick) { owner.onClick(); } })), row(owner) {}
                juce::AccessibleState getCurrentState() const override {
                    auto state = juce::AccessibilityHandler::getCurrentState().withSelectable();
                    return row.selected ? state.withSelected() : state;
                }
                Row& row;
            };
            return std::make_unique<Handler>(*this);
        }
        int indent = 10;
    };
    struct Caption {
        juce::String text;
        juce::Rectangle<int> bounds; // In the content's coordinates.
    };
    void rebuild() {
        rows.clear();
        content.removeAllChildren();
        for (const auto& channel : channels) {
            auto row = std::make_unique<Row>(channel);
            const auto id = channel.id;
            row->onClick = [this, id] { if (onChoose) { onChoose(id); } };
            row->eye.onClick = [this, id] {
                const auto show = !visible.contains(id);
                if (show) { visible.insert(id); } else { visible.erase(id); }
                if (onShow) { onShow(id, show); }
            };
            content.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
        }
        layout();
    }
    void layout() {
        // Rows run edge to edge unless a scroll bar is actually showing.
        const auto width = viewport.getWidth() - (viewport.getVerticalScrollBar().isVisible() ? viewport.getScrollBarThickness() : 0);
        int y = 4;
        captions.clear();
        const auto filtered = animatedOnly.getToggleState() && animatedOnly.isEnabled();
        const auto shown = [&](const Row& row) { return !filtered || row.channel.keyed || row.channel.driven || row.channel.id == selected; };
        for (std::size_t index = 0; index < rows.size(); ++index) {
            auto& row = *rows[index];
            row.setVisible(shown(row));
            if (!row.isVisible()) { continue; }
            // A caption opens each group with more than its own name ("Position", not "Drawing").
            const auto& group = row.channel.group;
            const auto startsGroup = std::none_of(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(index), [&](const auto& other) { return other->isVisible() && other->channel.group == group; });
            const auto grouped = group.isNotEmpty() && row.shortLabel != row.channel.label;
            if (startsGroup) {
                y += index == 0 ? 0 : 4;
                if (grouped) {
                    captions.push_back({group, {0, y, std::max(0, width), captionHeight}});
                    y += captionHeight;
                }
            }
            row.indent = grouped ? 20 : 10;
            row.setBounds(0, y, std::max(0, width), rowHeight);
            y += rowHeight;
        }
        content.setSize(std::max(0, width), y + 4);
        repaint();
    }
    void revealSelected() {
        for (auto& row : rows) {
            if (!row->selected) { continue; }
            const auto top = row->getY(), bottom = row->getBottom();
            const auto view = viewport.getViewPositionY(), height = viewport.getMaximumVisibleHeight();
            if (top < view) { viewport.setViewPosition(0, top); } else if (bottom > view + height) { viewport.setViewPosition(0, bottom - height); }
        }
    }
    static constexpr int headerHeight = 26, rowHeight = 22, captionHeight = 18;
    motion::ui::Chip animatedOnly {"Animated"};
    std::vector<Channel> channels;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<Caption> captions;
    std::string selected;
    std::set<std::string> visible;
    juce::String ownerKind, ownerName;
    juce::Viewport viewport;
    juce::Component content;
};
