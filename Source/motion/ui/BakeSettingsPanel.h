#pragma once

#include "../model/BakeSettings.h"
#include "TypedNumber.h"
#include <JuceHeader.h>
#include "MotionStyle.h"
#include <array>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <iomanip>
#include <locale>

class MotionBakeSettingsPanel final : public juce::Component {
public:
    explicit MotionBakeSettingsPanel(motion::BakeSettings initial) : settings(initial) {
        setName("Bake source settings");
        duration.setName("Bake duration");
        frameRate.setName("Bake frame rate");
        samples.setName("Bake samples per frame");
        seed.setName("Bake random seed");
        bpm.setName("Bake tempo");
        for (auto* input : { &duration, &seed, &bpm }) {
            input->setFont(motion::style::body());
            input->setJustification(juce::Justification::centredLeft);
            input->setSelectAllWhenFocused(true);
        }
        duration.setText(displayNumber(initial.duration), false);
        bpm.setText(displayNumber(initial.bpm), false);
        seed.setText(juce::String(static_cast<juce::int64>(initial.seed)), false);
        duration.setTooltip("Length of the prepared source in seconds. Rounded up to complete frames.");
        frameRate.setTooltip("Frames of source animation prepared each second.");
        samples.setTooltip("Samples retained per frame, preserving the source's point density.");
        seed.setTooltip("An unsigned 32-bit seed keeps random source generation repeatable.");
        bpm.setTooltip("Fixed tempo used while evaluating the source.");
        rates = { 24, 25, 30, 50, 60, 120 };
        strides = { 256, 512, 1024, 2048, 4096 };
        if (std::find(rates.begin(), rates.end(), initial.frameRate) == rates.end()) { rates.push_back(initial.frameRate); }
        if (std::find(strides.begin(), strides.end(), initial.pointsPerFrame) == strides.end()) { strides.push_back(initial.pointsPerFrame); }
        for (std::size_t index = 0; index < rates.size(); ++index) {
            frameRate.addItem(juce::String(rates[index], rates[index] == std::floor(rates[index]) ? 0 : 6) + " fps", static_cast<int>(index + 1));
            if (rates[index] == initial.frameRate || (std::isnan(rates[index]) && std::isnan(initial.frameRate))) {
                frameRate.setSelectedId(static_cast<int>(index + 1), juce::dontSendNotification);
            }
        }
        for (std::size_t index = 0; index < strides.size(); ++index) {
            samples.addItem(juce::String(static_cast<juce::int64>(strides[index])), static_cast<int>(index + 1));
            if (strides[index] == initial.pointsPerFrame) { samples.setSelectedId(static_cast<int>(index + 1), juce::dontSendNotification); }
        }
        for (auto* combo : { &frameRate, &samples }) { combo->onChange = [this] { refresh(); }; }
        duration.onTextChange = [this] { durationEdited = true; refresh(); };
        bpm.onTextChange = [this] { tempoEdited = true; refresh(); };
        seed.onTextChange = [this] { refresh(); };
        for (auto* caption : { &durationLabel, &rateLabel, &samplesLabel, &seedLabel, &bpmLabel }) { motion::style::dialog::caption(*caption); }
        for (auto* label : { &summary, &note, &error }) {
            label->setFont(motion::style::body());
            label->setJustificationType(juce::Justification::topLeft);
            label->setBorderSize({});
        }
        summary.setName("Bake prepared payload estimate");
        note.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        note.setVisible(false);
        error.setName("Bake validation error");
        error.setColour(juce::Label::textColourId, motion::style::error());
        bake.setName("Bake source");
        bake.setButtonText("Bake source");
        bake.onClick = [this] {
            refresh();
            if (valid && onBake) {
                bake.setEnabled(false);
                onBake(settings);
            }
        };
        for (auto* component : std::initializer_list<juce::Component*> { &durationLabel, &rateLabel, &samplesLabel, &seedLabel, &bpmLabel,
                 &duration, &frameRate, &samples, &seed, &bpm, &summary, &note, &error, &bake }) {
            addAndMakeVisible(component);
        }
        setSize(440, 290);
        refresh();
    }

    std::function<void(motion::BakeSettings)> onBake;
    // Inside the Scene's Lua editor: a footer of two columns, captions beside
    // fields like a clip's timing, and the bake button in the editor's header.
    juce::TextButton& bakeButton() { return bake; }
    // The last valid settings shown.
    const motion::BakeSettings& currentSettings() const { return settings; }
    void setEmbedded(bool value) {
        embedded = value;
        // One line in the grid's last cell, centred like the fields.
        for (auto* label : { &summary, &note, &error }) { label->setJustificationType(embedded ? juce::Justification::centredLeft : juce::Justification::topLeft); }
        resized();
    }
    static constexpr int embeddedRow = 28, embeddedHeight = 3 * embeddedRow;

