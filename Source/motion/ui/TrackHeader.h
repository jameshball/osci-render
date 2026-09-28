#pragma once

#include "../model/Timeline.h"
#include "MotionStyle.h"

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
        mute.setOnColour(juce::Colour(0xff8b6434));
        solo.setOnColour(juce::Colour(0xff347b52));
        lock.setOnColour(juce::Colour(0xff5c5f6b));
        grip.setTooltip("Drag to reorder; click for track actions");
        grip.onClick = [this] { if (onMenu) { onMenu(id); } };
        grip.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        grip.addMouseListener(this, false);
        for (auto* child : std::initializer_list<juce::Component*> { &name, &mute, &solo, &grip }) { addAndMakeVisible(child); }
        addChildComponent(lock);
    }
    void update(const motion::Track& track, bool group = false, bool collapsed = false, bool lanes = false, bool expanded = false) {
        isGroup = group;
        disclosure.setVisible(group || lanes);
        disclosure.setToggleState(group ? collapsed : !expanded, juce::dontSendNotification);
        lock.setVisible(!group);
        grip.setMouseCursor(group ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor);
        grip.setTooltip(group ? "Group actions" : "Drag to reorder; click for track actions");
        disclosure.setTitle(group ? "Fold group " + juce::String(id) : "Keyframe lanes " + juce::String(id));
        disclosure.setTooltip(group ? "Fold or unfold this group" : "Show or hide keyframe lanes");
        name.setFont(group ? motion::style::strong() : motion::style::body());
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
    }
    // One compact line: [fold/lanes] [grip] name ... [M][S][L]
    void resized() override {
        auto bounds = getLocalBounds().reduced(3, 0);
        disclosure.setBounds(bounds.removeFromLeft(16));
        grip.setBounds(bounds.removeFromLeft(14));
        auto buttons = bounds.removeFromRight(isGroup ? 38 : 57).withSizeKeepingCentre(isGroup ? 38 : 57, 18);
        for (auto* button : { &mute, &solo, &lock }) {
            if (!button->isVisible()) { continue; }
            button->setBounds(buttons.removeFromLeft(18));
            buttons.removeFromLeft(1);
        }
        name.setBounds(bounds.reduced(2, 0).withSizeKeepingCentre(bounds.getWidth() - 4, 18));
    }
    void mouseDown(const juce::MouseEvent& event) override {
        if (event.eventComponent == &name && onSelect) { onSelect(id); }
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        if (isGroup || event.eventComponent != &grip || event.getDistanceFromDragStart() < 4) { return; }
        auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
        if (container != nullptr && !container->isDragAndDropActive()) {
            container->startDragging("motion-track:" + juce::String(id) + ":" + juce::String(onDragRevision ? onDragRevision() : 0), &grip);
        }
    }
    const motion::Id id;
    std::function<void(motion::Id, std::string)> onRename;
    std::function<void(motion::Id)> onMute, onSolo, onMenu, onSelect, onCollapse, onLanes, onLock;
    std::function<std::uint64_t()> onDragRevision;
private:
    bool isGroup = false;
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
    motion::style::Chip mute {"M"}, solo {"S"}, lock {"L"};
};
