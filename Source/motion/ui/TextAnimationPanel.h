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
        motion::style::inspector::styleHeading(title, "Text animation");
        addAndMakeVisible(title);
        kinds.setName("Text animation");
        kinds.setTitle("Text animation");
        const std::array<const char*, 6> kindNames {"None", "Typewriter", "Rise in", "Pop in", "Wave", "Scatter in"};
        for (std::size_t index = 0; index < kindNames.size(); ++index) { kinds.addItem(kindNames[index], static_cast<int>(index) + 1); }
        kinds.setJustificationType(juce::Justification::centredRight);
        kinds.setTooltip("How the letters come in, one after another: Typewriter shows each in turn, Rise in lifts them into place, Pop in grows them with a bounce, Scatter in flies them in from all around. Wave bobs them up and down, over and over.");
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
        for (std::size_t index = 0; index < fields.size(); ++index) {
            motion::style::inspector::styleCaption(captions[index], {});
            addAndMakeVisible(captions[index]);
            auto& field = fields[index];
            field.setSpec(specs[index]);
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
        const auto shown = static_cast<int>(std::count_if(namings[kindIndex()].begin(), namings[kindIndex()].end(), [](const Naming& naming) { return naming.label != nullptr; }));
        return motion::style::inspector::headingHeight + motion::style::inspector::row + (shown + 1) / 2 * motion::style::inspector::row + 10;
    }
    void refresh() {
        const auto previous = lastHeight;
        const auto* asset = textAsset();
        if (asset != nullptr && pending.has_value() && pending->first == asset->id && sameAnimation(asset->textSettings, pending->second)) { pending.reset(); }
        const auto settings = current();
        const auto locked = isLocked();
        kinds.setSelectedId(static_cast<int>(settings.animation) + 1, juce::dontSendNotification);
        kinds.setEnabled(!locked);
        // Each setting named for what it does in this animation.
        for (std::size_t index = 0; index < fields.size(); ++index) {
            const auto& naming = namings[kindIndex()][index];
            if (naming.label == nullptr) { continue; }
            captions[index].setText(naming.label, juce::dontSendNotification);
            auto spec = specs[index];
            spec.label = naming.label;
            fields[index].setSpec(spec);
            fields[index].setName(juce::String("Text animation ") + naming.label);
            fields[index].setTitle(fields[index].getName());
            fields[index].setTooltip(naming.tip);
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
        title.setBounds(area.removeFromTop(motion::style::inspector::headingHeight));
        // The kind, captioned inside like the fields below, ending with the
        // value columns.
        const motion::style::PropertyGrid grid(area.getWidth());
        kinds.setBounds(area.removeFromTop(motion::style::inspector::row).reduced(0, 3).withWidth(grid.value + grid.column));
        kindCaption.setBounds(kinds.getBounds().withTrimmedLeft(6).withWidth(60));
        const auto* asset = textAsset();
        for (std::size_t index = 0; index < fields.size(); ++index) {
            const auto shown = asset != nullptr && namings[kindIndex()][index].label != nullptr;
            captions[index].setVisible(shown);
            fields[index].setVisible(shown);
        }
        motion::style::inspector::layoutFields(area, captions, {&fields[0], &fields[1], &fields[2], &fields[3]});
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
        {"text.characterDelay", "Stagger", "Text animation", "", 0, 30, 0.06, .01, 2, "s"},
        {"text.characterDuration", "Rise time", "Text animation", "", 0.01, 30, 0.35, .01, 2, "s"},
        {"text.hold", "Pause", "Text animation", "", 0, 30, 1.5, .1, 1, "s"},
        {"text.amount", "Distance", "Text animation", "", -10, 10, 1, .05, 2, ""}}};
    // What each setting is called, and does, in each animation; a setting an
    // animation does not use is not shown.
    struct Naming {
        const char* label;
        const char* tip;
    };
    static constexpr const char* staggerTip = "Seconds between one letter starting and the next";
    static constexpr const char* pauseTip = "Seconds the finished text stays before it plays again";
    static constexpr std::array<std::array<Naming, 4>, 6> namings {{
        {{{nullptr, nullptr}, {nullptr, nullptr}, {nullptr, nullptr}, {nullptr, nullptr}}},
        {{{"Stagger", "Seconds between one letter appearing and the next"}, {nullptr, nullptr}, {"Pause", pauseTip}, {nullptr, nullptr}}},
        {{{"Stagger", staggerTip}, {"Rise time", "Seconds each letter takes to rise into place"}, {"Pause", pauseTip}, {"Distance", "How far below its place each letter starts (1 is a letter's height)"}}},
        {{{"Stagger", staggerTip}, {"Pop time", "Seconds each letter takes to grow to size"}, {"Pause", pauseTip}, {"Bounce", "How far each letter overshoots its size before settling (0 for none)"}}},
        {{{"Stagger", "Seconds each letter's bob trails the one before"}, {"Period", "Seconds for one bob up and down"}, {nullptr, nullptr}, {"Height", "How high the letters bob (1 is a quarter of a letter's height)"}}},
        {{{"Stagger", staggerTip}, {"Fly-in time", "Seconds each letter takes to fly into place"}, {"Pause", pauseTip}, {"Spread", "How far away, and how far turned, each letter starts (1 is two letters' heights and half a turn)"}}},
    }};
    std::size_t kindIndex() const { return static_cast<std::size_t>(std::clamp(static_cast<int>(current().animation), 0, 5)); }

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
