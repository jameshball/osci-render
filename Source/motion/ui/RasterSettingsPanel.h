#pragma once

#include "../model/RasterSettings.h"
#include "../import/RasterSourcePreparer.h"
#include "Sheet.h"
#include "ScrubField.h"
#include "SourcePreview.h"
#include <functional>

// How an image or video is traced into beam strokes. Images show the traced
// first frame beside the settings, updated as they change.
class MotionRasterSettingsPanel final : public motion::ui::Sheet {
public:
    MotionRasterSettingsPanel(motion::RasterSettings initial, bool video, const juce::String& fileName, juce::MemoryBlock image = {})
        : Sheet(video ? "Prepare video" : "Prepare image", fileName, "Prepare"), settings(initial), isVideo(video), data(std::move(image)) {
        setName("Image preparation settings");
        nameAction(isVideo ? "Prepare video" : "Prepare image");
        detailEdited = initial.resolution != motion::RasterSettings().resolution || initial.mode == motion::RasterSettings::Mode::scanlines;
        mode.setSelected(initial.mode == motion::RasterSettings::Mode::scanlines ? 1 : 0);
        mode.setTooltip("Outlines follow image edges. Scanlines fill the visible image with horizontal beam passes.");
        mode.onChange = [this](int index) {
            if (!detailEdited) { selectResolution(index == 1 ? 64 : motion::RasterSettings().resolution); }
            refresh();
        };
        threshold.setName("Image threshold");
        threshold.setSpec(thresholdSpec);
        threshold.setValue(std::isfinite(initial.threshold) ? std::clamp(initial.threshold * 100, 0.0, 100.0) : 0);
        threshold.setTooltip("Pixels below this brightness are blanked. Transparency also reduces brightness.");
        threshold.onChange = [this](double) { thresholdEdited = true; refresh(); };
        threshold.onCommit = threshold.onChange;
        resolutions = {64, 128, 256, 512};
        strides = {512, 1024, 2048, 4096, 8192, 16384};
        if (std::find(resolutions.begin(), resolutions.end(), initial.resolution) == resolutions.end()) { resolutions.push_back(initial.resolution); }
        if (std::find(strides.begin(), strides.end(), initial.pointsPerFrame) == strides.end()) { strides.push_back(initial.pointsPerFrame); }
        detail.setName("Image detail");
        for (std::size_t i = 0; i < resolutions.size(); ++i) { detail.addItem(juce::String(resolutions[i]) + " px", static_cast<int>(i + 1)); }
        selectResolution(initial.resolution);
        detail.setTooltip("Maximum tracing dimension. Lower detail simplifies outlines and reduces preparation work.");
        detail.onChange = [this] { detailEdited = true; refresh(); };
        samples.setName("Image samples per frame");
        for (std::size_t i = 0; i < strides.size(); ++i) {
            samples.addItem(juce::String(static_cast<juce::int64>(strides[i])), static_cast<int>(i + 1));
            if (strides[i] == initial.pointsPerFrame) { samples.setSelectedId(static_cast<int>(i + 1), juce::dontSendNotification); }
        }
        samples.setTooltip("Beam samples stored per frame. Increase this if the image is too complex.");
        samples.onChange = [this] { refresh(); };
        invert.setToggleState(initial.invert, juce::dontSendNotification);
        invert.setTooltip("Invert image colours before tracing, for dark artwork on a light background. Transparency stays unchanged.");
        invert.onClick = [this] { refresh(); };
        frameRate.setName("Video bake frame rate");
        frameRate.setSpec(frameRateSpec);
        frameRate.setValue(initial.videoFrameRate);
        frameRate.setTooltip("Resample video timing at this rate for deterministic seeking and export.");
        frameRate.onChange = [this](double value) { settings.videoFrameRate = value; refresh(); };
        frameRate.onCommit = frameRate.onChange;
        for (auto* field : {&threshold, &frameRate}) { field->setFill(fieldFill()); }
        mode.fill = fieldFill();
        for (auto* combo : {&detail, &samples}) {
            combo->setColour(juce::ComboBox::backgroundColourId, fieldFill());
            combo->setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        }
        for (auto* caption : {&modeLabel, &thresholdLabel, &detailLabel, &samplesLabel, &frameRateLabel, &invertLabel}) { styleCaption(*caption); }
        error.setFont(motion::style::body());
        error.setColour(juce::Label::textColourId, motion::style::error());
        error.setBorderSize({});
        error.setName("Image preparation validation error");
        primary.onClick = [this] {
            if (submitted) { return; }
            refresh();
            if (valid && onPrepare) {
                submitted = true;
                primary.setEnabled(false);
                onPrepare(settings);
            }
        };
        for (auto* component : std::initializer_list<juce::Component*> {&modeLabel, &thresholdLabel, &detailLabel, &samplesLabel, &invertLabel,
                &mode, &threshold, &detail, &samples, &invert, &error}) {
            addAndMakeVisible(component);
        }
        if (isVideo) {
            addAndMakeVisible(frameRateLabel);
            addAndMakeVisible(frameRate);
        }
        if (hasPreview()) { addAndMakeVisible(preview); }
        const auto rows = isVideo ? 6 : 5;
        setSize(widthFor(hasPreview() ? bodyHeight(rows) : 0), heightFor(rows));
        refresh();
    }

