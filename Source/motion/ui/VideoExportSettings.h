#pragma once

#include "CanvasSizeEditor.h"
#include "ScrubField.h"

// How the composition is encoded: codec, quality and whether the
// soundtrack goes with it. The frame is the output canvas at the
// composition's frame rate, shown here and changed where they live.
class MotionVideoExportSettings final : public motion::ui::Sheet {
public:
    MotionVideoExportSettings(VideoEncodingConfiguration initial, const juce::String& composition)
        : Sheet("Export video", composition, "Export..."), config(std::move(initial)) {
        nameAction("Choose file and export...");
        for (auto* component : std::initializer_list<juce::Component*> { &frameLabel, &frame, &codecLabel, &qualityLabel, &soundtrackLabel, &codec, &quality, &soundtrack, &note }) {
            addAndMakeVisible(component);
        }
        const auto rate = juce::String(config.frameRate, config.frameRate == std::round(config.frameRate) ? 0 : 3);
        frame.setName("Export frame");
        frame.setText(juce::String(config.renderSize.width) + juce::String::fromUTF8(" \xc3\x97 ") + juce::String(config.renderSize.height) + " px" + motion::style::dot() + rate + " fps", juce::dontSendNotification);
        frame.setTooltip("The output canvas (the Scope's canvas button) at the composition's frame rate.");
        frame.setFont(motion::style::body());
        frame.setBorderSize({});
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
        for (auto* label : { &frameLabel, &codecLabel, &qualityLabel, &soundtrackLabel, &note }) { styleCaption(*label); }
        note.setText(config.preserveAlpha ? "Transparent background: ProRes 4444 with alpha." : juce::String(), juce::dontSendNotification);
        primary.onClick = [this] {
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
        const auto rows = 4;
        setSize(widthFor(), heightFor(rows, config.preserveAlpha ? 28 : 0));
    }
    std::function<void(VideoEncodingConfiguration)> onExport;

protected:
    void layoutBody(juce::Rectangle<int> area) override {
        formRow(area, frameLabel, frame, 0);
        formRow(area, codecLabel, codec);
        formRow(area, qualityLabel, quality, number);
        auto line = area.removeFromTop(row);
        soundtrackLabel.setBounds(line.removeFromLeft(caption));
        soundtrack.setBounds(line.removeFromLeft(motion::ui::Switch::width + 4));
        area.removeFromTop(rowGap);
        note.setBounds(area);
    }

private:
    static constexpr motion::PropertySpec qualitySpec {"quality", "Quality", "", "", 1, 100, 80, 1, 0, "%"};
    void updateQuality() {
        const auto chosen = static_cast<VideoCodec>(codec.getSelectedId() - 1);
        quality.setEnabled(!VideoEncodingConstants::getVideoCodecInfo(chosen).proRes);
    }
    VideoEncodingConfiguration config;
    juce::Label note, frame;
    juce::Label frameLabel { "Export frame caption", "Frame" }, codecLabel { "Video codec caption", "Codec" }, qualityLabel { "Video quality caption", "Quality" }, soundtrackLabel { "Soundtrack caption", "Soundtrack" };
    juce::ComboBox codec;
    motion::ui::ScrubField quality;
    motion::ui::Switch soundtrack { "Include stereo soundtrack" };
};
