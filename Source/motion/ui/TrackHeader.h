#pragma once

#include "../model/Timeline.h"
#include "Chip.h"
#include "MotionIcons.h"
#include "GrabCursor.h"

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
            if (isGroup) {
                if (onCollapse) { onCollapse(id); }
            } else if (onLanes) {
                onLanes(id);
            }
        };
        addChildComponent(disclosure);
        name.onTextChange = [this] { if (onRename) { onRename(id, name.getText().trim().toStdString()); } };
        solo.setTooltip("Solo: hear and see only soloed tracks");

        mute.onClick = [this] { if (onMute) { onMute(id); } };
        solo.onClick = [this] { if (onSolo) { onSolo(id); } };
        lock.onClick = [this] { if (onLock) { onLock(id); } };
        mute.setOnColour(motion::style::trackMute());
        solo.setOnColour(motion::style::trackSolo());
        lock.setOnColour(motion::style::trackLock());
        // Off, a switch is just its letter or glyph; on, it fills.
        for (auto* chip : {&mute, &solo, &lock}) { chip->quiet = true; }
        setRepaintsOnMouseActivity(true);
        // The glyph matches the M and S letters' height.
        lock.iconSize = 10.0f;
        // A click opens the menu; a drag moves the track instead. The open
        // hand says so; a tooltip window would reset the cursor on macOS.
        grip.onClick = [this] { if (gripTravel < 4 && onMenu) { onMenu(id); } };
        grip.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        grip.addMouseListener(this, false);
        for (auto* child : std::initializer_list<juce::Component*> { &name, &mute, &solo, &grip }) { addAndMakeVisible(child); }
        addChildComponent(lock);
    }
    void update(const motion::Track& track, bool group = false, bool collapsed = false, bool lanes = false, bool expanded = false) {
        if (isGroup != group) {
            isGroup = group;
            resized();
            repaint();
        }
        disclosure.setVisible(group || lanes);
        disclosure.setToggleState(group ? collapsed : !expanded, juce::dontSendNotification);
        lock.setVisible(!group);
        // Automation finds the controls by their names, which say whose they
        // are by id; read out, they say it by the track's name.
        const juce::String label(track.name);
        disclosure.setName(group ? "Fold group " + juce::String(id) : "Key lanes " + juce::String(id));
        disclosure.setTitle(group ? "Fold " + label : "Key lanes of " + label);
        disclosure.setTooltip(group ? juce::String() : "Show or hide key lanes");
        name.setFont(group ? motion::style::title() : motion::style::body());
        if (!name.isBeingEdited()) { name.setText(juce::String(track.name), juce::dontSendNotification); }
        name.setTooltip("Double-click to rename");
        name.setName("Track name " + juce::String(track.id));
        mute.setName("Mute track " + juce::String(track.id));
        solo.setName("Solo track " + juce::String(track.id));
        grip.setName("Reorder track " + juce::String(track.id));
        name.setTitle(label);
        mute.setTitle("Mute " + label);
        solo.setTitle("Solo " + label);
        grip.setTitle("Reorder " + label);
        lock.setName("Lock track " + juce::String(track.id));
        lock.setTitle("Lock " + label);
        mute.setToggleState(track.muted, juce::dontSendNotification);
        solo.setToggleState(track.solo, juce::dontSendNotification);
        lock.setToggleState(track.locked, juce::dontSendNotification);
    }
    // While dragged, the timeline draws the header in its lifted block above
    // the other rows; the header itself stays in place, unseen, to keep
    // receiving the drag.
    void setLifted(bool value) { setAlpha(value ? 0.0f : 1.0f); }
    // One compact line: [fold/lanes] [grip] name ... [M][S][L]
    void resized() override {
        // Controls stay on one top line however tall the row is.
        auto bounds = getLocalBounds().removeFromTop(std::min(getHeight(), 28)).reduced(3, 0);
        disclosure.setBounds(bounds.removeFromLeft(16));
        grip.setBounds(bounds.removeFromLeft(14));
        // Three fixed 18 px slots, so every row's switches line up; a group
        // leaves its lock slot empty.
        constexpr int chip = 18, spacing = 1;
        auto buttons = bounds.removeFromRight(3 * (chip + spacing)).withSizeKeepingCentre(3 * (chip + spacing), chip);
        for (auto* button : std::initializer_list<juce::Button*> { &mute, &solo, &lock }) {
            button->setBounds(buttons.removeFromLeft(chip));
            buttons.removeFromLeft(spacing);
        }
        // A group shows a folder before its name.
        folderArea = isGroup ? bounds.removeFromLeft(18).toFloat() : juce::Rectangle<float>();
        name.setBounds(bounds.reduced(4, 0).withSizeKeepingCentre(bounds.getWidth() - 8, 18));
    }
    void paintOverChildren(juce::Graphics& g) override {
        if (!folderArea.isEmpty()) { motion::icons::draw(g, motion::icons::Icon::folder, folderArea, osci::Colours::text().withAlpha(.7f), 14.0f); }
    }
    void mouseDown(const juce::MouseEvent& event) override {
        // Right-click anywhere on the header opens the track's menu.
        if (event.mods.isPopupMenu()) {
            if (onMenu) { onMenu(id); }
            return;
        }
        if (event.eventComponent == &grip) { gripTravel = 0; }
        // Anywhere on the header but its buttons selects the track or group.
        if ((event.eventComponent == &name || event.eventComponent == this) && onSelect) { onSelect(id, event.mods); }
    }
    // Dragging the grip moves the track, or a group with all it holds: the
    // timeline lifts it and slides the other rows aside.
    void mouseDrag(const juce::MouseEvent& event) override {
        if (event.eventComponent != &grip) { return; }
        gripTravel = std::max(gripTravel, event.getDistanceFromDragStart());
        if (gripTravel >= 4 && onReorder) {
            // The hand closes on the track while it is carried.
            if (!grabbing) {
                grabbing = true;
                grip.setMouseCursor(motion::grabbingCursor());
                grip.updateMouseCursor();
            }
            onReorder(id, event.getScreenPosition(), false);
        }
    }
    void mouseUp(const juce::MouseEvent& event) override {
        if (grabbing) {
            grabbing = false;
            grip.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            grip.updateMouseCursor();
        }
        if (event.eventComponent != &grip || gripTravel < 4) { return; }
        // The button has already ignored this release as a click; later
        // clicks (or an accessibility press) open the menu again.
        gripTravel = 0;
        if (onReorder) { onReorder(id, event.getScreenPosition(), true); }
    }
    const motion::Id id;
    std::function<void(motion::Id, std::string)> onRename;
    std::function<void(motion::Id)> onMute, onSolo, onMenu, onCollapse, onLanes, onLock;
    // Cmd adds or removes the track from a selection; Shift takes a run.
    std::function<void(motion::Id, juce::ModifierKeys)> onSelect;
    // The grip's drag, in screen coordinates; `finished` on release.
    std::function<void(motion::Id, juce::Point<int>, bool finished)> onReorder;
private:
    bool isGroup = false, grabbing = false;
    juce::Rectangle<float> folderArea;
    int gripTravel = 0;
    class FoldButton : public juce::TextButton {
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
            // Only while the row is under the pointer: a column of grips is noise.
            const auto* row = getParentComponent();
            if (!over && !down && (row == nullptr || !row->isMouseOver(true))) { return; }
            g.setColour(osci::Colours::text().withAlpha(over || down ? 0.9f : 0.4f));
            for (int row = -1; row <= 1; ++row) {
                for (int column = 0; column < 2; ++column) {
                    g.fillEllipse(getWidth() * 0.5f - 4 + column * 5, getHeight() * 0.5f - 1 + row * 5, 2, 2);
                }
            }
        }
    } grip;
    motion::ui::Chip lock {"Lock", motion::icons::Icon::lock};
    motion::ui::Chip mute {"M"}, solo {"S"};
};
