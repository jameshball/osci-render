#pragma once

#include "../MotionProcessor.h"
#include <array>
#include <cstdlib>

// Resolved seconds are explicit here even when the timeline ruler shows beats.
// The document converts musical clips back into their canonical beat domain.
class MotionClipTimingPanel : public juce::Component {
public:
    explicit MotionClipTimingPanel(MotionProcessor& owner) : processor(owner) {
        setName("Clip timing inspector");
        title.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        addAndMakeVisible(title);
        const std::array<const char*, 4> labels {"Start (s)", "Duration (s)", "Source offset (s)", "Speed"};
        const std::array<const char*, 4> names {"Clip start", "Clip duration", "Clip source offset", "Clip speed"};
        const std::array<const char*, 4> hints {
            "Move the clip without changing its source offset or animation.",
            "Change the right edge without stretching the source or animation.",
            "Slip the source and its animation inside the existing clip.",
            "Playback multiplier. 1 is normal speed; 2 is twice as fast. Clip duration stays unchanged."
        };
        for (std::size_t i = 0; i < values.size(); ++i) {
            captions[i].setText(labels[i], juce::dontSendNotification);
            captions[i].setFont(12.0f);
            addAndMakeVisible(captions[i]);
            auto& value = values[i];
            value.setName(names[i]); value.setTitle(names[i]);
            value.setEditable(false, true);
            value.setJustificationType(juce::Justification::centredRight);
            value.setFont(juce::FontOptions(13.0f));
            value.setColour(juce::Label::backgroundColourId, osci::Colours::veryDark());
            value.setTooltip(hints[i]);
            value.onEditorShow = [this] { editRevision = processor.document.revision(); editGeneration = processor.document.generation(); };
            value.onTextChange = [this, i] { if (!updating) { apply(i); } };
            addAndMakeVisible(value);
        }
        details.setFont(12.0f);
        details.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(details);
        status.setFont(12.0f);
        status.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(status);
        refresh();
    }
    void setSelection(motion::Id id) {
        if (id != selected) { discardEditors(); selected = id; error.clear(); }
        refresh();
    }
    void refresh() {
        if (editGeneration != processor.document.generation() || editRevision != processor.document.revision()) { discardEditors(); }
        const auto* clip = findClip();
        const bool locked = isLocked();
        title.setText(clip != nullptr ? juce::String(clip->name) : "Clip timing", juce::dontSendNotification);
        updating = true;
        const auto timing = clip != nullptr ? clip->timing(processor.document.project().bpm) : motion::ClipTiming();
        const std::array<double, 4> current {timing.start, timing.duration(), timing.offset, timing.rate};
        for (std::size_t i = 0; i < values.size(); ++i) {
            captions[i].setVisible(clip != nullptr);
            values[i].setVisible(clip != nullptr);
            values[i].setEnabled(clip != nullptr && !locked);
            if (!values[i].isBeingEdited()) { values[i].setText(juce::String(current[i], 6), juce::dontSendNotification); }
        }
        updating = false;
        details.setText(clip != nullptr ? "Ends at " + juce::String(timing.end(), 3) + " s\n"
            + (clip->timeBase == motion::ClipTimeBase::beats ? "Beat anchored - follows project tempo." : "Time anchored - keeps its position in seconds.")
            : "Select a clip in the timeline to edit its timing.", juce::dontSendNotification);
        status.setColour(juce::Label::textColourId, error.isNotEmpty() ? juce::Colours::orange : osci::Colours::text().withAlpha(.6f));
        status.setText(error.isNotEmpty() ? error : locked ? "Track locked. Timing is read-only." : "Times are in seconds. Edits preserve existing keyframes and notes.", juce::dontSendNotification);
        status.setVisible(clip != nullptr || error.isNotEmpty());
        resized();
    }
    void resized() override {
        auto area = getLocalBounds().reduced(10, 0);
        title.setBounds(area.removeFromTop(36));
        for (std::size_t i = 0; i < values.size(); ++i) {
            auto row = area.removeFromTop(38);
            captions[i].setBounds(row.removeFromLeft(108));
            values[i].setBounds(row.reduced(2, 6));
        }
        area.removeFromTop(12);
        details.setBounds(area.removeFromTop(55));
        area.removeFromTop(8);
        status.setBounds(area.removeFromTop(70));
    }
private:
    const motion::Clip* findClip() const {
        for (const auto& track : processor.document.project().tracks) {
            for (const auto& clip : track.clips) { if (clip.id == selected) { return &clip; } }
        }
        return nullptr;
    }
    bool isLocked() const {
        for (const auto& track : processor.document.project().tracks) {
            for (const auto& clip : track.clips) { if (clip.id == selected) { return track.locked; } }
        }
        return false;
    }
    void discardEditors() {
        for (auto& value : values) { if (value.isBeingEdited()) { value.hideEditor(true); } }
    }
    void apply(std::size_t index) {
        const auto* clip = findClip();
        if (clip == nullptr || isLocked() || editGeneration != processor.document.generation() || editRevision != processor.document.revision()) { refresh(); return; }
        const auto text = values[index].getText().trim();
        char* end = nullptr;
        const auto value = std::strtod(text.toRawUTF8(), &end);
        if (text.isEmpty() || end == text.toRawUTF8() || *end != '\0' || !std::isfinite(value)) {
            error = "Enter a finite number."; refresh(); return;
        }
        auto timing = clip->timing(processor.document.project().bpm);
        if (index == 0) { timing.moveTo(value); }
        else if (index == 1) { timing.setDuration(value); }
        else if (index == 2) { timing.offset = value; }
        else { timing.rate = value; }
        const auto result = processor.document.setClipTiming(selected, timing);
        error = result.failed() ? result.getErrorMessage() : juce::String();
        refresh();
    }
    MotionProcessor& processor;
    motion::Id selected = 0;
    std::uint64_t editRevision = 0, editGeneration = 0;
    bool updating = false;
    std::array<juce::Label, 4> captions, values;
    juce::Label title, details, status;
    juce::String error;
};
