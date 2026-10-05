#pragma once

#include "MotionStyle.h"
#include "DocumentMenu.h"
#include "MotionIcons.h"
#include "ScrubField.h"

#include "../MotionProcessor.h"
#include "../model/PropertySchema.h"
#include "../model/PropertyTarget.h"

// The effects of one owner (a clip, track, group or the composition), shown
// in Properties under the owner's own settings. Each effect is a card with an
// on/off box, its parameters as scrub fields with keys, and a close button.
// Effects arrive by dragging them from the Effects library; cards reorder by
// dragging their header. Other stages with effects are listed as chips.
class MotionEffectStack final : public juce::Component, public juce::DragAndDropTarget {
public:
    explicit MotionEffectStack(MotionProcessor& ownerProcessor) : processor(ownerProcessor) { setName("Effects"); }
    ~MotionEffectStack() override { cancelGesture(); }

    // Selecting a parameter (to graph it) and jumping to another stage.
    std::function<void(motion::Id, std::string)> onPropertySelected;
    std::function<void(motion::Id)> onShowOwner;
    std::function<void()> onHeightChanged;
    // A newly added effect asks to be scrolled into view.
    std::function<void(juce::Component&)> onReveal;

    // `value` is a clip, track or group id, or 0 for the composition;
    // `clip` is the selection whose other stages are listed.
    void setOwner(std::optional<motion::Id> value, motion::Id clip);
    std::optional<motion::Id> getOwner() const { return owner; }
    void setDragActive(bool active);

    int preferredHeight() const;

    void refresh();
    void updateValues() {
        for (auto& card : cards) { card->update(); }
    }

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Effects from the library are added here; cards are moved within.
    bool isInterestedInDragSource(const SourceDetails& details) override;
    // While something is dragged over the cards they part to open its place;
    // a card being moved leaves its own slot empty.
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails& details) override;
    // Adds an effect of `type` at insertion point `index` (or last).
    void addEffect(const std::string& type, int index = -1);

private:
    static constexpr int headingHeight = 20, chipHeight = 22, dropZoneHeight = 36;
    struct Gesture { motion::Project before; std::uint64_t revision; motion::Id effect; std::string property; double time; bool changed = false; };

    // One effect: header (on/off, name, close) and a row per parameter.
    class Card final : public juce::Component {
    public:
        Card(MotionEffectStack& owner, motion::Id effectId);
        int preferredHeight() const { return headerHeight + static_cast<int>(rows.size()) * (motion::style::controlHeight + 2) + motion::style::gap; }
        motion::Id getEffectId() const { return id; }
        void update();
        void resized() override;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
    private:
        static constexpr int headerHeight = 28, labelWidth = 92;
        struct Row { std::string id; juce::String label; MotionScrubField field; osci::KeyframeButton key; };
        MotionEffectStack& stack;
        motion::Id id;
        juce::String name;
        bool enabled = true;
        juce::Rectangle<float> box;
        osci::CloseButton close {"Remove effect"};
        std::vector<std::unique_ptr<Row>> rows;
    };

    // Chips for the other stages of the selection that have effects.
    void refreshStages();
    float cardTarget(int index) const;
    void stepCards();
    void settleCards();
    int insertionIndex(int y) const;
    double frameTime() const { return processor.document.project().frameTime(processor.position.load()); }
    void setEnabled(motion::Id id, bool value);
    void showMenu(motion::Id id);
    void remove(motion::Id id);
    // Moves an effect to insertion point `index` here, possibly from another stage.
    void move(motion::Id id, int index);
    static std::vector<std::vector<motion::EffectInstance>*> allOwners(motion::Project& project);
    void toggleKey(motion::Id id, const std::string& property);
    void beginGesture(motion::Id id, const std::string& property);
    void setValue(motion::Id id, const std::string& property, double value);
    void finishGesture();
    void cancelGesture();

    MotionProcessor& processor;
    std::optional<motion::Id> owner;
    motion::Id context = 0;
    std::vector<motion::Id> listed;
    std::vector<std::unique_ptr<Card>> cards;
    std::vector<std::pair<juce::String, motion::Id>> listedStages;
    std::vector<std::unique_ptr<juce::TextButton>> stages;
    std::optional<Gesture> gesture;
    std::vector<float> cardShift;
    int dragged = -1, gapIndex = -1, gapHeight = 0;
    juce::TimedCallback cardAnimation {[this] { stepCards(); }};
    juce::Rectangle<int> dropZone;
    bool cancelledGesture = false, dragActive = false, dropHover = false;
};
