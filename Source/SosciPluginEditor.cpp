#include "SosciPluginProcessor.h"
#include "SosciPluginEditor.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

SosciPluginEditor::SosciPluginEditor(SosciAudioProcessor& p) : CommonPluginEditor(p, "sosci", "sosci", 1180, 750), audioProcessor(p) {
    initialiseMenuBar(model);
    if (juce::JUCEApplication::isStandaloneApp()) {
        addAndMakeVisible(volume);
    }
    addAndMakeVisible(visualiserSettingsWrapper);

    BooleanParameter* visualiserFullScreen = audioProcessor.visualiserParameters.visualiserFullScreen;
    visualiserFullScreenChanged();

    visualiser.setFullScreenCallback([this, visualiserFullScreen](FullScreenMode mode) {
        if (mode == FullScreenMode::TOGGLE) {
            visualiserFullScreen->setBoolValueNotifyingHost(!visualiserFullScreen->getBoolValue());
        } else if (mode == FullScreenMode::FULL_SCREEN) {
            visualiserFullScreen->setBoolValueNotifyingHost(true);
        } else if (mode == FullScreenMode::MAIN_COMPONENT) {
            visualiserFullScreen->setBoolValueNotifyingHost(false);
        }

        visualiserFullScreenChanged();
    });

    resized();
    visualiserFullScreen->addListener(this);

    if (juce::JUCEApplication::isStandaloneApp()) {
        juce::StandalonePluginHolder* standalone = juce::StandalonePluginHolder::getInstance();
        juce::AudioDeviceManager& manager = standalone->deviceManager;
        manager.addChangeListener(this);
        currentInputDevice = getInputDeviceName();
    }
}

SosciPluginEditor::~SosciPluginEditor() {
    if (juce::JUCEApplication::isStandaloneApp()) {
        juce::StandalonePluginHolder* standalone = juce::StandalonePluginHolder::getInstance();
        juce::AudioDeviceManager& manager = standalone->deviceManager;
        manager.removeChangeListener(this);
    }
    audioProcessor.visualiserParameters.visualiserFullScreen->removeListener(this);
    menuBar.setModel(nullptr);
}

void SosciPluginEditor::paint(juce::Graphics& g) {
    g.fillAll(Colours::veryDark);
}

void SosciPluginEditor::resized() {
    CommonPluginEditor::resized();
    auto area = getLocalBounds();

    if (audioProcessor.visualiserParameters.visualiserFullScreen->getBoolValue()) {
        visualiser.setBounds(area);
    } else {
        menuBar.setBounds(area.removeFromTop(25));

        if (juce::JUCEApplication::isStandaloneApp()) {
            auto volumeArea = area.removeFromLeft(30);
            volume.setBounds(volumeArea.withSizeKeepingCentre(volumeArea.getWidth(), juce::jmin(volumeArea.getHeight(), 300)));
        }

        auto settingsArea = area.removeFromRight(juce::jmax(juce::jmin(0.4 * getWidth(), 550.0), 350.0));
        visualiserSettings.setSize(settingsArea.getWidth(), VISUALISER_SETTINGS_HEIGHT);
        visualiserSettingsWrapper.setBounds(settingsArea);

        if (area.getWidth() < 10) {
            area.setWidth(10);
        }
        if (area.getHeight() < 10) {
            area.setHeight(10);
        }
        visualiser.setBounds(area);
    }
}

bool SosciPluginEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    if (files.size() != 1) {
        return false;
    }
    juce::File file(files[0]);
    return 
        file.hasFileExtension("wav") ||
        file.hasFileExtension("mp3") ||
        file.hasFileExtension("aiff") ||
        file.hasFileExtension("flac") ||
        file.hasFileExtension("ogg") ||
        file.hasFileExtension("sosci");
}

void SosciPluginEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    if (files.size() != 1) {
        return;
    }
    juce::File file(files[0]);
    
    if (file.hasFileExtension("sosci")) {
        openProject(file);
    } else {
        audioProcessor.loadAudioFile(file);
    }
}

void SosciPluginEditor::visualiserFullScreenChanged() {
    bool fullScreen = audioProcessor.visualiserParameters.visualiserFullScreen->getBoolValue();
    
    visualiser.setFullScreen(fullScreen);

    if (juce::JUCEApplication::isStandaloneApp()) {
        volume.setVisible(!fullScreen);
    }
    visualiserSettingsWrapper.setVisible(!fullScreen);
    menuBar.setVisible(!fullScreen);
    resized();
    repaint();
}

void SosciPluginEditor::parameterValueChanged(int parameterIndex, float newValue) {
    juce::MessageManager::callAsync([this] {
        visualiserFullScreenChanged();
    });
}

void SosciPluginEditor::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void SosciPluginEditor::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (juce::JUCEApplication::isStandaloneApp()) {
        juce::String inputDevice = getInputDeviceName();
        if (inputDevice != currentInputDevice) {
            currentInputDevice = inputDevice;
            // switch to getting audio from input if the user changes the input device
            // because we assume they are debugging and want to hear the audio from mic
            audioProcessor.stopAudioFile();
        }
    }
}

juce::String SosciPluginEditor::getInputDeviceName() {
    if (juce::StandalonePluginHolder::getInstance() == nullptr) {
        return "Unknown";
    }
    juce::StandalonePluginHolder* standalone = juce::StandalonePluginHolder::getInstance();
    juce::AudioDeviceManager& manager = standalone->deviceManager;
    auto device = manager.getCurrentAudioDevice();
    auto deviceType = manager.getCurrentDeviceTypeObject();
    int inputIndex = deviceType->getIndexOfDevice(device, true);
    auto inputName = deviceType->getDeviceNames(true)[inputIndex];
    return inputName;
}
