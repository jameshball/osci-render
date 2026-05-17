#pragma once

#include <JuceHeader.h>
#include "../components/effects/EffectComponent.h"
#include <osci_gui/osci_gui.h>
#include "../LookAndFeel.h"
#include "../components/SwitchButton.h"
#include "VisualiserGeometry.h"

// Define codec options
enum class VideoCodec {
    H264,
    H265,
    VP9,
#if JUCE_MAC
    ProRes,
#endif
};

class RecordingParameters {
public:
    RecordingParameters() {
        qualityParameter.disableLfo();
        qualityParameter.disableSidechain();
        canvasWidth.disableLfo();
        canvasWidth.disableSidechain();
        canvasHeight.disableLfo();
        canvasHeight.disableSidechain();
        frameRate.disableLfo();
        frameRate.disableSidechain();
    }

private:

#if OSCI_PREMIUM
    const bool sosciFeatures = true;
#else
    const bool sosciFeatures = false;
#endif

public:

    osci::EffectParameter qualityParameter = osci::EffectParameter(
        "Video Quality",
        "Controls the quality of the recording video. 0 is the worst possible quality, and 1 is almost lossless.",
        "brightness",
        VERSION_HINT, 0.7, 0.0, 1.0
    );
    osci::BooleanParameter losslessAudio = osci::BooleanParameter("Lossless Audio", "losslessAudio", VERSION_HINT, false, "Record audio in a lossless format.");
    osci::BooleanParameter losslessVideo = osci::BooleanParameter("Lossless Video", "losslessVideo", VERSION_HINT, false, "Record video in a lossless format. WARNING: This is not supported by all media players.");
    std::shared_ptr<osci::Effect> qualityEffect = std::make_shared<osci::SimpleEffect>(&qualityParameter);

    osci::BooleanParameter recordAudio = osci::BooleanParameter("Record Audio", "recordAudio", VERSION_HINT, true, "Record audio along with the video.");
    osci::BooleanParameter recordVideo = osci::BooleanParameter("Record Video", "recordVideo", VERSION_HINT, sosciFeatures, "Record video output of the visualiser.");

    VisualiserCanvasPreset canvasPreset = VisualiserCanvasPreset::Square;

    osci::EffectParameter canvasWidth = osci::EffectParameter(
        "Canvas Width",
        "The width of the visualiser canvas and recorded video. This only changes when not recording.",
        "canvasWidth",
        VERSION_HINT, 1024, VisualiserGeometry::minCanvasDimension, VisualiserGeometry::maxCanvasDimension, 2.0
    );
    std::shared_ptr<osci::Effect> canvasWidthEffect = std::make_shared<osci::SimpleEffect>(&canvasWidth);

    osci::EffectParameter canvasHeight = osci::EffectParameter(
        "Canvas Height",
        "The height of the visualiser canvas and recorded video. This only changes when not recording.",
        "canvasHeight",
        VERSION_HINT, 1024, VisualiserGeometry::minCanvasDimension, VisualiserGeometry::maxCanvasDimension, 2.0
    );
    std::shared_ptr<osci::Effect> canvasHeightEffect = std::make_shared<osci::SimpleEffect>(&canvasHeight);

    osci::EffectParameter frameRate = osci::EffectParameter(
        "Frame Rate",
        "The frame rate of the recorded video. This only changes when not recording.",
        "frameRate",
        VERSION_HINT, 60.0, 10, 240, 0.01
    );
    std::shared_ptr<osci::Effect> frameRateEffect = std::make_shared<osci::SimpleEffect>(&frameRate);

    juce::String compressionPreset = "fast";
    VideoCodec videoCodec = VideoCodec::H264;

    void save(juce::XmlElement* xml) {
        auto settingsXml = xml->createNewChildElement("recordingSettings");
        losslessAudio.save(settingsXml->createNewChildElement("losslessAudio"));
        losslessVideo.save(settingsXml->createNewChildElement("losslessVideo"));
        recordAudio.save(settingsXml->createNewChildElement("recordAudio"));
        recordVideo.save(settingsXml->createNewChildElement("recordVideo"));
        settingsXml->setAttribute("compressionPreset", compressionPreset);
        settingsXml->setAttribute("customSharedTextureServerName", customSharedTextureServerName);
        settingsXml->setAttribute("videoCodec", static_cast<int>(videoCodec));

        auto qualityXml = settingsXml->createNewChildElement("quality");
        qualityEffect->save(qualityXml);

        settingsXml->setAttribute("canvasPreset", static_cast<int>(canvasPreset));

        auto canvasWidthXml = settingsXml->createNewChildElement("canvasWidth");
        canvasWidthEffect->save(canvasWidthXml);

        auto canvasHeightXml = settingsXml->createNewChildElement("canvasHeight");
        canvasHeightEffect->save(canvasHeightXml);

        auto frameRateXml = settingsXml->createNewChildElement("frameRate");
        frameRateEffect->save(frameRateXml);
    }

