#pragma once

#include "../MotionProcessor.h"
#include "MotionStyle.h"
#include <array>
#include <cstdlib>
#include <functional>

// A clip's timing, shown as the first section of the Properties inspector.
// Times use the ruler's format; the document converts musical clips back into
// their canonical beat domain.
class MotionClipTimingPanel : public juce::Component {
public:
    explicit MotionClipTimingPanel(MotionProcessor& owner) : processor(owner) {
        setName("Clip timing inspector");
        title.setText("Timing", juce::dontSendNotification);
        title.setFont(motion::style::caption());
        title.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        title.setBorderSize({0, 2, 0, 0});
        addAndMakeVisible(title);
        const std::array<const char*, 4> labels {"Start", "Duration", "Offset", "Speed"};
        const std::array<const char*, 4> names {"Clip start", "Clip duration", "Clip source offset", "Clip speed"};
        const std::array<const char*, 4> hints {
            "Move the clip without changing its source offset or animation. Uses the ruler's units; add s for seconds.",
            "Change the right edge without stretching the source or animation. Musical lengths read bars.beats (1.2 is a bar and two beats); add s for seconds.",
            "Source offset in seconds: slip the source and its animation inside the existing clip.",
            "Playback multiplier. 1 is normal speed; 2 is twice as fast. Clip duration stays unchanged."
        };
        for (std::size_t i = 0; i < values.size(); ++i) {
            captions[i].setText(labels[i], juce::dontSendNotification);
            captions[i].setFont(motion::style::caption());
            captions[i].setColour(juce::Label::textColourId, osci::Colours::textMuted());
            addAndMakeVisible(captions[i]);
            auto& value = values[i];
            value.setName(names[i]); value.setTitle(names[i]);
            value.setEditable(false, true);
            value.setJustificationType(juce::Justification::centredRight);
            value.setFont(motion::style::body());
            value.setColour(juce::Label::backgroundColourId, osci::Colours::veryDark());
            value.setTooltip(hints[i]);
            value.onEditorShow = [this] { editRevision = processor.document.revision(); editGeneration = processor.document.generation(); };
            value.onTextChange = [this, i] { if (!updating) { apply(i); } };
            addAndMakeVisible(value);
        }
        details.setFont(motion::style::caption());
        details.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        details.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(details);
        status.setFont(motion::style::caption());
        status.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(status);
        refresh();
    }
    void setSelection(motion::Id id) {
        if (id != selected) { discardEditors(); selected = id; error.clear(); }
        refresh();
    }
    std::function<void()> onHeightChanged;
    bool hasClip() const { return findClip() != nullptr; }
    int preferredHeight() const { return hasClip() ? 16 + 2 * rowHeight + (status.isVisible() ? 30 : 0) + 4 : 0; }
    void refresh() {
        if (editGeneration != processor.document.generation() || editRevision != processor.document.revision()) { discardEditors(); }
        const auto* clip = findClip();
        const bool locked = isLocked();
        const auto previousHeight = preferredHeight();
        updating = true;
        const auto timing = clip != nullptr ? clip->timing(processor.document.project().tempo()) : motion::ClipTiming();
        const auto grid = processor.document.project().timeGrid();
        const std::array<juce::String, 4> current {juce::String(grid.positionLabel(timing.start)), juce::String(grid.durationLabel(timing.start, timing.end())), format(timing.offset) + "s", format(timing.rate)};
        for (std::size_t i = 0; i < values.size(); ++i) {
            values[i].setEnabled(clip != nullptr && !locked);
            if (!values[i].isBeingEdited()) { values[i].setText(current[i], juce::dontSendNotification); }
        }
        updating = false;
        // Which clock the clip keeps, on the heading line.
        details.setText(clip == nullptr ? juce::String() : clip->timeBase == motion::ClipTimeBase::beats ? "In beats" : "In seconds", juce::dontSendNotification);
        details.setTooltip(clip != nullptr && clip->timeBase == motion::ClipTimeBase::beats ? "Follows tempo changes" : "Keeps its time when the tempo changes");
        status.setColour(juce::Label::textColourId, error.isNotEmpty() ? motion::style::error() : osci::Colours::textMuted());
        status.setText(error.isNotEmpty() ? error : (locked ? "Track locked: timing is read-only." : juce::String()), juce::dontSendNotification);
        status.setVisible(clip != nullptr && status.getText().isNotEmpty());
        setVisible(clip != nullptr);
        resized();
        if (preferredHeight() != previousHeight && onHeightChanged) { onHeightChanged(); }
    }
    void resized() override {
        auto area = getLocalBounds();
        auto heading = area.removeFromTop(16);
        details.setBounds(heading.removeFromRight(80));
        title.setBounds(heading);
        for (std::size_t line = 0; line < 2; ++line) {
            auto row = area.removeFromTop(rowHeight);
            const auto half = row.getWidth() / 2;
            for (std::size_t column = 0; column < 2; ++column) {
                auto cell = row.removeFromLeft(half).withTrimmedRight(column == 0 ? 6 : 0);
                const auto i = line * 2 + column;
                captions[i].setBounds(cell.removeFromLeft(56));
                values[i].setBounds(cell.reduced(0, 3));
            }
        }
        status.setBounds(area.removeFromTop(30));
    }
    // Three decimals, more only when the value needs them (no trailing zeros).
    static juce::String format(double value) {
        auto text = juce::String(value, 6);
        while (text.contains(".") && text.endsWithChar('0') && text.length() - text.indexOfChar('.') > 4) { text = text.dropLastCharacters(1); }
        return text;
    }
private:
    const motion::Clip* findClip() const { return motion::findClip(processor.document.project(), selected); }
    bool isLocked() const {
        const auto* track = motion::findClipTrack(processor.document.project(), selected);
        return track != nullptr && track->locked;
    }
    void discardEditors() {
        for (auto& value : values) { if (value.isBeingEdited()) { value.hideEditor(true); } }
    }
    void apply(std::size_t index) {
        const auto* clip = findClip();
        if (clip == nullptr || isLocked() || editGeneration != processor.document.generation() || editRevision != processor.document.revision()) { refresh(); return; }
        const auto text = values[index].getText().trim();
        auto timing = clip->timing(processor.document.project().tempo());
        const auto grid = processor.document.project().timeGrid();
        std::optional<double> parsed;
        if (index == 0) {
            parsed = grid.parsePosition(text.toStdString());
        } else if (index == 1) {
            parsed = grid.parseDuration(text.toStdString(), timing.start);
        } else {
            // Offset is in seconds; its "s" is optional.
            const auto digits = index == 2 && text.endsWithIgnoreCase("s") ? text.dropLastCharacters(1).trim() : text;
            char* end = nullptr;
            const auto number = std::strtod(digits.toRawUTF8(), &end);
            if (!digits.isEmpty() && end != digits.toRawUTF8() && *end == '\0' && std::isfinite(number)) { parsed = number; }
        }
        if (!parsed.has_value()) {
            error = index < 2 ? "Enter a time like the ruler shows (" + juce::String(grid.positionLabel(timing.start)) + "), or seconds with s." : "Enter a finite number.";
            refresh();
            return;
        }
        const auto value = *parsed;
        if (index == 0) {
            timing.moveTo(value);
        } else if (index == 1) {
            timing.setDuration(value);
        } else if (index == 2) {
            timing.offset = value;
        } else {
            timing.rate = value;
        }
        const auto result = processor.document.setClipTiming(selected, timing);
        error = result.failed() ? result.getErrorMessage() : juce::String();
        refresh();
    }
    static constexpr int rowHeight = 28;
    MotionProcessor& processor;
    motion::Id selected = 0;
    std::uint64_t editRevision = 0, editGeneration = 0;
    bool updating = false;
    std::array<juce::Label, 4> captions, values;
    juce::Label title, details, status;
    juce::String error;
};
