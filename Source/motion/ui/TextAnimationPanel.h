#pragma once

#include "../MotionProcessor.h"
#include "FormControls.h"
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
        motion::style::inspector::styleHeading(title, "Characters");
        addAndMakeVisible(title);
        kinds.setName("Text animation");
        kinds.setTitle("Text animation");
        const std::array<const char*, 6> kindNames {"None", "Type", "Rise", "Pop", "Wave", "Scatter"};
        for (std::size_t index = 0; index < kindNames.size(); ++index) { kinds.addItem(kindNames[index], static_cast<int>(index) + 1); }
        kinds.setJustificationType(juce::Justification::centredRight);
        kinds.setTooltip("None: every character at once. Type: one by one. Rise: into place. Pop: with a small overshoot. Wave: a looping bob. Scatter: in from all around.");
        motion::style::styleField(kinds);
        kinds.onChange = [this] {
            if (kinds.getSelectedId() <= 0) { return; }
            auto next = current();
            next.animation = static_cast<motion::TextSettings::Animation>(kinds.getSelectedId() - 1);
            apply(next);
        };
        addAndMakeVisible(kinds);
        motion::style::inspector::styleCaption(kindCaption, "Animation");
        kindCaption.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(kindCaption);
        const std::array<const char*, 4> labels {"Stagger", "Each", "Hold", "Amount"};
        const std::array<const char*, 4> fieldTips {"Seconds between one character starting and the next", "Seconds each character takes to arrive", "Seconds the finished text holds before the source loops", "How far characters travel (1 is one character height)"};
        for (std::size_t index = 0; index < fields.size(); ++index) {
            motion::style::inspector::styleCaption(captions[index], labels[index]);
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
        return motion::style::inspector::headingHeight + motion::style::inspector::row + (current().animated() ? 2 * motion::style::inspector::row : 0) + 10;
    }
    void refresh() {
        const auto previous = lastHeight;
        const auto* asset = textAsset();
        if (asset != nullptr && pending.has_value() && pending->first == asset->id && sameAnimation(asset->textSettings, pending->second)) { pending.reset(); }
        const auto settings = current();
        const auto locked = isLocked();
        kinds.setSelectedId(static_cast<int>(settings.animation) + 1, juce::dontSendNotification);
        kinds.setEnabled(!locked);
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
        title.setBounds(area.removeFromTop(motion::style::inspector::headingHeight));
        // The kind, captioned inside like the fields below, ending with the
        // value columns.
        const motion::style::PropertyGrid grid(area.getWidth());
        kinds.setBounds(area.removeFromTop(motion::style::inspector::row).reduced(0, 3).withWidth(grid.value + grid.column));
        kindCaption.setBounds(kinds.getBounds().withTrimmedLeft(6).withWidth(60));
        const auto* asset = textAsset();
        const auto animated = asset != nullptr && current().animated();
        for (std::size_t index = 0; index < fields.size(); ++index) {
            captions[index].setVisible(animated);
            fields[index].setVisible(animated);
        }
        if (animated) { motion::style::inspector::layoutFields(area, captions, {&fields[0], &fields[1], &fields[2], &fields[3]}); }
    }

private:
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
    juce::ComboBox kinds;
    juce::Label kindCaption;
    std::array<juce::Label, 4> captions;
    std::array<motion::ui::ScrubField, 4> fields;
};
