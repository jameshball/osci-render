#pragma once

#include "CanvasSizeEditor.h"
#include "ScrubField.h"

// What the exported video looks like: its canvas, codec, quality and
// whether the soundtrack goes with it. The frame rate is the composition's.
class MotionVideoExportSettings final : public motion::ui::Sheet {
public:
    explicit MotionVideoExportSettings(VideoEncodingConfiguration initial)
        : Sheet("Export video", juce::String(initial.frameRate, initial.frameRate == std::round(initial.frameRate) ? 0 : 3) + " fps", "Export..."),
          canvas(initial.renderSize, caption, row, rowGap), config(std::move(initial)) {
        nameAction("Choose file and export...");
        for (auto* component : std::initializer_list<juce::Component*> { &canvas, &codecLabel, &qualityLabel, &soundtrackLabel, &codec, &quality, &soundtrack, &note, &error }) {
            addAndMakeVisible(component);
        }
        codec.setName("Video codec");
        for (const auto& item : VideoEncodingConstants::videoCodecs) {
            codec.addItem(item.displayName, static_cast<int>(item.codec) + 1);
        }
        if (config.preserveAlpha) { config.codec = VideoCodec::ProRes4444; }
        codec.setSelectedId(static_cast<int>(config.codec) + 1, juce::dontSendNotification);
        codec.setEnabled(!config.preserveAlpha);
        codec.onChange = [this] { updateQuality(); };
        quality.setName("Video quality");
        quality.setSpec(qualitySpec);
        quality.setValue(std::clamp(100.0 - (config.crf - 1) * 2.0, 1.0, 100.0));
        quality.setFill(fieldFill());
        soundtrack.setToggleState(config.includeAudio, juce::dontSendNotification);
        soundtrack.setTooltip("Include the stereo soundtrack");
        for (auto* label : { &codecLabel, &qualityLabel, &soundtrackLabel, &note }) { styleCaption(*label); }
        note.setText(config.preserveAlpha ? "Transparent background: ProRes 4444 with alpha." : juce::String(), juce::dontSendNotification);
        error.setFont(motion::style::body());
        error.setBorderSize({});
        error.setColour(juce::Label::textColourId, motion::style::error());
        canvas.onChange = [this] { error.setText({}, juce::dontSendNotification); };
        primary.onClick = [this] {
            const auto size = canvas.value();
            if (!size.has_value()) {
                error.setText("Even sizes, 128-4096 px", juce::dontSendNotification);
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
        const auto rows = MotionCanvasSizeEditor::rows + 3;
        setSize(widthFor(), heightFor(rows, config.preserveAlpha ? 28 : 0));
    }
    std::function<void(VideoEncodingConfiguration)> onExport;

protected:
    void layoutBody(juce::Rectangle<int> area) override {
        canvas.setBounds(area.removeFromTop(canvas.preferredHeight()));
        area.removeFromTop(rowGap);
        formRow(area, codecLabel, codec);
        formRow(area, qualityLabel, quality, number);
        auto line = area.removeFromTop(row);
        soundtrackLabel.setBounds(line.removeFromLeft(caption));
        soundtrack.setBounds(line.removeFromLeft(motion::ui::Switch::width + 4));
        area.removeFromTop(rowGap);
        note.setBounds(area);
        error.setBounds(footerLeft);
    }

private:
    static constexpr motion::PropertySpec qualitySpec {"quality", "Quality", "", "", 1, 100, 80, 1, 0, "%"};
    void updateQuality() {
        const auto chosen = static_cast<VideoCodec>(codec.getSelectedId() - 1);
        quality.setEnabled(!VideoEncodingConstants::getVideoCodecInfo(chosen).proRes);
    }
    MotionCanvasSizeEditor canvas;
    VideoEncodingConfiguration config;
    juce::Label note, error;
    juce::Label codecLabel { "Video codec caption", "Codec" }, qualityLabel { "Video quality caption", "Quality" }, soundtrackLabel { "Soundtrack caption", "Soundtrack" };
    juce::ComboBox codec;
    motion::ui::ScrubField quality;
    motion::ui::Switch soundtrack { "Include stereo soundtrack" };
};
