#include "SosciPluginProcessor.h"
#include "SosciPluginEditor.h"
#include "CustomStandaloneFilterWindow.h"

SosciPluginEditor::SosciPluginEditor(SosciAudioProcessor& p) : CommonPluginEditor(p, "sosci", "sosci", 1180, 750), audioProcessor(p) {
    // Create timeline controller for audio playback
    audioTimelineController = std::make_shared<AudioTimelineController>(audioProcessor);
    
    // Listen for audio file changes
    audioProcessor.addAudioPlayerListener(this);
    
    initialiseMenuBar(model);
    if (juce::JUCEApplication::isStandaloneApp()) {
        addAndMakeVisible(volume);
    }
    addAndMakeVisible(visualiserSettingsWrapper);

    osci::BooleanParameter* visualiserFullScreen = audioProcessor.visualiserParameters.visualiserFullScreen;
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
    
    // Set initial timeline controller state
    updateTimelineController();
}

SosciPluginEditor::~SosciPluginEditor() {
    audioProcessor.removeAudioPlayerListener(this);
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
            auto volumeArea = area.removeFromLeft(35);
            volume.setBounds(volumeArea.withSizeKeepingCentre(30, juce::jmin(volumeArea.getHeight(), 300)));
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
    return file.hasFileExtension("wav") ||
           file.hasFileExtension("mp3") ||
           file.hasFileExtension("aiff") ||
           file.hasFileExtension("flac") ||
           file.hasFileExtension("ogg") ||
           file.hasFileExtension("sosci");
}

void SosciPluginEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    juce::ignoreUnused(x, y);
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
    juce::Component::SafePointer<SosciPluginEditor> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis != nullptr)
            safeThis->visualiserFullScreenChanged();
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

void SosciPluginEditor::parserChanged() {
    updateTimelineController();
}

void SosciPluginEditor::updateTimelineController() {
    // Show timeline when audio file is loaded
    std::shared_ptr<TimelineController> controller = audioProcessor.wavParser.isInitialised() 
        ? audioTimelineController 
        : nullptr;
    
    visualiser.setTimelineController(controller);
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
