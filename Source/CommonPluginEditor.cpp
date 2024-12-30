#include "CommonPluginProcessor.h"
#include "CommonPluginEditor.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

CommonPluginEditor::CommonPluginEditor(CommonAudioProcessor& p, juce::String appName, juce::String projectFileType, int width, int height)
	: AudioProcessorEditor(&p), audioProcessor(p), appName(appName), projectFileType(projectFileType)
{
    if (!applicationFolder.exists()) {
        applicationFolder.createDirectory();
    }
    
#if JUCE_LINUX
    // use OpenGL on Linux for much better performance. The default on Mac is CoreGraphics, and on Window is Direct2D which is much faster.
    openGlContext.attachTo(*getTopLevelComponent());
#endif

    setLookAndFeel(&lookAndFeel);

    addAndMakeVisible(menuBar);

    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        if (juce::TopLevelWindow::getNumTopLevelWindows() > 0) {
            juce::TopLevelWindow* w = juce::TopLevelWindow::getTopLevelWindow(0);
            juce::DocumentWindow* dw = dynamic_cast<juce::DocumentWindow*>(w);
            if (dw != nullptr) {
                dw->setBackgroundColour(Colours::veryDark);
                dw->setColour(juce::ResizableWindow::backgroundColourId, Colours::veryDark);
                dw->setTitleBarButtonsRequired(juce::DocumentWindow::allButtons, false);
                dw->setUsingNativeTitleBar(true);
            }
        }

        juce::StandalonePluginHolder* standalone = juce::StandalonePluginHolder::getInstance();
        if (standalone != nullptr) {
            standalone->getMuteInputValue().setValue(false);
        }
    }
    
    addAndMakeVisible(visualiser);

    visualiser.openSettings = [this] {
        openVisualiserSettings();
    };

    visualiser.closeSettings = [this] {
        visualiserSettingsWindow.setVisible(false);
    };

    visualiserSettings.setLookAndFeel(&getLookAndFeel());
    visualiserSettings.setSize(550, 450);
    visualiserSettingsWindow.setContentNonOwned(&visualiserSettings, true);
    visualiserSettingsWindow.centreWithSize(550, 450);
#if JUCE_WINDOWS
    // if not standalone, use native title bar for compatibility with DAWs
    visualiserSettingsWindow.setUsingNativeTitleBar(processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone);
#elif JUCE_MAC
    visualiserSettingsWindow.setUsingNativeTitleBar(true);
#endif

    recordingSettings.setLookAndFeel(&getLookAndFeel());
    recordingSettings.setSize(300, 200);
    recordingSettingsWindow.setContentNonOwned(&recordingSettings, true);
    recordingSettingsWindow.centreWithSize(300, 200);
#if JUCE_WINDOWS
    // if not standalone, use native title bar for compatibility with DAWs
    recordingSettingsWindow.setUsingNativeTitleBar(processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone);
#elif JUCE_MAC
    recordingSettingsWindow.setUsingNativeTitleBar(true);
#endif
    
    menuBar.toFront(true);

    setSize(width, height);
    setResizable(true, true);
    setResizeLimits(250, 250, 999999, 999999);

    tooltipDropShadow.setOwner(&tooltipWindow);

#if SOSCI_FEATURES
    SharedTextureManager::getInstance()->initGL();
#endif
}

void CommonPluginEditor::initialiseMenuBar(juce::MenuBarModel& menuBarModel) {
    menuBar.setModel(&menuBarModel);
}

CommonPluginEditor::~CommonPluginEditor() {
    if (audioProcessor.haltRecording != nullptr) {
        audioProcessor.haltRecording();
    }
    setLookAndFeel(nullptr);
    juce::Desktop::getInstance().setDefaultLookAndFeel(nullptr);
}

bool CommonPluginEditor::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown() && key.getKeyCode() == 'S') {
        saveProjectAs();
    } else if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S') {
        saveProject();
    } else if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'O') {
        openProject();
    }

    return false;
}

void CommonPluginEditor::openProject() {
    chooser = std::make_unique<juce::FileChooser>("Load " + appName + " Project", audioProcessor.lastOpenedDirectory, "*." + projectFileType);
    auto flags = juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this](const juce::FileChooser& chooser) {
        auto file = chooser.getResult();
        if (file != juce::File()) {
            auto data = juce::MemoryBlock();
            if (file.loadFileAsData(data)) {
                audioProcessor.setStateInformation(data.getData(), data.getSize());
            }
            audioProcessor.currentProjectFile = file.getFullPathName();
            audioProcessor.lastOpenedDirectory = file.getParentDirectory();
            updateTitle();
        }
    });
}

void CommonPluginEditor::saveProject() {
    if (audioProcessor.currentProjectFile.isEmpty()) {
        saveProjectAs();
    } else {
        auto data = juce::MemoryBlock();
        audioProcessor.getStateInformation(data);
        auto file = juce::File(audioProcessor.currentProjectFile);
        file.create();
        file.replaceWithData(data.getData(), data.getSize());
        updateTitle();
    }
}

void CommonPluginEditor::saveProjectAs() {
    chooser = std::make_unique<juce::FileChooser>("Save " + appName + " Project", audioProcessor.lastOpenedDirectory, "*." + projectFileType);
    auto flags = juce::FileBrowserComponent::saveMode;

    chooser->launchAsync(flags, [this](const juce::FileChooser& chooser) {
        auto file = chooser.getResult();
        if (file != juce::File()) {
            audioProcessor.currentProjectFile = file.getFullPathName();
            saveProject();
        }
    });
}

void CommonPluginEditor::updateTitle() {
    juce::String title = appName;
    if (!audioProcessor.currentProjectFile.isEmpty()) {
        appName += " - " + audioProcessor.currentProjectFile;
    }
    getTopLevelComponent()->setName(title);
}

void CommonPluginEditor::openAudioSettings() {
    juce::StandalonePluginHolder* standalone = juce::StandalonePluginHolder::getInstance();
    standalone->showAudioSettingsDialog();
}

void CommonPluginEditor::openRecordingSettings() {
    recordingSettingsWindow.setVisible(true);
    recordingSettingsWindow.toFront(true);
}

void CommonPluginEditor::resetToDefault() {
    juce::StandaloneFilterWindow* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
    if (window != nullptr) {
        window->resetToDefaultState();
    }
}

void CommonPluginEditor::openVisualiserSettings() {
    visualiserSettingsWindow.setVisible(true);
    visualiserSettingsWindow.toFront(true);
}
