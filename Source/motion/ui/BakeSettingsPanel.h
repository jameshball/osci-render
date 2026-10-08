#pragma once

#include "../model/BakeSettings.h"
#include <JuceHeader.h>
#include "MotionStyle.h"
#include "ScrubField.h"
#include <array>
#include <functional>

// How a Lua source is baked, as the foot of its editor: a "Bake" heading
// with the estimate (frames and memory) on its right, then the settings two
// to a row. The bake itself is the editor's primary button.
class MotionBakeSettingsPanel final : public juce::Component {
public:
    explicit MotionBakeSettingsPanel(motion::BakeSettings initial) : settings(initial) {
        setName("Bake source settings");
        duration.setName("Bake duration");
        frameRate.setName("Bake frame rate");
        samples.setName("Bake samples per frame");
        seed.setName("Bake random seed");
        bpm.setName("Bake tempo");
        duration.setSpec(durationSpec);
        bpm.setSpec(tempoSpec);
        seed.setSpec(seedSpec);
        duration.setValue(initial.duration);
        bpm.setValue(initial.bpm);
        seed.setValue(initial.seed);
        duration.setTooltip("Length of the prepared source. Rounded up to complete frames.");
        frameRate.setTooltip("Frames of source animation prepared each second.");
        samples.setTooltip("Samples retained per frame, preserving the source's point density.");
        seed.setTooltip("Keeps random source generation repeatable.");
        bpm.setTooltip("Fixed tempo used while evaluating the source.");
        rates = { 24, 25, 30, 50, 60, 120 };
        strides = { 256, 512, 1024, 2048, 4096 };
        if (std::find(rates.begin(), rates.end(), initial.frameRate) == rates.end()) { rates.push_back(initial.frameRate); }
        if (std::find(strides.begin(), strides.end(), initial.pointsPerFrame) == strides.end()) { strides.push_back(initial.pointsPerFrame); }
        for (std::size_t index = 0; index < rates.size(); ++index) {
            frameRate.addItem(juce::String(rates[index], rates[index] == std::floor(rates[index]) ? 0 : 3) + " fps", static_cast<int>(index + 1));
            if (rates[index] == initial.frameRate) { frameRate.setSelectedId(static_cast<int>(index + 1), juce::dontSendNotification); }
        }
        for (std::size_t index = 0; index < strides.size(); ++index) {
            samples.addItem(juce::String(static_cast<juce::int64>(strides[index])), static_cast<int>(index + 1));
            if (strides[index] == initial.pointsPerFrame) { samples.setSelectedId(static_cast<int>(index + 1), juce::dontSendNotification); }
        }
        for (auto* combo : { &frameRate, &samples }) { combo->onChange = [this] { refresh(); }; }
        for (auto* field : { &duration, &bpm, &seed }) {
            field->onChange = [this](double) { refresh(); };
            field->onCommit = field->onChange;
        }
        motion::style::inspector::styleHeading(title, "Bake");
        for (auto [caption, text] : { std::pair { &durationLabel, "Duration" }, std::pair { &rateLabel, "Frame rate" }, std::pair { &samplesLabel, "Samples" }, std::pair { &seedLabel, "Seed" }, std::pair { &bpmLabel, "Tempo" } }) {
            motion::style::inspector::styleCaption(*caption, text);
        }
        summary.setName("Bake prepared payload estimate");
        summary.setFont(motion::style::caption());
        summary.setJustificationType(juce::Justification::centredRight);
        summary.setBorderSize({});
        for (auto* component : std::initializer_list<juce::Component*> { &title, &summary, &durationLabel, &rateLabel, &samplesLabel, &seedLabel, &bpmLabel,
                 &duration, &frameRate, &samples, &seed, &bpm }) {
            addAndMakeVisible(component);
        }
        bake.setName("Bake source");
        bake.onClick = [this] {
            refresh();
            if (valid && onBake) {
                bake.setEnabled(false);
                onBake(settings);
            }
        };
        refresh();
    }

