#pragma once

#include "../MotionProcessor.h"
#include "Chip.h"
#include "MotionStyle.h"
#include "ScrubField.h"
#include <array>
#include <functional>

// A text clip's per-character animation, under its timing in Properties.
// The settings belong to the text source, so every clip of it follows; a
// change prepares the source again.
class MotionTextAnimationPanel final : public juce::Component {
public:
    explicit MotionTextAnimationPanel(MotionProcessor& owner) : processor(owner) {
        setName("Text animation inspector");
        title.setText("Characters", juce::dontSendNotification);
        title.setFont(motion::style::caption());
        title.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        title.setBorderSize({0, 2, 0, 0});
        addAndMakeVisible(title);
        const std::array<const char*, 6> chipTips {"Every character shows at once", "Characters appear one by one", "Characters rise into place", "Characters pop in with a small overshoot", "Characters bob in a looping wave", "Characters fly in from all around"};
        for (std::size_t index = 0; index < kinds.size(); ++index) {
            auto& chip = *kinds[index];
            chip.setName("Text animation " + chip.getButtonText());
            chip.setTitle(chip.getName());
            chip.setTooltip(chipTips[index]);
            chip.setClickingTogglesState(false);
            chip.onClick = [this, index] {
                auto next = current();
                next.animation = static_cast<motion::TextSettings::Animation>(index);
                apply(next);
            };
            addAndMakeVisible(chip);
        }
        const std::array<const char*, 4> labels {"Stagger", "Each", "Hold", "Amount"};
        const std::array<const char*, 4> fieldTips {"Seconds between one character starting and the next", "Seconds each character takes to arrive", "Seconds the finished text holds before the source loops", "How far characters travel (1 is one character height)"};
        for (std::size_t index = 0; index < fields.size(); ++index) {
            captions[index].setText(labels[index], juce::dontSendNotification);
            captions[index].setFont(motion::style::caption());
            captions[index].setColour(juce::Label::textColourId, osci::Colours::textMuted());
            addAndMakeVisible(captions[index]);
            auto& field = fields[index];
            field.setName(juce::String("Text animation ") + labels[index]);
            field.setTitle(field.getName());
            field.setSpec(specs[index]);
            field.setTooltip(fieldTips[index]);
            // A drag previews its number; the source prepares once, on release.
            field.onCommit = [this](double) { applyFields(); };
            field.onEnd = [this] { applyFields(); };
            addAndMakeVisible(field);
        }
    }

    std::function<void(motion::Id, motion::TextSettings)> onApply;
    std::function<void()> onHeightChanged;

