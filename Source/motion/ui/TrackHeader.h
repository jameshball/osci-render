#pragma once

#include "../model/Timeline.h"
#include "MotionIcons.h"

class MotionTrackHeader : public juce::Component {
public:
    explicit MotionTrackHeader(motion::Id trackId) : id(trackId) {
        setName("Track header");
        name.setEditable(false, true);
        name.setJustificationType(juce::Justification::centredLeft);
        name.setFont(motion::style::body());
        name.setMinimumHorizontalScale(1.0f);
        name.addMouseListener(this, false);
        disclosure.onClick = [this] {
            if (isGroup) { if (onCollapse) { onCollapse(id); } }
            else if (onLanes) { onLanes(id); }
        };
        addChildComponent(disclosure);
        name.onTextChange = [this] { if (onRename) { onRename(id, name.getText().trim().toStdString()); } };
        mute.setTooltip("Mute this track");
        solo.setTooltip("Show only soloed tracks");
        lock.setTooltip("Lock this track against edits");

        mute.onClick = [this] { if (onMute) { onMute(id); } };
        solo.onClick = [this] { if (onSolo) { onSolo(id); } };
        lock.onClick = [this] { if (onLock) { onLock(id); } };
        arm.onClick = [this] { if (onArm) { onArm(id); } };
        arm.setOnColour(juce::Colour(0xffb04545));
        mute.setOnColour(juce::Colour(0xff8b6434));
        solo.setOnColour(juce::Colour(0xff347b52));
        lock.setOnColour(juce::Colour(0xff5c5f6b));
        // The glyph matches the M and S letters' height.
        lock.iconSize = 10.0f;
        grip.setTooltip("Drag to reorder; click for track actions");
        // A click opens the menu; a drag moves the track instead.
        grip.onClick = [this] { if (gripTravel < 4 && onMenu) { onMenu(id); } };
        grip.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        grip.addMouseListener(this, false);
        for (auto* child : std::initializer_list<juce::Component*> { &name, &mute, &solo, &grip }) { addAndMakeVisible(child); }
        addChildComponent(lock);
        addChildComponent(arm);
    }
    void update(const motion::Track& track, bool group = false, bool collapsed = false, bool lanes = false, bool expanded = false) {
        isGroup = group;
        const auto& labels = motion::style::trackLabels();
        const auto colour = !group && track.label > 0 && track.label < static_cast<int>(labels.size()) ? juce::Colour(labels[static_cast<std::size_t>(track.label)].argb).brighter(.5f) : juce::Colours::transparentBlack;
        if (colour != labelColour) { labelColour = colour; repaint(); }
        disclosure.setVisible(group || lanes);
        disclosure.setToggleState(group ? collapsed : !expanded, juce::dontSendNotification);
        lock.setVisible(!group);
        const auto armable = !group && track.kind == motion::TrackKind::visual && armingAvailable;
        if (arm.isVisible() != armable) {
            arm.setVisible(armable);
            resized();
        }
        grip.setMouseCursor(group ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor);
        grip.setTooltip(group ? "Group actions" : "Drag to reorder; click for track actions");
        disclosure.setTitle(group ? "Fold group " + juce::String(id) : "Keyframe lanes " + juce::String(id));
        disclosure.setTooltip(group ? "Fold or unfold this group" : "Show or hide keyframe lanes");
        name.setFont(group ? motion::style::title() : motion::style::body());
        if (!name.isBeingEdited()) { name.setText(juce::String(track.name), juce::dontSendNotification); }
        name.setTooltip(juce::String(track.name) + " - double-click to rename");
        name.setName("Track name " + juce::String(track.id));
        mute.setName("Mute track " + juce::String(track.id));
        solo.setName("Solo track " + juce::String(track.id));
        grip.setName("Reorder track " + juce::String(track.id));
        name.setTitle(name.getName());
        mute.setTitle(mute.getName());
        solo.setTitle(solo.getName());
        grip.setTitle(grip.getName());
        lock.setName("Lock track " + juce::String(track.id));
        lock.setTitle(lock.getName());
        mute.setToggleState(track.muted, juce::dontSendNotification);
        solo.setToggleState(track.solo, juce::dontSendNotification);
        lock.setToggleState(track.locked, juce::dontSendNotification);
        arm.setName("MIDI input track " + juce::String(track.id));
        arm.setTitle(arm.getName());
        arm.setToggleState(track.midiInput != 0, juce::dontSendNotification);
        arm.setTooltip(track.midiInput == 0 ? "Play and record live MIDI on this track (choose a channel in the track menu)"
            : "Live MIDI input: " + (track.midiInput == motion::Track::anyMidiChannel ? juce::String("any channel") : "channel " + juce::String(track.midiInput)));
    }
    // While dragged, the header covers the rows it passes over with its own
    // card and the left of the lifted block's outline.
    void setLifted(bool value) {
        if (lifted != value) {
            lifted = value;
            repaint();
        }
    }
    void paint(juce::Graphics& g) override {
        if (lifted) {
            const auto card = getLocalBounds().toFloat().withTrimmedTop(-1.0f).withTrimmedBottom(-1.0f).withWidth(static_cast<float>(getWidth() + 8)).reduced(.75f);
            juce::Path shape;
            shape.addRoundedRectangle(card.getX(), card.getY(), card.getWidth(), card.getHeight(), 5.0f, 5.0f, true, false, true, false);
            g.setColour(motion::style::raised());
            g.fillPath(shape);
            g.setColour(motion::style::accent().withAlpha(.55f));
            g.strokePath(shape, juce::PathStrokeType(1.5f));
        }
        if (!labelColour.isTransparent()) {
            g.setColour(labelColour);
            g.fillRect(0, 2, 3, std::min(getHeight(), 28) - 4);
        }
    }
    // One compact line: [fold/lanes] [grip] name ... [M][S][L]
    void resized() override {
        // Controls stay on one top line however tall the row is.
        auto bounds = getLocalBounds().removeFromTop(std::min(getHeight(), 28)).reduced(3, 0);
        disclosure.setBounds(bounds.removeFromLeft(16));
        grip.setBounds(bounds.removeFromLeft(14));
        // Square 16 px switches, 2 px apart.
        constexpr int chip = 16, spacing = 2;
        int count = 0;
        for (auto* button : std::initializer_list<juce::Button*> { &arm, &mute, &solo, &lock }) { count += button->isVisible() ? 1 : 0; }
        const auto chips = count * (chip + spacing);
        auto buttons = bounds.removeFromRight(chips).withSizeKeepingCentre(chips, chip);
        for (auto* button : std::initializer_list<juce::Button*> { &arm, &mute, &solo, &lock }) {
            if (!button->isVisible()) { continue; }
            button->setBounds(buttons.removeFromLeft(chip));
            buttons.removeFromLeft(spacing);
        }
        name.setBounds(bounds.reduced(4, 0).withSizeKeepingCentre(bounds.getWidth() - 8, 18));
    }
    void mouseDown(const juce::MouseEvent& event) override {
        // Right-click anywhere on the header opens the track's menu.
        if (event.mods.isPopupMenu()) {
            if (onMenu) { onMenu(id); }
            return;
        }
        if (event.eventComponent == &grip) { gripTravel = 0; }
        // Anywhere on the header but its buttons selects the track or group.
        if ((event.eventComponent == &name || event.eventComponent == this) && onSelect) { onSelect(id); }
    }
    // Dragging the grip moves the track itself: the timeline lifts it and
    // slides the other rows aside.
    void mouseDrag(const juce::MouseEvent& event) override {
        if (isGroup || event.eventComponent != &grip) { return; }
        gripTravel = std::max(gripTravel, event.getDistanceFromDragStart());
        if (gripTravel >= 4 && onReorder) { onReorder(id, event.getScreenPosition(), false); }
    }
    void mouseUp(const juce::MouseEvent& event) override {
        if (isGroup || event.eventComponent != &grip || gripTravel < 4) { return; }
        // The button has already ignored this release as a click; later
        // clicks (or an accessibility press) open the menu again.
        gripTravel = 0;
        if (onReorder) { onReorder(id, event.getScreenPosition(), true); }
    }
    const motion::Id id;
    std::function<void(motion::Id, std::string)> onRename;
    std::function<void(motion::Id)> onMute, onSolo, onMenu, onSelect, onCollapse, onLanes, onLock, onArm;
    // Live input reaches main-timeline tracks only.
    bool armingAvailable = true;
    // The grip's drag, in screen coordinates; `finished` on release.
    std::function<void(motion::Id, juce::Point<int>, bool finished)> onReorder;
private:
    bool isGroup = false, lifted = false;
    int gripTravel = 0;
    juce::Colour labelColour = juce::Colours::transparentBlack;
    class Disclosure : public juce::TextButton {
        void paintButton(juce::Graphics& g, bool over, bool) override {
            g.setColour(osci::Colours::text().withAlpha(over ? 1.0f : 0.65f));
            const auto x = getWidth() * 0.5f;
            const auto y = getHeight() * 0.5f;
            juce::Path path;
            if (getToggleState()) { path.addTriangle(x - 2, y - 4, x + 3, y, x - 2, y + 4); } else { path.addTriangle(x - 4, y - 2, x + 4, y - 2, x, y + 3); }
            g.fillPath(path);
        }
    } disclosure;
    juce::Label name;
    class ReorderHandle : public juce::TextButton {
        void paintButton(juce::Graphics& g, bool over, bool down) override {
            g.setColour(osci::Colours::text().withAlpha(over || down ? 0.9f : 0.4f));
            for (int row = -1; row <= 1; ++row) {
                for (int column = 0; column < 2; ++column) {
                    g.fillEllipse(getWidth() * 0.5f - 4 + column * 5, getHeight() * 0.5f - 1 + row * 5, 2, 2);
                }
            }
        }
    } grip;
    motion::icons::Chip lock {"Lock", motion::icons::Icon::lock};
    motion::style::Chip mute {"M"}, solo {"S"}, arm {juce::String(juce::CharPointer_UTF8("\xe2\x97\x8f"))};
};