    // opt to not change any values if not found
    void load(juce::XmlElement* xml) {
        if (auto* settingsXml = xml->getChildByName("recordingSettings")) {
            if (auto* losslessAudioXml = settingsXml->getChildByName("losslessAudio")) {
                losslessAudio.load(losslessAudioXml);
            }
            if (auto* losslessVideoXml = settingsXml->getChildByName("losslessVideo")) {
                losslessVideo.load(losslessVideoXml);
            }
            if (auto* recordAudioXml = settingsXml->getChildByName("recordAudio")) {
                recordAudio.load(recordAudioXml);
            }
            if (auto* recordVideoXml = settingsXml->getChildByName("recordVideo")) {
                recordVideo.load(recordVideoXml);
            }
            if (settingsXml->hasAttribute("compressionPreset")) {
                compressionPreset = settingsXml->getStringAttribute("compressionPreset");
            }
            if (settingsXml->hasAttribute("customSharedTextureServerName")) {
                customSharedTextureServerName = settingsXml->getStringAttribute("customSharedTextureServerName");
            }
            if (settingsXml->hasAttribute("videoCodec")) {
                int codecValue = settingsXml->getIntAttribute("videoCodec", 0);
                videoCodec = static_cast<VideoCodec>(codecValue);
            }
            if (auto* qualityXml = settingsXml->getChildByName("quality")) {
                qualityEffect->load(qualityXml);
            }
            bool loadedCanvasSize = false;
            if (auto* canvasWidthXml = settingsXml->getChildByName("canvasWidth")) {
                canvasWidthEffect->load(canvasWidthXml);
                loadedCanvasSize = true;
            }
            if (auto* canvasHeightXml = settingsXml->getChildByName("canvasHeight")) {
                canvasHeightEffect->load(canvasHeightXml);
                loadedCanvasSize = true;
            }
            if (!loadedCanvasSize) {
                if (auto* resolutionXml = settingsXml->getChildByName("resolution")) {
                    const int legacyResolution = VisualiserGeometry::getLegacyResolutionFromXml(resolutionXml);
                    if (legacyResolution > 0) {
                        setCanvasSize({legacyResolution, legacyResolution});
                    }
                }
            }
            sanitiseCanvasParameters();
            if (settingsXml->hasAttribute("canvasPreset")) {
                const auto fallbackPreset = VisualiserGeometry::getPresetForRenderSize(getCanvasSize());
                canvasPreset = VisualiserGeometry::sanitiseCanvasPreset(settingsXml->getIntAttribute("canvasPreset", static_cast<int>(fallbackPreset)), fallbackPreset);
            } else {
                canvasPreset = VisualiserGeometry::getPresetForRenderSize(getCanvasSize());
            }
            if (auto* frameRateXml = settingsXml->getChildByName("frameRate")) {
                frameRateEffect->load(frameRateXml);
            }
        }
    }

    juce::StringArray compressionPresets = { "ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow" };
    juce::String customSharedTextureServerName = "";

    VisualiserRenderSize getCanvasSize() {
        return VisualiserGeometry::sanitiseRenderSize(juce::roundToInt(canvasWidth.getValueUnnormalised()),
                                                      juce::roundToInt(canvasHeight.getValueUnnormalised()));
    }

    void setCanvasSize(VisualiserRenderSize size) {
        size = VisualiserGeometry::sanitiseRenderSize(size.width, size.height);
        canvasWidth.setUnnormalisedValueNotifyingHost(static_cast<float>(size.width));
        canvasHeight.setUnnormalisedValueNotifyingHost(static_cast<float>(size.height));
        canvasPreset = VisualiserGeometry::getPresetForRenderSize(size);
    }

