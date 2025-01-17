#include "RecordingSettings.h"
#include "VisualiserComponent.h"
#include "../PluginEditor.h"


RecordingSettings::RecordingSettings(RecordingParameters& ps) : parameters(ps) {
#if SOSCI_FEATURES
    addAndMakeVisible(quality);
    addAndMakeVisible(losslessVideo);
    addAndMakeVisible(recordAudio);
    addAndMakeVisible(recordVideo);
    addAndMakeVisible(compressionPreset);
    addAndMakeVisible(compressionPresetLabel);
    addAndMakeVisible(customSharedTextureOutputLabel);
    addAndMakeVisible(customSharedTextureOutputEditor);
    
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
    quality.setEnabled(!losslessVideo.getToggleState());
    losslessVideo.onClick = [this] {
        quality.setEnabled(!losslessVideo.getToggleState());
    };
    compressionPreset.onChange = [this] {
        parameters.compressionPreset = parameters.compressionPresets[compressionPreset.getSelectedId() - 1];
    };
    compressionPreset.addItemList(parameters.compressionPresets, 1);
    compressionPreset.setSelectedId(parameters.compressionPresets.indexOf(parameters.compressionPreset) + 1);
    compressionPresetLabel.setTooltip("The compression preset to use when recording video. Slower presets will produce smaller files at the expense of encoding time.");
    
    customSharedTextureOutputLabel.setTooltip("Custom name for when creating a new Syphon/Spout server. WARNING: You should not use the same name when running multiple servers at once!.");
    customSharedTextureOutputEditor.setText(parameters.customSharedTextureServerName);
    customSharedTextureOutputEditor.onTextChange = [this] {
        parameters.customSharedTextureServerName = customSharedTextureOutputEditor.getText();
    };
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
    losslessVideo.setBounds(area.removeFromTop(rowHeight));
    quality.setBounds(area.removeFromTop(rowHeight).expanded(6, 0));
    recordAudio.setBounds(area.removeFromTop(rowHeight));
    recordVideo.setBounds(area.removeFromTop(rowHeight));
    auto row = area.removeFromTop(rowHeight);
    compressionPresetLabel.setBounds(row.removeFromLeft(170));
    compressionPreset.setBounds(row.removeFromRight(100));
    
    area.removeFromTop(5);
    row = area.removeFromTop(rowHeight);
    customSharedTextureOutputLabel.setBounds(row.removeFromLeft(170));
    customSharedTextureOutputEditor.setBounds(row.removeFromRight(100));
#else
    recordVideoWarning.setBounds(area.removeFromTop(2 * rowHeight));
    area.removeFromTop(rowHeight / 2);
    sosciLink.setBounds(area.removeFromTop(rowHeight));
#endif

}
