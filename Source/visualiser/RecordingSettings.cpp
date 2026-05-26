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
        int codecValue = settingsXml->getIntAttribute("videoCodec", 0);
        videoCodec = static_cast<VideoCodec>(codecValue);
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

    customTextureOutputLabel.setTooltip("Custom source name for texture output. Avoid using the same name for multiple running outputs.");
    customTextureOutputEditor.setText(parameters.customTextureOutputName);
    customTextureOutputEditor.onTextChange = [this] {
        parameters.customTextureOutputName = customTextureOutputEditor.getText();
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