    // A change being prepared shows here until the source has it.
    void clearPending() { pending.reset(); refresh(); }
    void setSelection(motion::Id clip) {
        selected = clip;
        refresh();
    }
    int preferredHeight() const {
        const auto* asset = textAsset();
        if (asset == nullptr) { return 0; }
        return 16 + 30 + (current().animated() ? 2 * rowHeight : 0) + 10;
    }
    void refresh() {
        const auto previous = lastHeight;
        const auto* asset = textAsset();
        if (asset != nullptr && pending.has_value() && pending->first == asset->id && sameAnimation(asset->textSettings, pending->second)) { pending.reset(); }
        const auto settings = current();
        const auto locked = isLocked();
        for (std::size_t index = 0; index < kinds.size(); ++index) {
            kinds[index]->setToggleState(static_cast<int>(settings.animation) == static_cast<int>(index), juce::dontSendNotification);
            kinds[index]->setEnabled(!locked);
        }
        const std::array<double, 4> values {settings.characterDelay, settings.characterDuration, settings.hold, settings.amount};
        for (std::size_t index = 0; index < fields.size(); ++index) {
            fields[index].setValue(values[index]);
            fields[index].setEnabled(!locked);
        }
        lastHeight = preferredHeight();
        resized();
        if (lastHeight != previous && onHeightChanged) { onHeightChanged(); }
    }
    void resized() override {
        auto area = getLocalBounds();
        title.setBounds(area.removeFromTop(16));
        // Six kinds share one row, edge to edge.
        auto chips = area.removeFromTop(30).withTrimmedTop(3).withTrimmedBottom(3);
        const auto each = (chips.getWidth() - 4 * static_cast<int>(kinds.size() - 1)) / static_cast<int>(kinds.size());
        for (std::size_t index = 0; index < kinds.size(); ++index) {
            kinds[index]->setBounds(index + 1 == kinds.size() ? chips : chips.removeFromLeft(each));
            chips.removeFromLeft(4);
        }
        const auto* asset = textAsset();
        const auto animated = asset != nullptr && current().animated();
        // The same two-column rows as the clip's timing.
        for (std::size_t line = 0; line < 2; ++line) {
            auto row = animated ? area.removeFromTop(rowHeight) : juce::Rectangle<int>();
            const auto half = row.getWidth() / 2;
            for (std::size_t column = 0; column < 2; ++column) {
                const auto index = line * 2 + column;
                auto cell = row.removeFromLeft(half).withTrimmedRight(column == 0 ? 6 : 0);
                captions[index].setVisible(animated);
                fields[index].setVisible(animated);
                captions[index].setBounds(cell.removeFromLeft(56));
                fields[index].setBounds(cell.reduced(0, 3));
            }
        }
    }

private:
    static constexpr int rowHeight = 28;
    const motion::Asset* textAsset() const {
        const auto& project = processor.document.project();
        const auto* clip = motion::findClip(project, selected);
        const auto asset = clip != nullptr ? motion::findAsset(project.assets, clip->asset) : nullptr;
        return asset != nullptr && asset->extension.equalsIgnoreCase(".txt") ? asset.get() : nullptr;
    }
    bool isLocked() const {
        const auto* track = motion::findClipTrack(processor.document.project(), selected);
        return track != nullptr && track->locked;
    }
    motion::TextSettings current() const {
        const auto* asset = textAsset();
        if (asset == nullptr) { return {}; }
        return pending.has_value() && pending->first == asset->id ? pending->second : asset->textSettings;
    }
    static bool sameAnimation(const motion::TextSettings& a, const motion::TextSettings& b) {
        return a.animation == b.animation && a.characterDelay == b.characterDelay && a.characterDuration == b.characterDuration && a.hold == b.hold && a.amount == b.amount;
    }
    void applyFields() {
        auto next = current();
        next.characterDelay = fields[0].getValue();
        next.characterDuration = fields[1].getValue();
        next.hold = fields[2].getValue();
        next.amount = fields[3].getValue();
        apply(next);
    }
    void apply(const motion::TextSettings& next) {
        const auto* asset = textAsset();
        if (asset == nullptr || isLocked() || sameAnimation(next, current()) || next.validate().isNotEmpty()) { refresh(); return; }
        pending = std::make_pair(asset->id, next);
        if (onApply) { onApply(asset->id, next); }
        refresh();
    }

    static constexpr std::array<motion::PropertySpec, 4> specs {{
        {"text.characterDelay", "Stagger", "Characters", "", 0, 30, 0.06, .01, 2, "s"},
        {"text.characterDuration", "Each", "Characters", "", 0.01, 30, 0.35, .01, 2, "s"},
        {"text.hold", "Hold", "Characters", "", 0, 30, 1.5, .1, 1, "s"},
        {"text.amount", "Amount", "Characters", "", -10, 10, 1, .05, 2, ""}}};

    MotionProcessor& processor;
    motion::Id selected = 0;
    int lastHeight = 0;
    std::optional<std::pair<motion::Id, motion::TextSettings>> pending;
    juce::Label title;
    motion::ui::Chip none {"None"}, typeOn {"Type"}, rise {"Rise"}, pop {"Pop"}, wave {"Wave"}, scatter {"Scatter"};
    std::array<motion::ui::Chip*, 6> kinds {&none, &typeOn, &rise, &pop, &wave, &scatter};
    std::array<juce::Label, 4> captions;
    std::array<motion::ui::ScrubField, 4> fields;
};
