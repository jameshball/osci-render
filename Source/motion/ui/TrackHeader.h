#pragma once

#include "../model/Timeline.h"

class MotionTrackHeader : public juce::Component {
public:
    explicit MotionTrackHeader(motion::Id trackId) : id(trackId) {
        setName("Track header");
        name.setEditable(false, true);
        name.setJustificationType(juce::Justification::centredLeft);
        name.setFont(juce::FontOptions(12));
        name.onTextChange = [this] { if (onRename) { onRename(id, name.getText().trim().toStdString()); } };
        mute.setButtonText("M");
        solo.setButtonText("S");
        mute.setTooltip("Mute this track");
        solo.setTooltip("Show only soloed tracks");
        for (auto* button : { &mute, &solo }) { button->setClickingTogglesState(true); }
        mute.onClick = [this] { if (onMute) { onMute(id); } };
        solo.onClick = [this] { if (onSolo) { onSolo(id); } };
        mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff8b6434));
        solo.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff347b52));
        for (auto* button : { &mute, &solo }) { button->setColour(juce::TextButton::textColourOnId, juce::Colours::white); }
        grip.setTooltip("Drag to reorder; click for track actions");
        grip.onClick = [this] { if (onMenu) { onMenu(id); } };
        grip.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        grip.addMouseListener(this, false);
        for (auto* child : std::initializer_list<juce::Component*> { &name, &mute, &solo, &grip }) { addAndMakeVisible(child); }
    }
    void update(const motion::Track& track) {
        if (!name.isBeingEdited()) { name.setText(juce::String(track.name), juce::dontSendNotification); }
        name.setName("Track name " + juce::String(track.id));
        mute.setName("Mute track " + juce::String(track.id));
        solo.setName("Solo track " + juce::String(track.id));
        grip.setName("Reorder track " + juce::String(track.id));
        name.setTitle(name.getName());
        mute.setTitle(mute.getName());
        solo.setTitle(solo.getName());
        grip.setTitle(grip.getName());
        mute.setToggleState(track.muted, juce::dontSendNotification);
        solo.setToggleState(track.solo, juce::dontSendNotification);
    }
    void resized() override {
        auto bounds = getLocalBounds().reduced(3, 1);
        grip.setBounds(bounds.removeFromLeft(18));
        name.setBounds(bounds.removeFromTop(19));
        mute.setBounds(bounds.removeFromLeft(29));
        bounds.removeFromLeft(3);
        solo.setBounds(bounds.removeFromLeft(29));
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        if (event.eventComponent != &grip || event.getDistanceFromDragStart() < 4) { return; }
        auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
        if (container != nullptr && !container->isDragAndDropActive()) {
            container->startDragging("motion-track:" + juce::String(id) + ":" + juce::String(onDragRevision ? onDragRevision() : 0), &grip);
        }
    }
    const motion::Id id;
    std::function<void(motion::Id, std::string)> onRename;
    std::function<void(motion::Id)> onMute, onSolo, onMenu;
    std::function<std::uint64_t()> onDragRevision;
private:
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
    juce::TextButton mute, solo;
};
