#pragma once

#include <JuceHeader.h>
#include "../components/EffectComponent.h"
#include "../components/SvgButton.h"
#include "../LookAndFeel.h"
#include "../components/SwitchButton.h"

#define VERSION_HINT 2

class RecordingParameters {
public:
    RecordingParameters() {
        qualityParameter.disableLfo();
        qualityParameter.disableSidechain();
    }
    
private:

#if SOSCI_FEATURES
    const bool sosciFeatures = true;
#else
    const bool sosciFeatures = false;
#endif
    
public:

    EffectParameter qualityParameter = EffectParameter(
        "Video Quality",
        "Controls the quality of the recording video. 0 is the worst possible quality, and 1 is almost lossless.",
        "brightness",
        VERSION_HINT, 0.7, 0.0, 1.0
    );
    BooleanParameter losslessVideo = BooleanParameter("Lossless Video", "losslessVideo", VERSION_HINT, false, "Record video in a lossless format. WARNING: This is not supported by all media players.");
    Effect qualityEffect = Effect(&qualityParameter);

    BooleanParameter recordAudio = BooleanParameter("Record Audio", "recordAudio", VERSION_HINT, true, "Record audio along with the video.");
    BooleanParameter recordVideo = BooleanParameter("Record Video", "recordVideo", VERSION_HINT, sosciFeatures, "Record video output of the visualiser.");

    juce::String compressionPreset = "fast";

    void save(juce::XmlElement* xml) {
        auto settingsXml = xml->createNewChildElement("recordingSettings");
        losslessVideo.save(settingsXml->createNewChildElement("losslessVideo"));
        recordAudio.save(settingsXml->createNewChildElement("recordAudio"));
        recordVideo.save(settingsXml->createNewChildElement("recordVideo"));
        settingsXml->setAttribute("compressionPreset", compressionPreset);
        settingsXml->setAttribute("customSharedTextureServerName", customSharedTextureServerName);
        
        auto qualityXml = settingsXml->createNewChildElement("quality");
        qualityEffect.save(qualityXml);
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
            if (auto* qualityXml = settingsXml->getChildByName("quality")) {
                qualityEffect.load(qualityXml);
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

    RecordingParameters& parameters;

private:
    EffectComponent quality{parameters.qualityEffect};

    jux::SwitchButton losslessVideo{&parameters.losslessVideo};
    jux::SwitchButton recordAudio{&parameters.recordAudio};
    jux::SwitchButton recordVideo{&parameters.recordVideo};
    
#if !SOSCI_FEATURES
    juce::TextEditor recordVideoWarning{"recordVideoWarning"};
    juce::HyperlinkButton sosciLink{"Purchase here", juce::URL("https://osci-render.com/sosci")};
#endif

    juce::Label compressionPresetLabel{"Compression Speed", "Compression Speed"};
    juce::ComboBox compressionPreset;
    
    juce::Label customSharedTextureOutputLabel{"Custom Syphon/Spout Name", "Custom Syphon/Spout Name"};
    juce::TextEditor customSharedTextureOutputEditor{"customSharedTextureOutputEditor"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordingSettings)
};
