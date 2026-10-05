#pragma once

#include "../MotionProcessor.h"
#include "TypedNumber.h"
#include "MotionStyle.h"
#include <array>
#include <functional>

// With nothing selected, Properties shows the composition's own settings:
// how long it runs, its frame rate, tempo and meter, in the same two-column
// rows as a clip's timing.
class MotionCompositionPanel final : public juce::Component {
public:
    explicit MotionCompositionPanel(MotionProcessor& owner) : processor(owner) {
        setName("Composition settings");
        motion::style::inspector::styleHeading(title, "Timing");
        addAndMakeVisible(title);
        const std::array<const char*, 4> labels {"Length", "Frames", "Tempo", "Meter"};
        for (std::size_t index = 0; index < captions.size(); ++index) {
            motion::style::inspector::styleCaption(captions[index], labels[index]);
            addAndMakeVisible(captions[index]);
        }
        for (auto [field, name, tip] : {std::tuple {&length, "Composition length", "How long the composition runs, in seconds; it is never shorter than its last clip"},
                                        std::tuple {&tempo, "Composition tempo", "Beats per minute at the start; tempo changes on the ruler take over from there"}}) {
            field->setName(name);
            field->setTitle(name);
            field->setTooltip(tip);
            field->setEditable(false, true);
            field->setJustificationType(juce::Justification::centredRight);
            field->setFont(motion::style::body());
            field->setColour(juce::Label::backgroundColourId, osci::Colours::veryDark());
            addAndMakeVisible(*field);
        }
        length.onTextChange = [this] { if (!updating) { applyLength(); } };
        tempo.onTextChange = [this] { if (!updating) { applyTempo(); } };
        frameRate.setName("Composition frame rate");
        frameRate.setTooltip("Frames per second for the timeline grid and video export");
        const std::array<const char*, 8> rateNames {"23.976 fps", "24 fps", "25 fps", "29.97 fps", "30 fps", "50 fps", "60 fps", "120 fps"};
        for (std::size_t index = 0; index < rateNames.size(); ++index) { frameRate.addItem(rateNames[index], 500 + static_cast<int>(index)); }
        meter.setName("Composition meter");
        meter.setTooltip("Beats in a bar");
        for (const auto beats : {2, 3, 4, 5, 6, 7}) { meter.addItem(juce::String(beats) + "/4", 400 + beats); }
        for (auto* combo : {&frameRate, &meter}) {
            motion::style::styleField(*combo);
            combo->onChange = [this, combo] { if (!updating && onTiming) { onTiming(combo->getSelectedId()); } };
            addAndMakeVisible(*combo);
        }
    }

    // The Timing menu's own command ids, so both change things the same way.
    std::function<void(int)> onTiming;
    std::function<void(const juce::String&)> onError;
    std::function<void()> onHeightChanged;
    static constexpr std::array<double, 8> rates {24000.0 / 1001, 24, 25, 30000.0 / 1001, 30, 50, 60, 120};

    void setShown(bool value) {
        if (shown == value) { return; }
        shown = value;
        refresh();
        if (onHeightChanged) { onHeightChanged(); }
    }
    int preferredHeight() const { return shown ? motion::style::inspector::headingHeight + 2 * motion::style::inspector::row + 10 : 0; }
    void refresh() {
        if (!shown) { return; }
        const auto& project = processor.document.project();
        updating = true;
        if (!length.isBeingEdited()) { length.setText(juce::String(project.duration, 2) + "s", juce::dontSendNotification); }
        if (!tempo.isBeingEdited()) { tempo.setText(juce::String(project.bpm, project.bpm == std::round(project.bpm) ? 0 : 2), juce::dontSendNotification); }
        int rate = 0;
        for (std::size_t index = 0; index < rates.size(); ++index) { if (std::abs(project.frameRate - rates[index]) < 1.0e-9) { rate = 500 + static_cast<int>(index); } }
        frameRate.setSelectedId(rate, juce::dontSendNotification);
        if (rate == 0) { frameRate.setText(juce::String(project.frameRate, 3) + " fps", juce::dontSendNotification); }
        meter.setSelectedId(400 + project.beatsPerBar, juce::dontSendNotification);
        updating = false;
    }
    void resized() override {
        auto area = getLocalBounds();
        title.setBounds(area.removeFromTop(motion::style::inspector::headingHeight));
        motion::style::inspector::layoutFields(area, captions, {&length, &frameRate, &tempo, &meter});
    }

private:
    void applyLength() {
        const auto value = motion::ui::parseNumber(length.getText(), "s");
        auto& document = processor.document;
        double lastClip = 0;
        for (const auto& track : document.project().tracks) {
            for (const auto& clip : track.clips) { lastClip = std::max(lastClip, clip.timing(document.project().tempo()).end()); }
        }
        if (!value.has_value() || *value <= 0 || *value > 24 * 3600) {
            if (onError) { onError("Enter a length in seconds, up to 24 hours."); }
        } else if (*value < lastClip - 1.0e-9) {
            if (onError) { onError("The composition runs at least to its last clip (" + juce::String(lastClip, 2) + " s)."); }
        } else if (std::abs(*value - document.project().duration) > 1.0e-9) {
            document.edit("Change composition length", [length = *value](motion::Project& project) { project.duration = length; });
        }
        refresh();
    }
    void applyTempo() {
        const auto value = motion::ui::parseNumber(tempo.getText());
        if (!value.has_value() || *value < 1 || *value > 1000) {
            if (onError) { onError("Enter a tempo between 1 and 1000 BPM."); }
        } else if (*value != processor.document.project().bpm) {
            const auto result = processor.document.changeTempo(*value);
            if (result.failed() && onError) { onError(result.getErrorMessage()); }
        }
        refresh();
    }

    MotionProcessor& processor;
    bool shown = false, updating = false;
    juce::Label title, length, tempo;
    std::array<juce::Label, 4> captions;
    juce::ComboBox frameRate, meter;
};
