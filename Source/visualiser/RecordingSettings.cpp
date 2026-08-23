#include "RecordingSettings.h"
#include "RecordingStateMigration.h"
#include "VisualiserComponent.h"

RecordingParameters::RecordingParameters() {
    qualityParameter.disableLfo();
    qualityParameter.disableSidechain();
    canvasWidth.disableLfo();
    canvasWidth.disableSidechain();
    canvasHeight.disableLfo();
    canvasHeight.disableSidechain();
    frameRate.disableLfo();
    frameRate.disableSidechain();
}

void RecordingParameters::save(juce::XmlElement* xml) {
    auto settingsXml = xml->createNewChildElement("recordingSettings");
    losslessAudio.save(settingsXml->createNewChildElement("losslessAudio"));
    losslessVideo.save(settingsXml->createNewChildElement("losslessVideo"));
    recordAudio.save(settingsXml->createNewChildElement("recordAudio"));
    recordVideo.save(settingsXml->createNewChildElement("recordVideo"));
    settingsXml->setAttribute("compressionPreset", compressionPreset);
    settingsXml->setAttribute("customTextureOutputName", customTextureOutputName);
    settingsXml->setAttribute("videoCodec", static_cast<int>(videoCodec));

    auto qualityXml = settingsXml->createNewChildElement("quality");
    qualityEffect->save(qualityXml);

    auto canvasWidthXml = settingsXml->createNewChildElement("canvasWidth");
    canvasWidthEffect->save(canvasWidthXml);

    auto canvasHeightXml = settingsXml->createNewChildElement("canvasHeight");
    canvasHeightEffect->save(canvasHeightXml);

    auto frameRateXml = settingsXml->createNewChildElement("frameRate");
    frameRateEffect->save(frameRateXml);
}

void RecordingParameters::load(juce::XmlElement* xml) {
    auto* settingsXml = xml->getChildByName("recordingSettings");
    if (settingsXml == nullptr) {
        return;
    }

    auto* losslessAudioXml = settingsXml->getChildByName("losslessAudio");
    if (losslessAudioXml != nullptr) {
        losslessAudio.load(losslessAudioXml);
    }

    auto* losslessVideoXml = settingsXml->getChildByName("losslessVideo");
    if (losslessVideoXml != nullptr) {
        losslessVideo.load(losslessVideoXml);
    }

    auto* recordAudioXml = settingsXml->getChildByName("recordAudio");
    if (recordAudioXml != nullptr) {
        recordAudio.load(recordAudioXml);
    }

    auto* recordVideoXml = settingsXml->getChildByName("recordVideo");
    if (recordVideoXml != nullptr) {
        recordVideo.load(recordVideoXml);
    }

    if (settingsXml->hasAttribute("compressionPreset")) {
        compressionPreset = settingsXml->getStringAttribute("compressionPreset");
    }

    if (settingsXml->hasAttribute("customTextureOutputName")) {
        customTextureOutputName = settingsXml->getStringAttribute("customTextureOutputName");
    }

    if (settingsXml->hasAttribute("videoCodec")) {
        const int codecValue = settingsXml->getIntAttribute("videoCodec", 0);
        videoCodec = VideoEncodingConstants::videoCodecFromSerializedValue(codecValue);
    }

    auto* qualityXml = settingsXml->getChildByName("quality");
    if (qualityXml != nullptr) {
        qualityEffect->load(qualityXml);
    }

    bool loadedCanvasWidth = false;
    bool loadedCanvasHeight = false;
    auto* canvasWidthXml = settingsXml->getChildByName("canvasWidth");
    if (canvasWidthXml != nullptr) {
        canvasWidthEffect->load(canvasWidthXml);
        loadedCanvasWidth = true;
    }

    auto* canvasHeightXml = settingsXml->getChildByName("canvasHeight");
    if (canvasHeightXml != nullptr) {
        canvasHeightEffect->load(canvasHeightXml);
        loadedCanvasHeight = true;
    }

    if (!(loadedCanvasWidth && loadedCanvasHeight)) {
        const int legacyResolution = RecordingStateMigration::getLegacyResolution(settingsXml);
        if (legacyResolution > 0) {
            setCanvasSize({legacyResolution, legacyResolution});
        }
    }
    sanitiseCanvasParameters();
    canvasPreset = VisualiserGeometry::getPresetForRenderSize(getCanvasSize());

    auto* frameRateXml = settingsXml->getChildByName("frameRate");
    if (frameRateXml != nullptr) {
        frameRateEffect->load(frameRateXml);
    }
}

