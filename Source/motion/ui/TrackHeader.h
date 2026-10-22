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
        // The glyph matches the M and S letters' height.
        lock.iconSize = 10.0f;
        // The padlock shows only on a locked track or the row under the
        // pointer, which every part of the header reports.
        addMouseListener(&hover, true);
        for (auto* child : std::initializer_list<juce::Component*> { &name, &mute, &solo }) { addAndMakeVisible(child); }
        addChildComponent(lock);
    }
    ~MotionTrackHeader() override { removeMouseListener(&hover); }
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
        name.setTitle(label);
        mute.setTitle("Mute " + label);
        solo.setTitle("Solo " + label);
        lock.setName("Lock track " + juce::String(track.id));
        lock.setTitle("Lock " + label);
        mute.setToggleState(track.muted, juce::dontSendNotification);
        solo.setToggleState(track.solo, juce::dontSendNotification);
        lock.setToggleState(track.locked, juce::dontSendNotification);
        showLock();
    }
    // The composition's own row: its effects, which act on everything. It
    // can only show its key lanes; there is nothing to mute, lock or move.
    void updateComposition(bool lanes, bool expanded) {
        isComposition = true;
        disclosure.setVisible(lanes);
        disclosure.setToggleState(!expanded, juce::dontSendNotification);
        disclosure.setName("Key lanes composition");
        disclosure.setTitle("Key lanes of the composition's effects");
        disclosure.setTooltip("Show or hide the effects' key lanes");
        for (auto* control : std::initializer_list<juce::Component*> {&mute, &solo, &lock}) { control->setVisible(false); }
        name.setEditable(false, false);
        name.setFont(motion::style::body());
        name.setText("Composition", juce::dontSendNotification);
        name.setName("Composition effects");
        name.setTitle("Composition effects");
        name.setTooltip("Effects on everything in the composition: click to show them in Properties");
        resized();
    }
    // While dragged, the timeline draws the header in its lifted block above
    // the other rows; the header itself stays in place, unseen, to keep
    // receiving the drag.
    void setLifted(bool value) { setAlpha(value ? 0.0f : 1.0f); }
    // Where the name's text starts, from the header's left edge: the timeline
    // starts its band captions and lane labels there too.
    static constexpr int edge = 3, foldWidth = 14, namePad = 4, nameInset = edge + foldWidth + namePad;
    // One compact line: [fold/lanes] name ... [M][S][L]
    void resized() override {
        // Controls stay on one top line however tall the row is.
        auto bounds = getLocalBounds().removeFromTop(std::min(getHeight(), 28)).reduced(edge, 0);
        disclosure.setBounds(bounds.removeFromLeft(foldWidth));
        // Three fixed 18 px slots, so every row's switches line up; a group
        // leaves its lock slot empty.
        constexpr int chip = 18, spacing = 1;
        auto buttons = bounds.removeFromRight(3 * (chip + spacing)).withSizeKeepingCentre(3 * (chip + spacing), chip);
        for (auto* button : std::initializer_list<juce::Button*> { &mute, &solo, &lock }) {
            button->setBounds(buttons.removeFromLeft(chip));
            buttons.removeFromLeft(spacing);
        }
        // A group shows a folder before its name; the composition its effects' glyph.
        folderArea = isGroup || isComposition ? bounds.removeFromLeft(18).toFloat() : juce::Rectangle<float>();
        name.setBounds(bounds.reduced(namePad, 0).withSizeKeepingCentre(bounds.getWidth() - 2 * namePad, 18));
    }
    void paintOverChildren(juce::Graphics& g) override {
        if (!folderArea.isEmpty()) { motion::icons::draw(g, isComposition ? motion::icons::Icon::effects : motion::icons::Icon::folder, folderArea, osci::Colours::text().withAlpha(.7f), 14.0f); }
    }
    void mouseDown(const juce::MouseEvent& event) override {
        // Right-click anywhere on the header opens the track's menu.
        if (event.mods.isPopupMenu()) {
            travel = -1;
            if (onMenu) { onMenu(id); }
            return;
        }
        travel = event.mods.isLeftButtonDown() ? 0 : -1;
        // Anywhere on the header but its buttons selects the track or group.
        if (onSelect) { onSelect(id, event.mods); }
    }
    // Dragging the header, by its name or the space around it, moves the
    // track, or a group with all it holds: the timeline lifts it and slides
    // the other rows aside. A press that moves less than 4 px stays a click.
    void mouseDrag(const juce::MouseEvent& event) override {
        if (travel < 0 || !onReorder) { return; }
        travel = std::max(travel, event.getDistanceFromDragStart());
        if (travel < 4) { return; }
        // The hand closes on the track while it is carried.
        if (!carried) { carry(true); }
        onReorder(id, event.getScreenPosition(), false);
    }
    void mouseUp(const juce::MouseEvent& event) override {
        travel = -1;
        if (!carried) { return; }
        carry(false);
        if (onReorder) { onReorder(id, event.getScreenPosition(), true); }
    }
    // Screen readers reach the menu that a right-click opens.
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group, juce::AccessibilityActions().addAction(juce::AccessibilityActionType::showMenu, [this] { if (onMenu) { onMenu(id); } }));
    }
    const motion::Id id;
    std::function<void(motion::Id, std::string)> onRename;
    std::function<void(motion::Id)> onMute, onSolo, onMenu, onCollapse, onLanes, onLock;
    // Cmd adds or removes the track from a selection; Shift takes a run.
    std::function<void(motion::Id, juce::ModifierKeys)> onSelect;
    // The header's drag, in screen coordinates; `finished` on release.
    std::function<void(motion::Id, juce::Point<int>, bool finished)> onReorder;
private:
    bool isGroup = false, isComposition = false, carried = false;
    juce::Rectangle<float> folderArea;
    // How far the pointer has moved since a left press; -1 with none.
    int travel = -1;
    void carry(bool value) {
        carried = value;
        const auto cursor = value ? motion::grabbingCursor() : juce::MouseCursor(juce::MouseCursor::NormalCursor);
        for (auto* part : std::initializer_list<juce::Component*> { this, &name }) {
            part->setMouseCursor(cursor);
            part->updateMouseCursor();
        }
    }
    // Hidden, an unlocked padlock still takes clicks and keeps its slot, so
    // M and S stay in line.
    void showLock() { lock.setAlpha(lock.getToggleState() || isMouseOver(true) ? 1.0f : 0.0f); }
    struct Hover final : juce::MouseListener {
        explicit Hover(MotionTrackHeader& owner) : header(owner) {}
        void mouseEnter(const juce::MouseEvent&) override { header.showLock(); }
        void mouseExit(const juce::MouseEvent&) override { header.showLock(); }
        MotionTrackHeader& header;
    } hover {*this};
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
    motion::ui::Chip lock {"Lock", motion::icons::Icon::lock};
    motion::ui::Chip mute {"M"}, solo {"S"};
};
