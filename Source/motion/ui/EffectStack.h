#pragma once

#include "MotionStyle.h"
#include "PreviewGesture.h"
#include "DocumentMenu.h"
#include "MotionIcons.h"
#include "ScrubField.h"
#include "Chip.h"
#include "FormControls.h"
#include "KeyframeButton.h"
#include "PropertyRow.h"

#include "../MotionProcessor.h"
#include "../model/PropertySchema.h"
#include "../model/PropertyTarget.h"
#include <set>

// The effects of one owner (a clip, track, group or the composition), shown
// in Properties under the owner's own settings. Each effect is a section of
// its own, unboxed: a hairline, a header that folds it (a switch, its name,
// and a remove button shown while hovered), then its parameters as
// Properties' own rows (value, modulation, keys).
// Effects arrive by dragging them from the Effects library; cards reorder by
// dragging their header. Other stages with effects are listed as chips.
class MotionEffectStack final : public juce::Component, public juce::DragAndDropTarget {
public:
    explicit MotionEffectStack(MotionProcessor& ownerProcessor) : processor(ownerProcessor) { setName("Effects"); }
    ~MotionEffectStack() override { cancelGesture(); }

    // Selecting a parameter (to graph it) and jumping to another stage.
    std::function<void(motion::Id, std::string)> onPropertySelected;
    // A parameter's modulate glyph: its curve, opened in the Graph.
    std::function<void(motion::Id, const std::string&)> onModulate;
    std::function<void(motion::Id)> onShowOwner;
    std::function<void()> onHeightChanged;
    // A newly added effect asks to be scrolled into view.
    std::function<void(juce::Component&)> onReveal;

    // `value` is a clip, track or group id, or 0 for the composition;
    // `clip` is the selection whose other stages are listed.
    void setOwner(std::optional<motion::Id> value, motion::Id clip);
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
    // What an added effect goes on: none for a camera, the Scope or audio.
    std::optional<motion::Id> effectOwner() const { return owner; }

private:
    static constexpr int chipHeight = 22, dropZoneHeight = 36;

    // One effect: header (on/off, name, close) and a row per parameter.
    class Card final : public juce::Component {
    public:
        Card(MotionEffectStack& owner, motion::Id effectId);
        ~Card() override { removeMouseListener(&hover); }
        int preferredHeight() const {
            if (folded()) { return headerHeight; }
            // Its rows, then as much below the last as between two.
            return headerHeight + static_cast<int>(rows.size()) * (motion::style::controlHeight + motion::style::gap) + motion::style::gap;
        }
        motion::Id getEffectId() const { return id; }
        // Between one card's outline and the next.
        static constexpr int spacing = motion::style::padding;
        void update();
        void resized() override;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;
        void mouseEnter(const juce::MouseEvent&) override { repaint(); }
        void mouseExit(const juce::MouseEvent&) override { repaint(); }
    private:
        static constexpr int headerHeight = 28;
        // The header and rows sit this far inside the card's outline.
        static constexpr int inset = motion::style::padding;
        bool folded() const { return stack.folded.contains(id); }
        void jump(const std::string& property, bool forward);
        // A parameter, drawn as a property's row: its name, muted, then its
        // value in the grid's last column, beside its keys.
        struct Row final : motion::ui::PropertyRow {
            std::string id;
            juce::String label;
            motion::ui::ScrubField field;
            bool dimmed = false; // its effect is off
            void paint(juce::Graphics& g) override;
            void resized() override;
        };
        // The pointer over the effect or anything in it shows its remove button.
        struct Hover final : juce::MouseListener {
            explicit Hover(Card& owner) : card(owner) {}
            void mouseEnter(const juce::MouseEvent&) override { card.showClose(); }
            void mouseExit(const juce::MouseEvent&) override { card.showClose(); }
            Card& card;
        };
        void showClose();
        MotionEffectStack& stack;
        motion::Id id;
        juce::String name;
        bool enabled = true;
        juce::Rectangle<float> fold;
        motion::ui::Switch power {"Effect on"};
        motion::icons::Button close {"Remove effect", motion::icons::Icon::close};
        std::vector<std::unique_ptr<Row>> rows;
        Hover hover {*this};
    };
    // Cards folded to their header (view state, kept while the editor is open).
    std::set<motion::Id> folded;
    // Driven by a route or a link, like Properties' lilac fields.
    bool isDriven(motion::Id effect, const std::string& property) const;

    // Chips for the other stages of the selection that have effects.
    void refreshStages();
    float cardTarget(int index) const;
    void stepCards();
    void settleCards();
    int insertionIndex(int y) const;
    double frameTime() const { return processor.document.project().frameTime(processor.position.load()); }
    // Whether an effect, or what owns effects, is on a locked track: shown, not edited.
    bool locked(motion::Id id) const;
    void setEnabled(motion::Id id, bool value);
    void showMenu(motion::Id id);
    void remove(motion::Id id);
    // Moves an effect to insertion point `index` here, possibly from another stage.
    void move(motion::Id id, int index);
    void toggleKey(motion::Id id, const std::string& property);
    void beginGesture(motion::Id id, const std::string& property);
    void setValue(motion::Id id, const std::string& property, double value);
    // A value typed or stepped in: never part of a scrub, so one Escaped
    // before doesn't swallow it.
    void commitValue(motion::Id id, const std::string& property, double value);
    void finishGesture();
    void cancelGesture();

    MotionProcessor& processor;
    std::optional<motion::Id> owner;
    motion::Id context = 0;
    std::vector<motion::Id> listed;
    // Whose effects `listed` holds, so a new selection never reads as an added effect.
    std::optional<motion::Id> listedOwner;
    std::vector<std::unique_ptr<Card>> cards;
    std::vector<std::pair<juce::String, motion::Id>> listedStages;
    std::vector<std::unique_ptr<motion::ui::Chip>> stages;
    motion::ui::PreviewGesture gesture {processor.document};
    double gestureTime = 0;
    std::vector<float> cardShift;
    int dragged = -1, gapIndex = -1, gapHeight = 0;
    juce::TimedCallback cardAnimation {[this] { stepCards(); }};
    juce::Rectangle<int> dropZone, stagesCaption;
    bool cancelledGesture = false, dragActive = false, dropHover = false;
};