VisualiserRenderSize RecordingParameters::getCanvasSize() {
    return VisualiserGeometry::sanitiseRenderSize(juce::roundToInt(canvasWidth.getValueUnnormalised()),
                                                  juce::roundToInt(canvasHeight.getValueUnnormalised()));
}

void RecordingParameters::setCanvasSize(VisualiserRenderSize size) {
    size = VisualiserGeometry::sanitiseRenderSize(size.width, size.height);
    canvasWidth.setUnnormalisedValueNotifyingHost(static_cast<float>(size.width));
    canvasHeight.setUnnormalisedValueNotifyingHost(static_cast<float>(size.height));
    canvasPreset = VisualiserGeometry::getPresetForRenderSize(size);
}

void RecordingParameters::sanitiseCanvasParameters() {
    const auto size = getCanvasSize();
    if (canvasWidth.getValueUnnormalised() != size.width) {
        canvasWidth.setUnnormalisedValueNotifyingHost(static_cast<float>(size.width));
    }
    if (canvasHeight.getValueUnnormalised() != size.height) {
        canvasHeight.setUnnormalisedValueNotifyingHost(static_cast<float>(size.height));
    }
}

RecordingSettings::RecordingSettings(RecordingParameters& ps, VisualiserParameters& visualiserParameters)
    : parameters(ps), visualiserParameters(visualiserParameters) {
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
    addAndMakeVisible(customTextureOutputLabel);
    addAndMakeVisible(customTextureOutputEditor);

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
    visualiserParameters.transparentBackground->addListener(this);
    startTimerHz(30);

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
    losslessVideo.onClick = [this] {
        updateVideoEncodingControls();
    };
    compressionPreset.onChange = [this] {
        parameters.compressionPreset = parameters.compressionPresets[compressionPreset.getSelectedId() - 1];
    };
    compressionPreset.addItemList(parameters.compressionPresets, 1);
    compressionPreset.setSelectedId(parameters.compressionPresets.indexOf(parameters.compressionPreset) + 1);
    compressionPresetLabel.setTooltip("The compression preset to use when recording video. Slower presets will produce smaller files at the expense of encoding time.");

    // Setup the codec selector
    for (const auto& codec : VideoEncodingConstants::videoCodecs) {
        videoCodecSelector.addItem(codec.displayName, static_cast<int>(codec.codec) + 1);
    }
    videoCodecSelector.setSelectedId(static_cast<int>(parameters.videoCodec) + 1);
    videoCodecSelector.onChange = [this] {
        parameters.videoCodec = VideoEncodingConstants::videoCodecFromSerializedValue(videoCodecSelector.getSelectedId() - 1);
        const auto& codecInfo = VideoEncodingConstants::getVideoCodecInfo(parameters.videoCodec);
        if (!codecInfo.supportsLosslessAudio) {
            losslessAudio.setToggleState(false, juce::NotificationType::sendNotification);
        }
        updateLosslessAudioEnabled();
        updateVideoEncodingControls();
    };
    videoCodecLabel.setTooltip("The video codec to use when recording. Different codecs offer different trade-offs between quality, file size, and compatibility.");

    customTextureOutputLabel.setTooltip("Custom source name for texture output. Avoid using the same name for multiple running outputs.");
    customTextureOutputEditor.setText(parameters.customTextureOutputName);
    customTextureOutputEditor.onTextChange = [this] {
        parameters.customTextureOutputName = customTextureOutputEditor.getText();
    };
    updateVideoEncodingControls();
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
    stopTimer();
    parameters.canvasWidth.removeListener(this);
    parameters.canvasHeight.removeListener(this);
#if OSCI_PREMIUM
    visualiserParameters.transparentBackground->removeListener(this);
#endif
}

void RecordingSettings::updateLosslessAudioEnabled() {
    const auto& codecInfo = VideoEncodingConstants::getVideoCodecInfo(getVideoCodec());
    losslessAudio.setEnabled(recordAudio.getToggleState() && codecInfo.supportsLosslessAudio);
}

