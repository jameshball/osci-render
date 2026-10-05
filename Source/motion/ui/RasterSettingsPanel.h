#pragma once

#include "../model/RasterSettings.h"
#include <JuceHeader.h>
#include "MotionStyle.h"
#include <osci_gui/osci_gui.h>
#include <functional>

class MotionRasterSettingsPanel final : public juce::Component {
public:
    explicit MotionRasterSettingsPanel(motion::RasterSettings initial, bool video = false) : settings(initial), isVideo(video) {
        if (isVideo) {
            frameRate.setName("Video bake frame rate");
            frameRate.setSliderStyle(juce::Slider::LinearHorizontal);
            frameRate.setTextBoxStyle(juce::Slider::TextBoxRight, false, 66, 26);
            frameRate.setRange(1, 120, .001);
            frameRate.setValue(initial.videoFrameRate, juce::dontSendNotification);
            frameRate.setTextValueSuffix(" fps");
            frameRate.onValueChange = [this] { settings.videoFrameRate = frameRate.getValue(); refresh(); };
            frameRate.setTooltip("Resample video timing at this rate for deterministic seeking and export.");
            frameRateLabel.setFont(motion::style::body());
            frameRateLabel.setColour(juce::Label::textColourId, osci::Colours::textMuted());
            frameRateLabel.setBorderSize({});
            addAndMakeVisible(frameRate); addAndMakeVisible(frameRateLabel);
        }
        detailEdited = initial.resolution != motion::RasterSettings().resolution || initial.mode == motion::RasterSettings::Mode::scanlines;
        setName("Image preparation settings");
        mode.setName("Image trace mode");
        threshold.setName("Image threshold");
        detail.setName("Image detail");
        samples.setName("Image samples per frame");
        invert.setName("Invert image");
        invert.setButtonText("Invert image");
        invert.setToggleState(initial.invert, juce::dontSendNotification);
        mode.addItem("Outlines", 1);
        mode.addItem("Scanlines", 2);
        if (initial.mode == motion::RasterSettings::Mode::contours) {
            mode.setSelectedId(1, juce::dontSendNotification);
        } else if (initial.mode == motion::RasterSettings::Mode::scanlines) {
            mode.setSelectedId(2, juce::dontSendNotification);
        }
        resolutions = {64, 128, 256, 512};
        strides = {512, 1024, 2048, 4096, 8192, 16384};
        if (std::find(resolutions.begin(), resolutions.end(), initial.resolution) == resolutions.end()) { resolutions.push_back(initial.resolution); }
        if (std::find(strides.begin(), strides.end(), initial.pointsPerFrame) == strides.end()) { strides.push_back(initial.pointsPerFrame); }
        for (std::size_t i = 0; i < resolutions.size(); ++i) {
            detail.addItem(juce::String(resolutions[i]) + " px", static_cast<int>(i + 1));
            if (resolutions[i] == initial.resolution) { detail.setSelectedId(static_cast<int>(i + 1), juce::dontSendNotification); }
        }
        for (std::size_t i = 0; i < strides.size(); ++i) {
            samples.addItem(juce::String(static_cast<juce::int64>(strides[i])), static_cast<int>(i + 1));
            if (strides[i] == initial.pointsPerFrame) { samples.setSelectedId(static_cast<int>(i + 1), juce::dontSendNotification); }
        }
        threshold.setSliderStyle(juce::Slider::LinearHorizontal);
        threshold.setTextBoxStyle(juce::Slider::TextBoxRight, false, 66, 26);
        threshold.setRange(0, 100, 0);
        threshold.setNumDecimalPlacesToDisplay(2);
        threshold.setTextValueSuffix(" %");
        threshold.setValue(std::isfinite(initial.threshold) ? std::clamp(initial.threshold * 100, 0.0, 100.0) : 0, juce::dontSendNotification);
        threshold.setColour(juce::Slider::textBoxBackgroundColourId, osci::Colours::veryDark());
        threshold.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        threshold.setColour(juce::Slider::textBoxTextColourId, osci::Colours::text());
        mode.setTooltip("Outlines follow image edges. Scanlines fill the visible image with horizontal beam passes.");
        threshold.setTooltip("Pixels below this brightness are blanked. Transparency also reduces brightness.");
        detail.setTooltip("Maximum tracing dimension. Lower detail simplifies outlines and reduces preparation work.");
        samples.setTooltip("Beam samples stored per image frame. Increase this if the image is too complex.");
        invert.setTooltip("Invert image colours before tracing, for dark artwork on a light background. Transparency stays unchanged.");
        for (auto* combo : {&mode, &detail, &samples}) {
            combo->setColour(juce::ComboBox::backgroundColourId, osci::Colours::veryDark());
            combo->setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
            combo->onChange = [this] { refresh(); };
        }
        detail.onChange = [this] { detailEdited = true; refresh(); };
        mode.onChange = [this] {
            if (!detailEdited) {
                const auto suggested = mode.getSelectedId() == 2 ? 64 : motion::RasterSettings().resolution;
                const auto found = std::find(resolutions.begin(), resolutions.end(), suggested);
                detail.setSelectedId(static_cast<int>(std::distance(resolutions.begin(), found) + 1), juce::dontSendNotification);
            }
            refresh();
        };
        threshold.onValueChange = [this] { thresholdEdited = true; refresh(); };
        invert.onClick = [this] { refresh(); };
        for (auto* caption : {&modeLabel, &thresholdLabel, &detailLabel, &samplesLabel}) {
            caption->setFont(motion::style::body());
            caption->setColour(juce::Label::textColourId, osci::Colours::textMuted());
            caption->setBorderSize({});
        }
        for (auto* label : {&note, &error}) {
            label->setFont(motion::style::body());
            label->setJustificationType(juce::Justification::topLeft);
            label->setBorderSize({});
        }
        note.setColour(juce::Label::textColourId, osci::Colours::textMuted());

        error.setName("Image preparation validation error");
        error.setColour(juce::Label::textColourId, juce::Colour(0xffe98080));
        if (isVideo) {
        }
        prepare.setName(isVideo ? "Prepare video" : "Prepare image");
        prepare.setButtonText(isVideo ? "Prepare video" : "Prepare image");
        prepare.onClick = [this] {
            if (submitted) { return; }
            refresh();
            if (valid && onPrepare) {
                submitted = true;
                prepare.setEnabled(false);
                onPrepare(settings);
            }
        };
        for (auto* component : std::initializer_list<juce::Component*> {&modeLabel, &thresholdLabel, &detailLabel, &samplesLabel,
                &mode, &threshold, &detail, &samples, &invert, &note, &error, &prepare}) {
            addAndMakeVisible(component);
        }
        setSize(440, 400);
        refresh();
    }

