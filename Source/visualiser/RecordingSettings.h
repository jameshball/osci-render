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
        "Quality",
        "Controls the quality of the recording video. 0 is the worst possible quality, and 1 is lossless.",
        "brightness",
        VERSION_HINT, 0.7, 0.0, 1.0
    );
    Effect qualityEffect = Effect(&qualityParameter);

    BooleanParameter recordAudio = BooleanParameter("Record Audio", "recordAudio", VERSION_HINT, true, "Record audio along with the video.");
    BooleanParameter recordVideo = BooleanParameter("Record Video", "recordVideo", VERSION_HINT, sosciFeatures, "Record video output of the visualiser.");

    juce::String compressionPreset = "fast";

    int getCRF() {
        double quality = juce::jlimit(0.0, 1.0, qualityEffect.getValue());
        // mapping to 0-51 for ffmpeg's crf value
        return 51 * (1.0 - quality) ;
    }

    bool recordingVideo() {
        return recordVideo.getBoolValue();
    }

    bool recordingAudio() {
        return recordAudio.getBoolValue();
    }

    juce::String getCompressionPreset() {
        return compressionPreset;
    }

    void save(juce::XmlElement* xml) {
        auto settingsXml = xml->createNewChildElement("recordingSettings");
        recordAudio.save(settingsXml->createNewChildElement("recordAudio"));
        recordVideo.save(settingsXml->createNewChildElement("recordVideo"));
        settingsXml->setAttribute("compressionPreset", compressionPreset);
        
        auto qualityXml = settingsXml->createNewChildElement("quality");
        qualityEffect.save(qualityXml);
    }

    // opt to not change any values if not found
    void load(juce::XmlElement* xml) {
        if (auto* settingsXml = xml->getChildByName("recordingSettings")) {
            if (auto* recordAudioXml = settingsXml->getChildByName("recordAudio")) {
                recordAudio.load(recordAudioXml);
            }
            if (auto* recordVideoXml = settingsXml->getChildByName("recordVideo")) {
                recordVideo.load(recordVideoXml);
            }
            if (settingsXml->hasAttribute("compressionPreset")) {
                compressionPreset = settingsXml->getStringAttribute("compressionPreset");
            }
            if (auto* qualityXml = settingsXml->getChildByName("quality")) {
                qualityEffect.load(qualityXml);
            }
        }
    }
   
    juce::StringArray compressionPresets = { "ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow" };
};

class RecordingSettings : public juce::Component {
public:
    RecordingSettings(RecordingParameters&);
    ~RecordingSettings();

    void resized() override;

    RecordingParameters& parameters;

private:
    EffectComponent quality{parameters.qualityEffect};

    jux::SwitchButton recordAudio{&parameters.recordAudio};
    jux::SwitchButton recordVideo{&parameters.recordVideo};
    
#if !SOSCI_FEATURES
    juce::TextEditor recordVideoWarning{"recordVideoWarning"};
    juce::HyperlinkButton sosciLink{"Purchase here", juce::URL("https://osci-render.com/sosci")};
#endif

    juce::Label compressionPresetLabel{"Compression Speed", "Compression Speed"};
    juce::ComboBox compressionPreset;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordingSettings)
};
