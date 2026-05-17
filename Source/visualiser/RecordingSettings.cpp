#include "RecordingSettings.h"
#include "VisualiserComponent.h"


RecordingSettings::RecordingSettings(RecordingParameters& ps) : parameters(ps) {
#if OSCI_PREMIUM
    addAndMakeVisible(quality);
    addAndMakeVisible(canvasPresetSelector);
    addAndMakeVisible(canvasPresetLabel);
    addAndMakeVisible(canvasWidth);
    addAndMakeVisible(canvasHeight);
    addAndMakeVisible(frameRate);
    addAndMakeVisible(losslessAudio);
    addAndMakeVisible(losslessVideo);
    addAndMakeVisible(recordAudio);
    addAndMakeVisible(recordVideo);
    addAndMakeVisible(compressionPreset);
    addAndMakeVisible(compressionPresetLabel);
    addAndMakeVisible(videoCodecSelector);
    addAndMakeVisible(videoCodecLabel);
    addAndMakeVisible(customSharedTextureOutputLabel);
    addAndMakeVisible(customSharedTextureOutputEditor);

    quality.setRangeEnabled(false);
    canvasWidth.setRangeEnabled(false);
    canvasHeight.setRangeEnabled(false);
    frameRate.setRangeEnabled(false);

    canvasPresetSelector.addItem(VisualiserGeometry::getPresetName(VisualiserCanvasPreset::Square), static_cast<int>(VisualiserCanvasPreset::Square));
    canvasPresetSelector.addItem(VisualiserGeometry::getPresetName(VisualiserCanvasPreset::HDLandscape), static_cast<int>(VisualiserCanvasPreset::HDLandscape));
    canvasPresetSelector.addItem(VisualiserGeometry::getPresetName(VisualiserCanvasPreset::HDPortrait), static_cast<int>(VisualiserCanvasPreset::HDPortrait));
    canvasPresetSelector.addItem(VisualiserGeometry::getPresetName(VisualiserCanvasPreset::Custom), static_cast<int>(VisualiserCanvasPreset::Custom));
    canvasPresetSelector.onChange = [this] {
        auto selectedPreset = VisualiserGeometry::sanitiseCanvasPreset(canvasPresetSelector.getSelectedId());
        parameters.canvasPreset = selectedPreset;
        if (selectedPreset != VisualiserCanvasPreset::Custom) {
            parameters.setCanvasSize(VisualiserGeometry::getRenderSizeForPreset(selectedPreset));
        }
        updateCanvasControlsVisibility();
    };
    updateCanvasPresetSelector();
    updateCanvasControlsVisibility();
    canvasPresetLabel.setTooltip("The visualiser canvas size used for preview, recording, offline video rendering, and Syphon/Spout output.");

    parameters.canvasWidth.addListener(this);
    parameters.canvasHeight.addListener(this);

    updateLosslessAudioEnabled();
    recordAudio.onClick = [this] {
        if (!recordAudio.getToggleState() && !recordVideo.getToggleState()) {
            recordVideo.setToggleState(true, juce::NotificationType::sendNotification);
        }
        updateLosslessAudioEnabled();
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

    // Setup the codec selector
    videoCodecSelector.addItem("H.264", static_cast<int>(VideoCodec::H264) + 1);
    videoCodecSelector.addItem("H.265/HEVC", static_cast<int>(VideoCodec::H265) + 1);
    videoCodecSelector.addItem("VP9", static_cast<int>(VideoCodec::VP9) + 1);
#if JUCE_MAC
    videoCodecSelector.addItem("ProRes", static_cast<int>(VideoCodec::ProRes) + 1);
#endif
    videoCodecSelector.setSelectedId(static_cast<int>(parameters.videoCodec) + 1);
    videoCodecSelector.onChange = [this] {
        parameters.videoCodec = static_cast<VideoCodec>(videoCodecSelector.getSelectedId() - 1);
        if (parameters.videoCodec == VideoCodec::VP9) {
            losslessAudio.setToggleState(false, juce::NotificationType::sendNotification);
        }
        updateLosslessAudioEnabled();
    };
    videoCodecLabel.setTooltip("The video codec to use when recording. Different codecs offer different trade-offs between quality, file size, and compatibility.");

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
    recordVideoWarning.setColour(juce::TextEditor::textColourId, osci::Colours::accentColor());
    recordVideoWarning.setMultiLine(true);
    recordVideoWarning.setReadOnly(true);
    recordVideoWarning.setInterceptsMouseClicks(false, false);

    sosciLink.setColour(juce::HyperlinkButton::textColourId, osci::Colours::accentColor());
#endif
}

RecordingSettings::~RecordingSettings() {
    parameters.canvasWidth.removeListener(this);
    parameters.canvasHeight.removeListener(this);
}

void RecordingSettings::updateLosslessAudioEnabled() {
    losslessAudio.setEnabled(recordAudio.getToggleState()
                             && parameters.videoCodec != VideoCodec::VP9);
}

void RecordingSettings::updateCanvasPresetSelector() {
    const auto size = parameters.getCanvasSize();
    auto preset = VisualiserGeometry::getPresetForRenderSize(size);
    if (parameters.canvasPreset == VisualiserCanvasPreset::Custom) {
        preset = VisualiserCanvasPreset::Custom;
    } else {
        parameters.canvasPreset = preset;
    }
    canvasPresetSelector.setSelectedId(static_cast<int>(preset), juce::dontSendNotification);
}

void RecordingSettings::updateCanvasControlsVisibility() {
    const bool custom = parameters.canvasPreset == VisualiserCanvasPreset::Custom;
    canvasWidth.setVisible(custom);
    canvasHeight.setVisible(custom);
    canvasWidth.setEnabled(custom);
    canvasHeight.setEnabled(custom);
    resized();
    repaint();
}

void RecordingSettings::parameterValueChanged(int parameterIndex, float newValue) {
    juce::ignoreUnused(parameterIndex, newValue);
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<RecordingSettings>(this)] {
        if (safeThis == nullptr) {
            return;
        }
        safeThis->parameters.sanitiseCanvasParameters();
        safeThis->parameters.canvasPreset = VisualiserGeometry::getPresetForRenderSize(safeThis->parameters.getCanvasSize());
        safeThis->updateCanvasPresetSelector();
        safeThis->updateCanvasControlsVisibility();
    });
}

