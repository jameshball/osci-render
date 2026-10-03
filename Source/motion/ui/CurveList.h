#pragma once

#include <JuceHeader.h>
#include "MotionStyle.h"

// The Graph's channel list (like Blender's or After Effects' graph editor):
// every animatable property of the target, with its axis colour, whether it
// is keyed or driven, and an eye that shows it as a context curve. Clicking a
// row edits that property.
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
        animatedOnly.setOnColour(motion::style::accent().withAlpha(.3f));
        animatedOnly.onClick = [this] { layout(); };
        addAndMakeVisible(animatedOnly);
    }
    std::function<void(const std::string&)> onChoose;
    std::function<void(const std::string&, bool)> onShow;

    void setChannels(std::vector<Channel> next, const std::string& selectedId, const std::set<std::string>& shown) {
        const bool same = next.size() == channels.size() && std::equal(next.begin(), next.end(), channels.begin(), [](const auto& a, const auto& b) {
            return a.id == b.id && a.label == b.label && a.keyed == b.keyed && a.driven == b.driven && a.colour == b.colour;
        });
        channels = std::move(next);
        selected = selectedId;
        visible = shown;
        const auto anyAnimated = std::any_of(channels.begin(), channels.end(), [](const auto& channel) { return channel.keyed || channel.driven; });
        animatedOnly.setEnabled(anyAnimated);
        if (!same) { rebuild(); } else { layout(); }
        for (auto& row : rows) {
            row->selected = row->channel.id == selected;
            row->eye.setToggleState(visible.contains(row->channel.id) || row->selected, juce::dontSendNotification);
            // The edited curve is always drawn: its eye shows open but takes no clicks.
            row->eye.setInterceptsMouseClicks(!row->selected, false);
            row->repaint();
        }
        if (!same) { revealSelected(); }
    }
    void paint(juce::Graphics& g) override { g.fillAll(motion::style::sunken()); }
    void resized() override {
        auto area = getLocalBounds();
        animatedOnly.setBounds(area.removeFromTop(26).reduced(6, 4));
        viewport.setBounds(area);
        layout();
    }

private:
    struct Row final : juce::Component, juce::SettableTooltipClient {
        Channel channel;
        bool selected = false, hovered = false;
        struct Eye final : juce::Button {
            Eye() : juce::Button("Show curve") { setClickingTogglesState(false); }
            void paintButton(juce::Graphics& g, bool over, bool) override {
                // An eye: open (pupil drawn) when the curve is shown, closed (a
                // lid line) when it is not.
                const auto c = getLocalBounds().toFloat().getCentre();
                const auto shown = getToggleState();
                g.setColour(osci::Colours::text().withAlpha(isEnabled() ? (over ? 1.0f : shown ? .85f : .45f) : .3f));
                juce::Path eye;
                eye.startNewSubPath(c.x - 6, c.y);
                eye.quadraticTo(c.x, c.y - (shown ? 5.0f : 2.0f), c.x + 6, c.y);
                eye.quadraticTo(c.x, c.y + (shown ? 5.0f : 2.0f), c.x - 6, c.y);
                g.strokePath(eye, juce::PathStrokeType(1.2f));
                if (shown) { g.fillEllipse(c.x - 2, c.y - 2, 4, 4); }
            }
        } eye;
        std::function<void()> onClick;
        explicit Row(Channel value) : channel(std::move(value)) {
            setName("Curve " + channel.label);
            setTitle(getName());
            setTooltip(channel.label + (channel.keyed ? " - keyed" : "") + (channel.driven ? " - modulated" : "") + ". Click to edit its curve.");
            eye.setName("Show curve " + channel.label);
            eye.setTitle(eye.getName());
            eye.setTooltip("Show this curve behind the one being edited");
            addAndMakeVisible(eye);
        }
        void paint(juce::Graphics& g) override {
            auto bounds = getLocalBounds();
            if (selected) {
                g.setColour(motion::style::accent().withAlpha(.22f));
                g.fillRoundedRectangle(bounds.toFloat().reduced(2, 1), 3.0f);
            } else if (hovered) {
                g.setColour(juce::Colours::white.withAlpha(.05f));
                g.fillRoundedRectangle(bounds.toFloat().reduced(2, 1), 3.0f);
            }
            bounds.removeFromLeft(8);
            g.setColour(channel.colour.withAlpha(channel.keyed || channel.driven ? 1.0f : .45f));
            g.fillRoundedRectangle(bounds.removeFromLeft(4).withSizeKeepingCentre(4, 12).toFloat(), 1.5f);
            bounds.removeFromLeft(7);
            bounds.removeFromRight(24);
            auto marks = bounds.removeFromRight(26);
            if (channel.keyed) { motion::style::drawDiamond(g, {static_cast<float>(marks.getX() + 6), static_cast<float>(marks.getCentreY())}, 3.5f, true); }
            if (channel.driven) {
                g.setColour(motion::style::accent());
                g.setFont(motion::style::caption());
                g.drawText("~", marks.withTrimmedLeft(12), juce::Justification::centred);
            }
            g.setColour(selected ? juce::Colours::white : osci::Colours::text().withAlpha(channel.keyed || channel.driven ? .95f : .6f));
            g.setFont(selected ? motion::style::title() : motion::style::body());
            g.drawText(channel.label, bounds, juce::Justification::centredLeft, true);
        }
        void resized() override { eye.setBounds(getLocalBounds().removeFromRight(24).withSizeKeepingCentre(18, 14)); }
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
        const auto width = viewport.getWidth() - (viewport.isVerticalScrollBarShown() ? 6 : 0);
        int y = 4;
        juce::String group;
        const auto filtered = animatedOnly.getToggleState() && animatedOnly.isEnabled();
        for (auto& row : rows) {
            const auto shown = !filtered || row->channel.keyed || row->channel.driven || row->channel.id == selected;
            row->setVisible(shown);
            if (!shown) { continue; }
            if (row->channel.group != group && !group.isEmpty()) { y += 6; }
            group = row->channel.group;
            row->setBounds(0, y, std::max(0, width), rowHeight);
            y += rowHeight;
        }
        content.setSize(std::max(0, width), y + 4);
    }
    void revealSelected() {
        for (auto& row : rows) {
            if (!row->selected) { continue; }
            const auto top = row->getY(), bottom = row->getBottom();
            const auto view = viewport.getViewPositionY(), height = viewport.getMaximumVisibleHeight();
            if (top < view) { viewport.setViewPosition(0, top); } else if (bottom > view + height) { viewport.setViewPosition(0, bottom - height); }
        }
    }
    static constexpr int rowHeight = 22;
    motion::style::Chip animatedOnly {"Animated only"};
    std::vector<Channel> channels;
    std::vector<std::unique_ptr<Row>> rows;
    std::string selected;
    std::set<std::string> visible;
    juce::Viewport viewport;
    juce::Component content;
};
