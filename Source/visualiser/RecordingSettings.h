#pragma once

#include <JuceHeader.h>
#include "../components/effects/EffectComponent.h"
#include <osci_gui/osci_gui.h>
#include "../LookAndFeel.h"
#include <osci_gui/visualiser/osci_VisualiserGeometry.h>
#include <osci_gui/visualiser/osci_VisualiserParameters.h>
#include "../video/VideoEncodingConstants.h"

class RecordingParameters {
public:
    RecordingParameters();

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

    void save(juce::XmlElement* xml);

    // opt to not change any values if not found
    void load(juce::XmlElement* xml);

    juce::StringArray compressionPresets = { "ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow" };
    juce::String customTextureOutputName = "";

    VisualiserRenderSize getCanvasSize();
    void setCanvasSize(VisualiserRenderSize size);
    void sanitiseCanvasParameters();
};

struct VideoEncodingConfiguration {
    VideoCodec codec = VideoCodec::H264;
    VisualiserRenderSize renderSize;
    double frameRate = 60.0;
    int crf = 1;
    juce::String compressionPreset;
    juce::String fileExtension;
    juce::StringArray audioCodecArgs;
    bool includeAudio = true;
    bool preserveAlpha = false;
};

class RecordingSettings : public juce::Component, public juce::AudioProcessorParameter::Listener, private juce::Timer {
public:
    RecordingSettings(RecordingParameters&, VisualiserParameters&);
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

    juce::String getCustomTextureOutputName() {
        if (parameters.customTextureOutputName.isEmpty()) {
            parameters.customTextureOutputName = "osci-render - " + juce::String(juce::Time::getCurrentTime().toMilliseconds());
        }
        return parameters.customTextureOutputName;
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

    VideoCodec getVideoCodec() const {
        if (visualiserParameters.isTransparentBackgroundEnabled()) {
            return VideoCodec::ProRes4444;
        }
        return parameters.videoCodec;
    }

    VideoEncodingConfiguration createVideoEncodingConfiguration() {
        const auto codec = getVideoCodec();
        const auto& codecInfo = VideoEncodingConstants::getVideoCodecInfo(codec);
        const bool losslessAudio = parameters.losslessAudio.getBoolValue() && codecInfo.supportsLosslessAudio;
        return {
            .codec = codec,
            .renderSize = getCanvasSize(),
            .frameRate = getFrameRate(),
            .crf = getCRF(),
            .compressionPreset = getCompressionPreset(),
            .fileExtension = losslessAudio ? "mov" : codecInfo.defaultFileExtension,
            .audioCodecArgs = losslessAudio ? juce::StringArray{"-c:a", "pcm_s16le"}
                                            : juce::StringArray{"-c:a", "aac", "-b:a", "384k"},
            .includeAudio = recordingAudio(),
            .preserveAlpha = visualiserParameters.isTransparentBackgroundEnabled(),
        };
    }

    RecordingParameters& parameters;
    VisualiserParameters& visualiserParameters;

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

    juce::Label customTextureOutputLabel{"Texture Output Name", "Texture Output Name"};
    osci::TextEditor customTextureOutputEditor{"customTextureOutputEditor"};

    void updateLosslessAudioEnabled();
    void updateVideoEncodingControls();
    void updateCanvasPresetSelector();
    void updateCanvasControlsVisibility();
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void timerCallback() override;

    enum PendingParameterUpdate : unsigned int {
        videoEncodingControlsUpdate = 1u << 0,
        canvasControlsUpdate = 1u << 1,
    };

    std::atomic<unsigned int> pendingParameterUpdates { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordingSettings)
};