    std::function<void(motion::RasterSettings)> onPrepare;

protected:
    void layoutBody(juce::Rectangle<int> area) override {
        if (hasPreview()) {
            preview.setBounds(area.removeFromLeft(area.getHeight()));
            area.removeFromLeft(24);
        }
        formRow(area, modeLabel, mode);
        formRow(area, thresholdLabel, threshold, number);
        formRow(area, detailLabel, detail);
        formRow(area, samplesLabel, samples);
        if (isVideo) { formRow(area, frameRateLabel, frameRate, number); }
        auto line = area.removeFromTop(row);
        invertLabel.setBounds(line.removeFromLeft(caption));
        invert.setBounds(line.removeFromLeft(motion::ui::Switch::width + 4));
        error.setBounds(footerLeft);
    }

private:
    static constexpr motion::PropertySpec thresholdSpec {"threshold", "Threshold", "", "", 0, 100, 2, .1, 1, "%"};
    static constexpr motion::PropertySpec frameRateSpec {"frameRate", "Frame rate", "", "", 1, 120, 30, .5, 3, " fps"};
    bool hasPreview() const { return !isVideo && data.getSize() > 0; }
    void selectResolution(int resolution) {
        const auto found = std::find(resolutions.begin(), resolutions.end(), resolution);
        if (found != resolutions.end()) { detail.setSelectedId(static_cast<int>(std::distance(resolutions.begin(), found) + 1), juce::dontSendNotification); }
    }
    void refresh() {
        auto next = settings;
        const auto detailIndex = detail.getSelectedId() - 1;
        const auto sampleIndex = samples.getSelectedId() - 1;
        juce::String message;
        if (detailIndex < 0 || sampleIndex < 0 || static_cast<std::size_t>(detailIndex) >= resolutions.size() || static_cast<std::size_t>(sampleIndex) >= strides.size()) {
            message = "Choose an image detail and samples per frame.";
        } else {
            next.mode = mode.getSelected() == 0 ? motion::RasterSettings::Mode::contours : motion::RasterSettings::Mode::scanlines;
            next.resolution = resolutions[static_cast<std::size_t>(detailIndex)];
            next.pointsPerFrame = strides[static_cast<std::size_t>(sampleIndex)];
            next.invert = invert.getToggleState();
            if (thresholdEdited) { next.threshold = threshold.getValue() / 100.0; }
            message = juce::String(next.validate());
        }
        valid = message.isEmpty();
        error.setText(message, juce::dontSendNotification);
        primary.setEnabled(valid && !submitted);
        if (!valid) { return; }
        settings = next;
        if (!hasPreview()) { return; }
        preview.request([image = data, chosen = settings](const std::atomic<bool>& cancel) {
            motion::ui::SourcePreview::Result result;
            const auto prepared = motion::RasterSourcePreparer::prepare(image.getData(), image.getSize(), chosen, &cancel);
            if (!prepared) {
                result.error = juce::String(prepared.error);
                return result;
            }
            result.path = motion::ui::traceSource(*prepared.source, 0, static_cast<int>(chosen.pointsPerFrame));
            const auto frames = static_cast<int>(prepared.source->frameCount());
            result.caption = juce::String(static_cast<juce::int64>(chosen.pointsPerFrame)) + " points" + (frames > 1 ? motion::style::dot() + juce::String(frames) + " frames" : juce::String());
            return result;
        });
    }

    motion::RasterSettings settings;
    bool isVideo = false;
    juce::MemoryBlock data;
    bool valid = false, submitted = false, thresholdEdited = false, detailEdited = false;
    std::vector<int> resolutions;
    std::vector<std::size_t> strides;
    motion::ui::SegmentedControl mode {"Image trace mode", {"Outlines", "Scanlines"}};
    motion::ui::ScrubField threshold, frameRate;
    juce::ComboBox detail, samples;
    motion::ui::Switch invert {"Invert image"};
    motion::ui::SourcePreview preview;
    juce::Label modeLabel {"Image mode caption", "Trace"}, thresholdLabel {"Image threshold caption", "Threshold"};
    juce::Label detailLabel {"Image detail caption", "Detail"}, samplesLabel {"Image samples caption", "Samples per frame"};
    juce::Label frameRateLabel {"Video frame rate caption", "Frame rate"}, invertLabel {"Invert caption", "Invert"};
    juce::Label error;
};