    std::function<void(motion::RasterSettings)> onPrepare;

    void resized() override {
        auto area = getLocalBounds().reduced(motion::style::dialog::margin);
        motion::style::dialog::footer(area, {&prepare});
        const auto row = [&](juce::Label& label, juce::Component& field) { motion::style::dialog::formRow(area, label, field, 142); };
        row(modeLabel, mode);
        row(thresholdLabel, threshold);
        row(detailLabel, detail);
        row(samplesLabel, samples);
        if (isVideo) { row(frameRateLabel, frameRate); }
        // The tick box lines up with the fields' left edge.
        invert.setBounds(area.removeFromTop(motion::style::dialog::row).withTrimmedLeft(142 - 4));
        area.removeFromTop(motion::style::dialog::rowGap);
        error.setBounds(area);
    }

private:
    void refresh() {
        auto next = settings;
        juce::String message;
        const auto modeId = mode.getSelectedId();
        const auto detailIndex = detail.getSelectedId() - 1;
        const auto sampleIndex = samples.getSelectedId() - 1;
        if ((modeId != 1 && modeId != 2) || detailIndex < 0 || sampleIndex < 0
            || static_cast<std::size_t>(detailIndex) >= resolutions.size() || static_cast<std::size_t>(sampleIndex) >= strides.size()) {
            message = "Choose a trace mode, image detail and samples per frame.";
        } else {
            next.mode = modeId == 1 ? motion::RasterSettings::Mode::contours : motion::RasterSettings::Mode::scanlines;
            next.resolution = resolutions[static_cast<std::size_t>(detailIndex)];
            next.pointsPerFrame = strides[static_cast<std::size_t>(sampleIndex)];
            next.invert = invert.getToggleState();
            if (thresholdEdited) { next.threshold = threshold.getValue() / 100.0; }
            message = juce::String(next.validate());
        }
        valid = message.isEmpty();
        error.setText(message, juce::dontSendNotification);
        prepare.setEnabled(valid && !submitted);
        if (valid) { settings = next; }
    }

    motion::RasterSettings settings;
    bool isVideo = false;
    juce::Slider frameRate;
    juce::Label frameRateLabel {"Video frame rate caption", "Bake frame rate"};
    bool valid = false, submitted = false, thresholdEdited = false, detailEdited = false;
    std::vector<int> resolutions;
    std::vector<std::size_t> strides;
    juce::ComboBox mode, detail, samples;
    juce::Slider threshold;
    juce::ToggleButton invert;
    juce::Label modeLabel {"Image mode caption", "Trace mode"}, thresholdLabel {"Image threshold caption", "Brightness threshold"};
    juce::Label detailLabel {"Image detail caption", "Image detail"}, samplesLabel {"Image samples caption", "Samples per frame"};
    juce::Label note, error;
    juce::TextButton prepare;
};
