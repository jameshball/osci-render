#pragma once

#include <JuceHeader.h>
#include "../components/EffectComponent.h"
#include "../components/SvgButton.h"
#include "../LookAndFeel.h"
#include "../components/SwitchButton.h"

#define VERSION_HINT 2

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
        resolution.disableLfo();
        resolution.disableSidechain();
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
    osci::BooleanParameter losslessVideo = osci::BooleanParameter("Lossless Video", "losslessVideo", VERSION_HINT, false, "Record video in a lossless format. WARNING: This is not supported by all media players.");
    osci::Effect qualityEffect = osci::Effect(&qualityParameter);

    osci::BooleanParameter recordAudio = osci::BooleanParameter("Record Audio", "recordAudio", VERSION_HINT, true, "Record audio along with the video.");
    osci::BooleanParameter recordVideo = osci::BooleanParameter("Record Video", "recordVideo", VERSION_HINT, sosciFeatures, "Record video output of the visualiser.");
    
    osci::EffectParameter resolution = osci::EffectParameter(
        "Resolution",
        "The resolution of the recorded video. This only changes when not recording.",
        "resolution",
        VERSION_HINT, 1024, 128, 2048, 1.0
    );
    osci::Effect resolutionEffect = osci::Effect(&resolution);
    
    osci::EffectParameter frameRate = osci::EffectParameter(
        "Frame Rate",
        "The frame rate of the recorded video. This only changes when not recording.",
        "frameRate",
        VERSION_HINT, 60.0, 10, 240, 0.01
    );
    osci::Effect frameRateEffect = osci::Effect(&frameRate);

    juce::String compressionPreset = "fast";
    VideoCodec videoCodec = VideoCodec::H264;

    void save(juce::XmlElement* xml) {
        auto settingsXml = xml->createNewChildElement("recordingSettings");
        losslessVideo.save(settingsXml->createNewChildElement("losslessVideo"));
        recordAudio.save(settingsXml->createNewChildElement("recordAudio"));
        recordVideo.save(settingsXml->createNewChildElement("recordVideo"));
        settingsXml->setAttribute("compressionPreset", compressionPreset);
        settingsXml->setAttribute("customSharedTextureServerName", customSharedTextureServerName);
        settingsXml->setAttribute("videoCodec", static_cast<int>(videoCodec));
        
        auto qualityXml = settingsXml->createNewChildElement("quality");
        qualityEffect.save(qualityXml);
        
        auto resolutionXml = settingsXml->createNewChildElement("resolution");
        resolutionEffect.save(resolutionXml);
        
        auto frameRateXml = settingsXml->createNewChildElement("frameRate");
        frameRateEffect.save(frameRateXml);
    }

    // opt to not change any values if not found
    void load(juce::XmlElement* xml) {
        if (auto* settingsXml = xml->getChildByName("recordingSettings")) {
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
                qualityEffect.load(qualityXml);
            }
            if (auto* resolutionXml = settingsXml->getChildByName("resolution")) {
                resolutionEffect.load(resolutionXml);
            }
            if (auto* frameRateXml = settingsXml->getChildByName("frameRate")) {
                frameRateEffect.load(frameRateXml);
            }
        }
    }
   
    juce::StringArray compressionPresets = { "ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow" };
    juce::String customSharedTextureServerName = "";
};

class RecordingSettings : public juce::Component {
public:
    RecordingSettings(RecordingParameters&);
    ~RecordingSettings();

    void resized() override;

    int getCRF() {
        if (parameters.losslessVideo.getBoolValue()) {
            return 0;
        }
        double quality = juce::jlimit(0.0, 1.0, parameters.qualityEffect.getValue());
        // mapping to 1-51 for ffmpeg's crf value (ignoring 0 as this is lossless and
        // not supported by all media players)
        return 50 * (1.0 - quality) + 1;
    }
    
    int getVideoToolboxQuality() {
        if (parameters.losslessVideo.getBoolValue()) {
            return 100;
        }
        double quality = juce::jlimit(0.0, 1.0, parameters.qualityEffect.getValue());
        return 100 * quality;
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
    
    int getResolution() {
        return parameters.resolution.getValueUnnormalised();
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
            case VideoCodec::VP9:
            default:
                return "mp4";
        }
    }

    RecordingParameters& parameters;

private:
    EffectComponent quality{parameters.qualityEffect};
    EffectComponent resolution{parameters.resolutionEffect};
    EffectComponent frameRate{parameters.frameRateEffect};

    jux::SwitchButton losslessVideo{&parameters.losslessVideo};
    jux::SwitchButton recordAudio{&parameters.recordAudio};
    jux::SwitchButton recordVideo{&parameters.recordVideo};
    
#if !OSCI_PREMIUM
    juce::TextEditor recordVideoWarning{"recordVideoWarning"};
    juce::HyperlinkButton sosciLink{"Purchase here", juce::URL("https://osci-render.com/sosci")};
#endif

    juce::Label compressionPresetLabel{"Compression Speed", "Compression Speed"};
    juce::ComboBox compressionPreset;
    
    juce::Label videoCodecLabel{"Video Codec", "Video Codec"};
    juce::ComboBox videoCodecSelector;
    
    juce::Label customSharedTextureOutputLabel{"Custom Syphon/Spout Name", "Custom Syphon/Spout Name"};
    juce::TextEditor customSharedTextureOutputEditor{"customSharedTextureOutputEditor"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordingSettings)
};
