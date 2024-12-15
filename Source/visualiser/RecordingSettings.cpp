#include "RecordingSettings.h"
#include "VisualiserComponent.h"
#include "../PluginEditor.h"


RecordingSettings::RecordingSettings(RecordingParameters& ps) : parameters(ps) {
    addAndMakeVisible(quality);
    addAndMakeVisible(recordAudio);
    addAndMakeVisible(recordVideo);
    addAndMakeVisible(compressionPreset);
    addAndMakeVisible(compressionPresetLabel);

    quality.setSliderOnValueChange();
    quality.setRangeEnabled(false);
    recordAudio.onClick = [this] {
        if (!recordAudio.getToggleState() && !recordVideo.getToggleState()) {
            recordVideo.setToggleState(true, juce::NotificationType::sendNotification);
        }
    };
    recordVideo.onClick = [this] {
        if (!recordAudio.getToggleState() && !recordVideo.getToggleState()) {
            recordAudio.setToggleState(true, juce::NotificationType::sendNotification);
        }
    };
    compressionPreset.onChange = [this] {
        parameters.compressionPreset = parameters.compressionPresets[compressionPreset.getSelectedId() - 1];
    };
    compressionPreset.addItemList(parameters.compressionPresets, 1);
    compressionPreset.setSelectedId(parameters.compressionPresets.indexOf(parameters.compressionPreset) + 1);
    compressionPresetLabel.setTooltip("The compression preset to use when recording video. Slower presets will produce smaller files at the expense of encoding time.");
}

RecordingSettings::~RecordingSettings() {}

void RecordingSettings::resized() {
	auto area = getLocalBounds().reduced(20);
    double rowHeight = 30;
    quality.setBounds(area.removeFromTop(rowHeight).expanded(6, 0));
    recordAudio.setBounds(area.removeFromTop(rowHeight));
    recordVideo.setBounds(area.removeFromTop(rowHeight));
    auto row = area.removeFromTop(rowHeight);
    compressionPresetLabel.setBounds(row.removeFromLeft(140));
    compressionPreset.setBounds(row.removeFromRight(80));
}
