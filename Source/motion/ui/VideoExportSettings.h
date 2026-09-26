#pragma once

#include "../../visualiser/RecordingSettings.h"

class MotionVideoExportSettings final : public juce::Component {
public:
    explicit MotionVideoExportSettings(VideoEncodingConfiguration initial) : config(std::move(initial)) {
        for (auto* component : std::initializer_list<juce::Component*> { &frameRate, &widthLabel, &heightLabel, &codecLabel, &qualityLabel, &width, &height, &codec, &quality, &soundtrack, &note, &error, &exportButton }) {
            addAndMakeVisible(component);
        }
        frameRate.setText(juce::String(config.frameRate, 3) + " fps  /  Project frame rate", juce::dontSendNotification);
        frameRate.setName("Video project frame rate");
        width.setName("Video width");
        height.setName("Video height");
        width.setInputRestrictions(4, "0123456789");
        height.setInputRestrictions(4, "0123456789");
        width.setText(juce::String(config.renderSize.width));
        height.setText(juce::String(config.renderSize.height));
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
                                         : "Renders the complete composition with the current beam style.", juce::dontSendNotification);
        note.setJustificationType(juce::Justification::topLeft);
        error.setColour(juce::Label::textColourId, juce::Colour(0xffe98080));
        exportButton.setButtonText("Choose file and export...");
        exportButton.onClick = [this] {
            const auto w = width.getText().getIntValue();
            const auto h = height.getText().getIntValue();
            if (w < 128 || w > 4096 || h < 128 || h > 4096 || w % 2 != 0 || h % 2 != 0) {
                error.setText("Use even dimensions from 128 to 4096 pixels.", juce::dontSendNotification);
                return;
            }
            config.renderSize = { w, h };
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
        auto area = getLocalBounds().reduced(12);
        frameRate.setBounds(area.removeFromTop(30));
        area.removeFromTop(12);
        auto captions = area.removeFromTop(20);
        widthLabel.setBounds(captions.removeFromLeft((captions.getWidth() - 20) / 2));
        captions.removeFromLeft(20);
        heightLabel.setBounds(captions);
        auto row = area.removeFromTop(28);
        width.setBounds(row.removeFromLeft((row.getWidth() - 20) / 2));
        row.removeFromLeft(20);
        height.setBounds(row);
        codecLabel.setBounds(area.removeFromTop(26));
        codec.setBounds(area.removeFromTop(28));
        qualityLabel.setBounds(area.removeFromTop(26));
        quality.setBounds(area.removeFromTop(28));
        area.removeFromTop(10);
        soundtrack.setBounds(area.removeFromTop(28));
        note.setBounds(area.removeFromTop(36));
        error.setBounds(area.removeFromTop(24));
        exportButton.setBounds(area.removeFromTop(32));
    }
private:
    void updateQuality() {
        const auto chosen = static_cast<VideoCodec>(codec.getSelectedId() - 1);
        quality.setEnabled(!VideoEncodingConstants::getVideoCodecInfo(chosen).proRes);
    }
    VideoEncodingConfiguration config;
    juce::Label frameRate, note, error;
    juce::Label widthLabel { "Video width caption", "Width (pixels)" }, heightLabel { "Video height caption", "Height (pixels)" };
    juce::Label codecLabel { "Video codec caption", "Codec" }, qualityLabel { "Video quality caption", "Quality" };
    juce::TextEditor width, height;
    juce::ComboBox codec;
    juce::Slider quality;
    juce::ToggleButton soundtrack;
    juce::TextButton exportButton;
};
