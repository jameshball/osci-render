#pragma once

#include "../MotionProcessor.h"
#include "FormControls.h"
#include "MotionStyle.h"
#include "SourceKind.h"
#include <functional>
#include <optional>

// What a 3D object's size choice does to it, in its own numbers: fitted
// (centred, scaled to fill the screen by `scale`) or at its file's size.
inline juce::String motionModelSizeNote(bool original, double scale) {
    if (original) { return "At its file's own size and place, unscaled."; }
    const auto times = juce::String::fromUTF8("\xc3\x97");
    const auto factor = scale >= 10 ? juce::String(scale, 0) : scale >= 1 ? juce::String(scale, 1)
        : juce::String(scale, std::clamp(1 - static_cast<int>(std::floor(std::log10(std::max(scale, 1.0e-9)))), 2, 6));
    return "Centred and scaled " + times + factor + " to fill the screen.";
}

// A 3D object clip's size, under its timing in Properties: fitted to the
// screen or kept at its file's own size, and what that amounts to. It
// belongs to the source, so every clip of it follows; a change prepares the
// source again.
class MotionModelSizeInspector final : public juce::Component {
public:
    explicit MotionModelSizeInspector(MotionProcessor& owner) : processor(owner) {
        setName("3D object size inspector");
        motion::style::inspector::styleHeading(title, "3D object");
        addAndMakeVisible(title);
        size.setTooltip("Fit to screen centres the object and scales it to fill the screen; Original keeps its file's own size and place");
        size.onChange = [this](int index) {
            const auto* asset = modelAsset();
            const auto original = index == 1;
            if (asset == nullptr || isLocked() || current() == original) {
                refresh();
                return;
            }
            pending = std::make_pair(asset->id, original);
            if (onChange) { onChange(asset->id, original); }
            refresh();
        };
        addAndMakeVisible(size);
        motion::style::inspector::styleCaption(note, {});
        note.setJustificationType(juce::Justification::centredLeft);
        note.setBorderSize({});
        addAndMakeVisible(note);
    }

    std::function<void(motion::Id, bool original)> onChange;
    std::function<void()> onHeightChanged;

    void setSelection(motion::Id clip) {
        selected = clip;
        refresh();
    }
    int preferredHeight() const {
        return modelAsset() != nullptr ? motion::style::inspector::headingHeight + motion::style::inspector::row + noteHeight + 8 : 0;
    }
    void refresh() {
        const auto previous = lastHeight;
        const auto* asset = modelAsset();
        if (asset != nullptr && pending.has_value() && pending->first == asset->id && asset->originalSize == pending->second) { pending.reset(); }
        const auto original = current();
        size.setSelected(original ? 1 : 0);
        size.setEnabled(asset != nullptr && !isLocked());
        note.setText(asset != nullptr ? motionModelSizeNote(original, asset->fittedScale) : juce::String(), juce::dontSendNotification);
        lastHeight = preferredHeight();
        resized();
        if (lastHeight != previous && onHeightChanged) { onHeightChanged(); }
    }
    void resized() override {
        auto area = getLocalBounds();
        title.setBounds(area.removeFromTop(motion::style::inspector::headingHeight));
        // The choice ends with Properties' value columns, as the fields below do.
        const motion::style::PropertyGrid grid(area.getWidth());
        size.setBounds(area.removeFromTop(motion::style::inspector::row).reduced(0, 3).withWidth(grid.value + grid.column));
        note.setBounds(area.removeFromTop(noteHeight).withWidth(grid.value + grid.column));
    }

private:
    static constexpr int noteHeight = 18;
    const motion::Asset* modelAsset() const {
        const auto& project = processor.document.project();
        const auto* clip = motion::findClip(project, selected);
        const auto asset = clip != nullptr ? motion::findAsset(project.assets, clip->asset) : nullptr;
        return asset != nullptr && motion::ui::isModel(*asset) ? asset.get() : nullptr;
    }
    bool isLocked() const {
        const auto* track = motion::findClipTrack(processor.document.project(), selected);
        return track != nullptr && track->locked;
    }
    // A change being prepared shows here until the source has it.
    bool current() const {
        const auto* asset = modelAsset();
        if (asset == nullptr) { return false; }
        return pending.has_value() && pending->first == asset->id ? pending->second : asset->originalSize;
    }

    MotionProcessor& processor;
    motion::Id selected = 0;
    std::optional<std::pair<motion::Id, bool>> pending;
    int lastHeight = 0;
    juce::Label title, note;
    motion::ui::SegmentedControl size {"3D object size", {"Fit to screen", "Original"}};
};
