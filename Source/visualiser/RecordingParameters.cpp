#include "RecordingParameters.h"
#include "RecordingStateMigration.h"

int RecordingParameters::getCRF() const {
    if (losslessVideo.getBoolValue()) {
        return 0;
    }
    const double quality = juce::jlimit(0.0f, 1.0f, qualityEffect->getValue());
    // Round canonical CRF values back to themselves despite float storage error.
    // CRF 0 remains reserved for the explicit lossless setting.
    return juce::roundToInt(50.0 * (1.0 - quality) + 1.0);
}

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
    } else if (settingsXml->hasAttribute("customSharedTextureServerName")) {
        customTextureOutputName = settingsXml->getStringAttribute("customSharedTextureServerName");
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

