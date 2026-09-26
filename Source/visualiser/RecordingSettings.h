#pragma once

#include <JuceHeader.h>
#include "RecordingParameters.h"
#include "../components/effects/EffectComponent.h"
#include <osci_gui/osci_gui.h>
#include "../LookAndFeel.h"
#include <osci_gui/visualiser/osci_VisualiserGeometry.h>
#include <osci_gui/visualiser/osci_VisualiserParameters.h>
#include "../video/VideoEncodingConstants.h"

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
        return parameters.getCRF();
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
