#pragma once

#include "../MotionProcessor.h"
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
        title.setText("Timing", juce::dontSendNotification);
        title.setFont(motion::style::caption());
        title.setColour(juce::Label::textColourId, motion::style::muted());
        title.setBorderSize({0, 2, 0, 0});
        addAndMakeVisible(title);
        const std::array<const char*, 4> labels {"Length", "Frames", "Tempo", "Meter"};
        for (std::size_t index = 0; index < captions.size(); ++index) {
            captions[index].setText(labels[index], juce::dontSendNotification);
            captions[index].setFont(motion::style::caption());
            captions[index].setColour(juce::Label::textColourId, motion::style::muted());
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
            field->setColour(juce::Label::backgroundColourId, motion::style::field());
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
            combo->setColour(juce::ComboBox::backgroundColourId, motion::style::field());
            combo->setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
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
    int preferredHeight() const { return shown ? 16 + 2 * rowHeight + 10 : 0; }
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
        title.setBounds(area.removeFromTop(16));
        const std::array<juce::Component*, 4> fields {&length, &frameRate, &tempo, &meter};
        for (std::size_t line = 0; line < 2; ++line) {
            auto row = area.removeFromTop(rowHeight);
            const auto half = row.getWidth() / 2;
            for (std::size_t column = 0; column < 2; ++column) {
                const auto index = line * 2 + column;
                auto cell = row.removeFromLeft(half).withTrimmedRight(column == 0 ? 6 : 0);
                captions[index].setBounds(cell.removeFromLeft(56));
                fields[index]->setBounds(cell.reduced(0, 3));
            }
        }
    }

private:
    static constexpr int rowHeight = 28;
    static std::optional<double> number(juce::String text) {
        text = text.trim();
        if (text.endsWithIgnoreCase("s")) { text = text.dropLastCharacters(1).trim(); }
        char* end = nullptr;
        const auto value = std::strtod(text.toRawUTF8(), &end);
        if (text.isEmpty() || end == nullptr || *end != '\0' || !std::isfinite(value)) { return std::nullopt; }
        return value;
    }
    void applyLength() {
        const auto value = number(length.getText());
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
        const auto value = number(tempo.getText());
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