void RecordingSettings::updateVideoEncodingControls() {
#if OSCI_PREMIUM
    const bool transparencyRequired = visualiserParameters.isTransparentBackgroundEnabled();
    const auto effectiveCodec = getVideoCodec();
    const auto& codecInfo = VideoEncodingConstants::getVideoCodecInfo(effectiveCodec);

    videoCodecSelector.setSelectedId(static_cast<int>(effectiveCodec) + 1, juce::dontSendNotification);
    videoCodecSelector.setEnabled(!transparencyRequired);
    videoCodecLabel.setText(transparencyRequired ? "Video Codec (required for transparency)" : "Video Codec", juce::dontSendNotification);
    losslessVideo.setEnabled(!codecInfo.proRes);
    quality.setEnabled(!codecInfo.proRes && !losslessVideo.getToggleState());
    compressionPreset.setEnabled(!codecInfo.proRes);
    compressionPresetLabel.setEnabled(!codecInfo.proRes);
    updateLosslessAudioEnabled();
#endif
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
    juce::ignoreUnused(newValue);
#if OSCI_PREMIUM
    const bool transparencyChanged = parameterIndex == visualiserParameters.transparentBackground->getParameterIndex();
#else
    const bool transparencyChanged = false;
    juce::ignoreUnused(parameterIndex);
#endif
    auto updates = static_cast<unsigned int>(videoEncodingControlsUpdate);
    if (!transparencyChanged) {
        updates |= canvasControlsUpdate;
    }
    pendingParameterUpdates.fetch_or(updates, std::memory_order_release);
}

void RecordingSettings::timerCallback() {
    const auto updates = pendingParameterUpdates.exchange(0, std::memory_order_acquire);
    if (updates == 0) {
        return;
    }
    if ((updates & videoEncodingControlsUpdate) != 0) {
        updateVideoEncodingControls();
    }
    if ((updates & canvasControlsUpdate) != 0) {
        parameters.sanitiseCanvasParameters();
        parameters.canvasPreset = VisualiserGeometry::getPresetForRenderSize(parameters.getCanvasSize());
        updateCanvasPresetSelector();
        updateCanvasControlsVisibility();
    }
}

void RecordingSettings::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {
    juce::ignoreUnused(parameterIndex, gestureIsStarting);
}

void RecordingSettings::resized() {
    auto area = getLocalBounds().reduced(20);
    double rowHeight = 30;

#if OSCI_PREMIUM
    const auto labelWidth = 170;
    const auto controlWidth = juce::jlimit(100, 190, area.getWidth() - labelWidth - 20);

    losslessAudio.setBounds(area.removeFromTop(rowHeight));
    losslessVideo.setBounds(area.removeFromTop(rowHeight));
    quality.setBounds(area.removeFromTop(rowHeight).expanded(6, 0));
    auto row = area.removeFromTop(rowHeight);
    canvasPresetLabel.setBounds(row.removeFromLeft(labelWidth));
    canvasPresetSelector.setBounds(row.removeFromRight(controlWidth));
    if (canvasWidth.isVisible()) {
        canvasWidth.setBounds(area.removeFromTop(rowHeight).expanded(6, 0));
        canvasHeight.setBounds(area.removeFromTop(rowHeight).expanded(6, 0));
    }
    frameRate.setBounds(area.removeFromTop(rowHeight).expanded(6, 0));
    recordAudio.setBounds(area.removeFromTop(rowHeight));
    recordVideo.setBounds(area.removeFromTop(rowHeight));

    row = area.removeFromTop(rowHeight);
    compressionPresetLabel.setBounds(row.removeFromLeft(labelWidth));
    compressionPreset.setBounds(row.removeFromRight(controlWidth));

    area.removeFromTop(5);
    row = area.removeFromTop(rowHeight);
    videoCodecLabel.setBounds(row.removeFromLeft(labelWidth));
    videoCodecSelector.setBounds(row.removeFromRight(controlWidth));

    area.removeFromTop(5);
    row = area.removeFromTop(rowHeight);
    customTextureOutputLabel.setBounds(row.removeFromLeft(labelWidth));
    customTextureOutputEditor.setBounds(row.removeFromRight(controlWidth));
#else
    recordVideoWarning.setBounds(area.removeFromTop(2 * rowHeight));
    area.removeFromTop(rowHeight / 2);
    sosciLink.setBounds(area.removeFromTop(rowHeight));
#endif

}
