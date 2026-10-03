#pragma once

#include "../model/BakeSettings.h"
#include <JuceHeader.h>
#include "MotionStyle.h"
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
            input->setColour(juce::TextEditor::backgroundColourId, osci::Colours::veryDark());
            input->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
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
        for (auto* combo : { &frameRate, &samples }) {
            combo->setColour(juce::ComboBox::backgroundColourId, osci::Colours::veryDark());
            combo->setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
            combo->onChange = [this] { refresh(); };
        }
        duration.onTextChange = [this] { durationEdited = true; refresh(); };
        bpm.onTextChange = [this] { tempoEdited = true; refresh(); };
        seed.onTextChange = [this] { refresh(); };
        for (auto* caption : { &durationLabel, &rateLabel, &samplesLabel, &seedLabel, &bpmLabel }) {
            caption->setFont(motion::style::body());
            caption->setColour(juce::Label::textColourId, osci::Colours::textMuted());
            caption->setBorderSize({});
        }
        for (auto* label : { &summary, &note, &error }) {
            label->setFont(motion::style::body());
            label->setJustificationType(juce::Justification::topLeft);
            label->setBorderSize({});
        }
        summary.setName("Bake prepared payload estimate");
        note.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        note.setText("MIDI and slider inputs stay fixed while baking. Stateful Lua becomes a prepared source for repeatable scrubbing and export.", juce::dontSendNotification);
        error.setName("Bake validation error");
        error.setColour(juce::Label::textColourId, juce::Colour(0xffe98080));
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
        setSize(440, 400);
        refresh();
    }

    std::function<void(motion::BakeSettings)> onBake;

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        const auto row = [&](juce::Label& label, juce::Component& field) {
            auto bounds = area.removeFromTop(28);
            label.setBounds(bounds.removeFromLeft(142));
            field.setBounds(bounds);
            area.removeFromTop(10);
        };
        row(durationLabel, duration);
        row(rateLabel, frameRate);
        row(samplesLabel, samples);
        row(seedLabel, seed);
        row(bpmLabel, bpm);
        area.removeFromTop(2);
        summary.setBounds(area.removeFromTop(40));
        area.removeFromTop(8);
        note.setBounds(area.removeFromTop(44));
        area.removeFromTop(6);
        error.setBounds(area.removeFromTop(40));
        bake.setBounds(area.removeFromTop(32));
    }

private:
    static juce::String displayNumber(double value) {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::setprecision(9) << value;
        return juce::String(stream.str());
    }
    static bool number(const juce::String& text, double& result) {
        const auto trimmed = text.trim();
        char* end = nullptr;
        result = std::strtod(trimmed.toRawUTF8(), &end);
        return trimmed.isNotEmpty() && end != nullptr && *end == '\0' && std::isfinite(result);
    }
    void refresh() {
        auto next = settings;
        juce::String message;
        if (durationEdited && !number(duration.getText(), next.duration)) { message = "Enter a finite duration in seconds."; }
        else if (tempoEdited && !number(bpm.getText(), next.bpm)) { message = "Enter a tempo between 1 and 1000 BPM."; }
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
        bake.setEnabled(valid);
        if (valid) {
            settings = next;
            const auto frames = next.frameCount();
            const auto mib = static_cast<double>(frames) * next.pointsPerFrame * sizeof(motion::PointSample) / (1024 * 1024);
            summary.setText(juce::String(mib, 2) + " MiB prepared payload  /  " + juce::String(static_cast<juce::int64>(frames)) + " frames\n"
                + displayNumber(static_cast<double>(frames) / next.frameRate) + " seconds after frame rounding", juce::dontSendNotification);
        } else {
            summary.setText("Prepared payload estimate requires valid settings.\nMaximum prepared payload: 256 MiB.", juce::dontSendNotification);
        }
    }
    motion::BakeSettings settings;
    bool valid = false;
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