void RecordingSettings::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {
    juce::ignoreUnused(parameterIndex, gestureIsStarting);
}

void RecordingSettings::resized() {
	auto area = getLocalBounds().reduced(20);
    double rowHeight = 30;

#if OSCI_PREMIUM
    losslessAudio.setBounds(area.removeFromTop(rowHeight));
    losslessVideo.setBounds(area.removeFromTop(rowHeight));
    quality.setBounds(area.removeFromTop(rowHeight).expanded(6, 0));
    auto row = area.removeFromTop(rowHeight);
    canvasPresetLabel.setBounds(row.removeFromLeft(170));
    canvasPresetSelector.setBounds(row.removeFromRight(100));
    if (canvasWidth.isVisible()) {
        canvasWidth.setBounds(area.removeFromTop(rowHeight).expanded(6, 0));
        canvasHeight.setBounds(area.removeFromTop(rowHeight).expanded(6, 0));
    }
    frameRate.setBounds(area.removeFromTop(rowHeight).expanded(6, 0));
    recordAudio.setBounds(area.removeFromTop(rowHeight));
    recordVideo.setBounds(area.removeFromTop(rowHeight));

    row = area.removeFromTop(rowHeight);
    compressionPresetLabel.setBounds(row.removeFromLeft(170));
    compressionPreset.setBounds(row.removeFromRight(100));

    area.removeFromTop(5);
    row = area.removeFromTop(rowHeight);
    videoCodecLabel.setBounds(row.removeFromLeft(170));
    videoCodecSelector.setBounds(row.removeFromRight(100));

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