    void resized() override {
        if (embedded) {
            auto area = getLocalBounds();
            const std::array<std::pair<juce::Label*, juce::Component*>, 5> cells { { { &durationLabel, &duration }, { &rateLabel, &frameRate }, { &samplesLabel, &samples }, { &seedLabel, &seed }, { &bpmLabel, &bpm } } };
            juce::Rectangle<int> last;
            for (std::size_t line = 0; line < 3; ++line) {
                auto row = area.removeFromTop(embeddedRow);
                const auto half = row.getWidth() / 2;
                for (std::size_t column = 0; column < 2; ++column) {
                    auto cell = row.removeFromLeft(half).withTrimmedRight(column == 0 ? 8 : 0);
                    const auto index = line * 2 + column;
                    if (index >= cells.size()) {
                        last = cell;
                        continue;
                    }
                    cells[index].first->setBounds(cell.removeFromLeft(112));
                    cells[index].second->setBounds(cell.reduced(0, 2));
                }
            }
            // The estimate, or what is wrong, fills the last cell.
            summary.setBounds(last);
            error.setBounds(last);
            return;
        }
        auto area = getLocalBounds().reduced(motion::style::dialog::margin);
        motion::style::dialog::footer(area, { &bake });
        const auto row = [&](juce::Label& label, juce::Component& field) { motion::style::dialog::formRow(area, label, field, 142); };
        row(durationLabel, duration);
        row(rateLabel, frameRate);
        row(samplesLabel, samples);
        row(seedLabel, seed);
        row(bpmLabel, bpm);
        // The estimate and any problem share one slot above the button.
        summary.setBounds(area);
        error.setBounds(area);
    }

private:
    static juce::String displayNumber(double value) {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::setprecision(9) << value;
        return juce::String(stream.str());
    }
    void refresh() {
        auto next = settings;
        juce::String message;
        const auto typedDuration = motion::ui::parseNumber(duration.getText()), typedTempo = motion::ui::parseNumber(bpm.getText());
        if (durationEdited && !typedDuration.has_value()) {
            message = "Enter a finite duration in seconds.";
        } else if (tempoEdited && !typedTempo.has_value()) {
            message = "Enter a tempo between 1 and 1000 BPM.";
        }
        if (durationEdited) { next.duration = typedDuration.value_or(next.duration); }
        if (tempoEdited) { next.bpm = typedTempo.value_or(next.bpm); }
        const auto seedText = seed.getText().trim();
        if (message.isEmpty()) {
            if (seedText.isEmpty() || seedText.length() > 10 || !seedText.containsOnly("0123456789") || seedText.getLargeIntValue() > 0xffffffffLL) {
                message = "Seed must be a whole number from 0 to 4294967295.";
            } else { next.seed = static_cast<std::uint32_t>(seedText.getLargeIntValue()); }
        }
        const auto rateIndex = frameRate.getSelectedId() - 1;
        const auto strideIndex = samples.getSelectedId() - 1;
        if (message.isEmpty()) {
            if (rateIndex < 0 || strideIndex < 0 || static_cast<std::size_t>(rateIndex) >= rates.size() || static_cast<std::size_t>(strideIndex) >= strides.size()) {
                message = "Choose a frame rate and samples per frame.";
            } else {
                next.frameRate = rates[static_cast<std::size_t>(rateIndex)];
                next.pointsPerFrame = strides[static_cast<std::size_t>(strideIndex)];
                message = juce::String(next.validate());
            }
        }
        valid = message.isEmpty();
        error.setText(message, juce::dontSendNotification);
        summary.setVisible(valid);
        bake.setEnabled(valid);
        if (valid) {
            settings = next;
            const auto frames = next.frameCount();
            const auto mib = static_cast<double>(frames) * next.pointsPerFrame * sizeof(motion::PointSample) / (1024 * 1024);
            const auto seconds = static_cast<double>(frames) / next.frameRate;
            const auto dot = juce::String::fromUTF8(" \xc2\xb7 ");
            summary.setText(juce::String(static_cast<juce::int64>(frames)) + " frames" + dot + juce::String(mib, 2) + " MiB"
                + (std::abs(seconds - next.duration) > 1.0e-9 ? dot + "rounded to " + displayNumber(seconds) + " s" : juce::String()), juce::dontSendNotification);
        }
    }
    motion::BakeSettings settings;
    bool valid = false, embedded = false;
    bool durationEdited = false, tempoEdited = false;
    std::vector<double> rates;
    std::vector<std::size_t> strides;
    juce::Label durationLabel { "Bake duration caption", "Duration (seconds)" }, rateLabel { "Bake frame rate caption", "Frame rate" };
    juce::Label samplesLabel { "Bake samples caption", "Samples per frame" }, seedLabel { "Bake seed caption", "Random seed" }, bpmLabel { "Bake tempo caption", "Tempo (BPM)" };
    juce::TextEditor duration, seed, bpm;
    juce::ComboBox frameRate, samples;
    juce::Label summary, note, error;
    juce::TextButton bake;
};
