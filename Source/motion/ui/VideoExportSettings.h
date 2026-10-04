#pragma once

#include "CanvasSizeEditor.h"

class MotionVideoExportSettings final : public juce::Component {
public:
    explicit MotionVideoExportSettings(VideoEncodingConfiguration initial) : canvas(initial.renderSize), config(std::move(initial)) {
        for (auto* component : std::initializer_list<juce::Component*> { &frameRate, &canvas, &codecLabel, &qualityLabel, &codec, &quality, &soundtrack, &note, &error, &exportButton }) {
            addAndMakeVisible(component);
        }
        frameRate.setText(juce::String(config.frameRate, 3) + " fps, the composition's frame rate", juce::dontSendNotification);
        frameRate.setName("Video project frame rate");
        codec.setName("Video codec");
        for (const auto& item : VideoEncodingConstants::videoCodecs) {
            codec.addItem(item.displayName, static_cast<int>(item.codec) + 1);
        }
        if (config.preserveAlpha) { config.codec = VideoCodec::ProRes4444; }
        codec.setSelectedId(static_cast<int>(config.codec) + 1, juce::dontSendNotification);
        codec.setEnabled(!config.preserveAlpha);
        codec.onChange = [this] { updateQuality(); };
        quality.setName("Video quality");
        quality.setSliderStyle(juce::Slider::LinearHorizontal);
        quality.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 24);
        quality.setRange(1, 100, 1);
        quality.setValue(std::clamp(100.0 - (config.crf - 1) * 2.0, 1.0, 100.0));
        soundtrack.setButtonText("Include stereo soundtrack");
        soundtrack.setToggleState(config.includeAudio, juce::dontSendNotification);
        note.setText(config.preserveAlpha ? "Transparent background: ProRes 4444 with alpha."
                                         : juce::String(), juce::dontSendNotification);
        for (auto* label : { &frameRate, &codecLabel, &qualityLabel, &note }) { motion::style::dialog::caption(*label); }
        note.setJustificationType(juce::Justification::topLeft);
        error.setFont(motion::style::body());
        error.setBorderSize({});
        error.setColour(juce::Label::textColourId, motion::style::danger());
        soundtrack.setColour(juce::ToggleButton::textColourId, motion::style::text());
        canvas.onChange = [this] { error.setText({}, juce::dontSendNotification); };
        exportButton.setButtonText("Choose file and export...");
        exportButton.onClick = [this] {
            const auto size = canvas.value();
            if (!size.has_value()) {
                error.setText("Use even dimensions from 128 to 4096 pixels.", juce::dontSendNotification);
                return;
            }
            config.renderSize = *size;
            config.codec = static_cast<VideoCodec>(codec.getSelectedId() - 1);
            config.crf = juce::roundToInt(51.0 - quality.getValue() * 0.5);
            const auto& info = VideoEncodingConstants::getVideoCodecInfo(config.codec);
            config.fileExtension = config.preserveAlpha ? "mov" : info.defaultFileExtension;
            config.audioCodecArgs = info.proRes ? juce::StringArray { "-c:a", "pcm_s16le" }
                                               : juce::StringArray { "-c:a", "aac", "-b:a", "384k" };
            config.includeAudio = soundtrack.getToggleState();
            if (onExport) { onExport(config); }
        };
        updateQuality();
    }
    std::function<void(VideoEncodingConfiguration)> onExport;
    void resized() override {
        using namespace motion::style::dialog;
        auto area = getLocalBounds().reduced(margin);
        error.setBounds(footer(area, { &exportButton }, 180));
        frameRate.setBounds(area.removeFromTop(20));
        area.removeFromTop(rowGap);
        canvas.setBounds(area.removeFromTop(MotionCanvasSizeEditor::preferredHeight));
        area.removeFromTop(rowGap);
        formRow(area, codecLabel, codec, 72);
        formRow(area, qualityLabel, quality, 72);
        // The tick box lines up with the fields' left edge.
        soundtrack.setBounds(area.removeFromTop(row).withTrimmedLeft(72 - 4));
        area.removeFromTop(rowGap);
        note.setBounds(area);
    }
private:
    void updateQuality() {
        const auto chosen = static_cast<VideoCodec>(codec.getSelectedId() - 1);
        quality.setEnabled(!VideoEncodingConstants::getVideoCodecInfo(chosen).proRes);
    }
    MotionCanvasSizeEditor canvas;
    VideoEncodingConfiguration config;
    juce::Label frameRate, note, error;
    juce::Label codecLabel { "Video codec caption", "Codec" }, qualityLabel { "Video quality caption", "Quality" };
    juce::ComboBox codec;
    juce::Slider quality;
    juce::ToggleButton soundtrack;
    juce::TextButton exportButton;
};