    void sanitiseCanvasParameters() {
        const auto size = getCanvasSize();
        if (canvasWidth.getValueUnnormalised() != size.width) {
            canvasWidth.setUnnormalisedValueNotifyingHost(static_cast<float>(size.width));
        }
        if (canvasHeight.getValueUnnormalised() != size.height) {
            canvasHeight.setUnnormalisedValueNotifyingHost(static_cast<float>(size.height));
        }
    }

};

class RecordingSettings : public juce::Component, public juce::AudioProcessorParameter::Listener {
public:
    RecordingSettings(RecordingParameters&);
    ~RecordingSettings();

    void resized() override;

    int getCRF() {
        if (parameters.losslessVideo.getBoolValue()) {
            return 0;
        }
        double quality = juce::jlimit(0.0f, 1.0f, parameters.qualityEffect->getValue());
        // mapping to 1-51 for ffmpeg's crf value (ignoring 0 as this is lossless and
        // not supported by all media players)
        return 50 * (1.0 - quality) + 1;
    }

    bool recordingVideo() {
        return parameters.recordVideo.getBoolValue();
    }

    bool recordingAudio() {
        return parameters.recordAudio.getBoolValue();
    }

    juce::String getCompressionPreset() {
        return parameters.compressionPreset;
    }

    juce::String getCustomSharedTextureServerName() {
        if (parameters.customSharedTextureServerName.isEmpty()) {
            return "osci-render - " + juce::String(juce::Time::getCurrentTime().toMilliseconds());
        }
        return parameters.customSharedTextureServerName;
    }

    VisualiserRenderSize getCanvasSize() {
        return parameters.getCanvasSize();
    }

    int getCanvasWidth() {
        return getCanvasSize().width;
    }

    int getCanvasHeight() {
        return getCanvasSize().height;
    }

    double getFrameRate() {
        return parameters.frameRate.getValueUnnormalised();
    }

    VideoCodec getVideoCodec() {
        return parameters.videoCodec;
    }

    juce::String getFileExtensionForCodec() {
        switch (parameters.videoCodec) {
#if JUCE_MAC
            case VideoCodec::ProRes:
                return "mov";
#endif
            case VideoCodec::H264:
            case VideoCodec::H265:
                return parameters.losslessAudio.getBoolValue() ? "mov" : "mp4";
            case VideoCodec::VP9:
            default:
                return "mp4";
        }
    }

    juce::StringArray getAudioCodecArgs() const {
        if (parameters.losslessAudio.getBoolValue() && parameters.videoCodec != VideoCodec::VP9) {
            return {"-c:a", "pcm_s16le"};
        }
        return {"-c:a", "aac", "-b:a", "384k"};
    }

    RecordingParameters& parameters;

private:
    EffectComponent quality{*parameters.qualityEffect};
    EffectComponent canvasWidth{*parameters.canvasWidthEffect};
    EffectComponent canvasHeight{*parameters.canvasHeightEffect};
    EffectComponent frameRate{*parameters.frameRateEffect};

    jux::SwitchButton losslessAudio{&parameters.losslessAudio};
    jux::SwitchButton losslessVideo{&parameters.losslessVideo};
    jux::SwitchButton recordAudio{&parameters.recordAudio};
    jux::SwitchButton recordVideo{&parameters.recordVideo};

#if !OSCI_PREMIUM
    osci::TextEditor recordVideoWarning{"recordVideoWarning"};
    juce::HyperlinkButton sosciLink{"Purchase here", juce::URL("https://osci-render.com/#purchase")};
#endif

    juce::Label compressionPresetLabel{"Compression Speed", "Compression Speed"};
    juce::ComboBox compressionPreset;

    juce::Label canvasPresetLabel{"Resolution", "Resolution"};
    juce::ComboBox canvasPresetSelector;

    juce::Label videoCodecLabel{"Video Codec", "Video Codec"};
    juce::ComboBox videoCodecSelector;

    juce::Label customSharedTextureOutputLabel{"Custom Syphon/Spout Name", "Custom Syphon/Spout Name"};
    osci::TextEditor customSharedTextureOutputEditor{"customSharedTextureOutputEditor"};

    void updateLosslessAudioEnabled();
    void updateCanvasPresetSelector();
    void updateCanvasControlsVisibility();
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordingSettings)
};