    std::function<void(motion::BakeSettings)> onBake;
    // The editor's header holds the bake button.
    juce::TextButton& bakeButton() { return bake; }
    // The last valid settings shown.
    const motion::BakeSettings& currentSettings() const { return settings; }
    static constexpr int headingBlock = 30, row = 28, preferredHeight = headingBlock + 3 * row;

    void resized() override {
        auto area = getLocalBounds();
        // Two columns, captions beside their fields, the fields a fixed width
        // so numbers and choices line up whatever the editor's width. The
        // estimate ends where the fields do.
        const auto column = std::min(260, (area.getWidth() - 24) / 2);
        const auto fieldsRight = area.getX() + column + 24 + std::min(column - 72, 120) + 72;
        auto heading = area.removeFromTop(headingBlock).withTrimmedTop(10).withHeight(16);
        title.setBounds(heading);
        summary.setBounds(heading.withRight(std::min(heading.getRight(), fieldsRight)));
        const std::array<std::pair<juce::Label*, juce::Component*>, 5> cells { { { &durationLabel, &duration }, { &rateLabel, &frameRate }, { &samplesLabel, &samples }, { &seedLabel, &seed }, { &bpmLabel, &bpm } } };
        for (std::size_t index = 0; index < cells.size(); ++index) {
            auto cell = juce::Rectangle<int>(area.getX() + static_cast<int>(index % 2) * (column + 24), area.getY() + static_cast<int>(index / 2) * row, column, row);
            cells[index].first->setBounds(cell.removeFromLeft(72));
            cells[index].second->setBounds(cell.withWidth(std::min(cell.getWidth(), 120)).reduced(0, 2));
        }
    }

private:
    static constexpr motion::PropertySpec durationSpec {"duration", "Duration", "", "", 0.01, 3600, 5, .05, 3, "s"};
    static constexpr motion::PropertySpec tempoSpec {"tempo", "Tempo", "", "", 1, 1000, 120, 1, 1, " BPM"};
    static constexpr motion::PropertySpec seedSpec {"seed", "Seed", "", "", 0, 4294967295.0, 0, 1, 0, ""};
    void refresh() {
        auto next = settings;
        next.duration = duration.getValue();
        next.bpm = bpm.getValue();
        next.seed = static_cast<std::uint32_t>(std::clamp(seed.getValue(), 0.0, 4294967295.0));
        const auto rateIndex = frameRate.getSelectedId() - 1;
        const auto strideIndex = samples.getSelectedId() - 1;
        juce::String message;
        if (rateIndex < 0 || strideIndex < 0 || static_cast<std::size_t>(rateIndex) >= rates.size() || static_cast<std::size_t>(strideIndex) >= strides.size()) {
            message = "Choose a frame rate and samples per frame.";
        } else {
            next.frameRate = rates[static_cast<std::size_t>(rateIndex)];
            next.pointsPerFrame = strides[static_cast<std::size_t>(strideIndex)];
            message = juce::String(next.validate());
        }
        valid = message.isEmpty();
        bake.setEnabled(valid);
        summary.setColour(juce::Label::textColourId, valid ? osci::Colours::textMuted() : motion::style::error());
        if (!valid) {
            summary.setText(message, juce::dontSendNotification);
            return;
        }
        settings = next;
        const auto frames = next.frameCount();
        const auto mib = static_cast<double>(frames) * next.pointsPerFrame * sizeof(motion::PointSample) / (1024 * 1024);
        summary.setText(juce::String(static_cast<juce::int64>(frames)) + " frames" + motion::style::dot() + juce::String(mib, 2) + " MiB", juce::dontSendNotification);
    }
    motion::BakeSettings settings;
    bool valid = false;
    std::vector<double> rates;
    std::vector<std::size_t> strides;
    juce::Label title, summary;
    juce::Label durationLabel, rateLabel, samplesLabel, seedLabel, bpmLabel;
    motion::ui::ScrubField duration, seed, bpm;
    juce::ComboBox frameRate, samples;
    juce::TextButton bake;
};
