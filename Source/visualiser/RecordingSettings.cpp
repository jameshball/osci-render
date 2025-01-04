#include "RecordingSettings.h"
#include "VisualiserComponent.h"
#include "../PluginEditor.h"


RecordingSettings::RecordingSettings(RecordingParameters& ps) : parameters(ps) {
#if SOSCI_FEATURES
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
#else
    addAndMakeVisible(recordVideoWarning);
    addAndMakeVisible(sosciLink);
    
    recordVideoWarning.setText("Recording video is only available in osci-render premium or sosci.");
    recordVideoWarning.setJustification(juce::Justification::centred);
    recordVideoWarning.setColour(juce::TextEditor::textColourId, Colours::accentColor);
    recordVideoWarning.setMultiLine(true);
    recordVideoWarning.setReadOnly(true);
    recordVideoWarning.setInterceptsMouseClicks(false, false);
    
    sosciLink.setColour(juce::HyperlinkButton::textColourId, Colours::accentColor);
#endif
}

RecordingSettings::~RecordingSettings() {}

void RecordingSettings::resized() {
	auto area = getLocalBounds().reduced(20);
    double rowHeight = 30;
    
#if SOSCI_FEATURES
    quality.setBounds(area.removeFromTop(rowHeight).expanded(6, 0));
    recordAudio.setBounds(area.removeFromTop(rowHeight));
    recordVideo.setBounds(area.removeFromTop(rowHeight));
    auto row = area.removeFromTop(rowHeight);
    compressionPresetLabel.setBounds(row.removeFromLeft(140));
    compressionPreset.setBounds(row.removeFromRight(80));
#else
    recordVideoWarning.setBounds(area.removeFromTop(2 * rowHeight));
    area.removeFromTop(rowHeight / 2);
    sosciLink.setBounds(area.removeFromTop(rowHeight));
#endif

}
