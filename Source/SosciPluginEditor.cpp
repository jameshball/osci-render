#include "SosciPluginProcessor.h"
#include "SosciPluginEditor.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

SosciPluginEditor::SosciPluginEditor(SosciAudioProcessor& p)
	: AudioProcessorEditor(&p), audioProcessor(p)
{
#if JUCE_LINUX
    // use OpenGL on Linux for much better performance. The default on Mac is CoreGraphics, and on Window is Direct2D which is much faster.
    openGlContext.attachTo(*getTopLevelComponent());
#endif

    setLookAndFeel(&lookAndFeel);

    menuBar.setModel(&menuBarModel);
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

    visualiserSettingsWindow.setResizable(false, false);
#if JUCE_WINDOWS
    // if not standalone, use native title bar for compatibility with DAWs
    visualiserSettingsWindow.setUsingNativeTitleBar(processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone);
#elif JUCE_MAC
    visualiserSettingsWindow.setUsingNativeTitleBar(true);
#endif
    visualiserSettings.setLookAndFeel(&getLookAndFeel());
    visualiserSettings.setSize(550, 400);
    visualiserSettingsWindow.setContentNonOwned(&visualiserSettings, true);
    visualiserSettingsWindow.centreWithSize(550, 400);
    
    menuBar.toFront(true);

    setSize(700, 750);
    setResizable(true, true);
    setResizeLimits(250, 250, 999999, 999999);
}

SosciPluginEditor::~SosciPluginEditor() {
    setLookAndFeel(nullptr);
    juce::Desktop::getInstance().setDefaultLookAndFeel(nullptr);
}

void SosciPluginEditor::paint(juce::Graphics& g) {
    g.fillAll(Colours::veryDark);
}

void SosciPluginEditor::resized() {
    auto topBar = getLocalBounds().removeFromTop(25).removeFromLeft(200);
    menuBar.setBounds(topBar);
    auto visualiserArea = getLocalBounds();
    visualiserArea.removeFromTop(25);
    visualiser.setBounds(visualiserArea);
}

bool SosciPluginEditor::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown() && key.getKeyCode() == 'S') {
        saveProjectAs();
    } else if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S') {
        saveProject();
    } else if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'O') {
        openProject();
    }

    return false;
}

void SosciPluginEditor::openProject() {
    chooser = std::make_unique<juce::FileChooser>("Load sosci Project", audioProcessor.lastOpenedDirectory, "*.sosci");
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

void SosciPluginEditor::saveProject() {
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

void SosciPluginEditor::saveProjectAs() {
    chooser = std::make_unique<juce::FileChooser>("Save sosci Project", audioProcessor.lastOpenedDirectory, "*.sosci");
    auto flags = juce::FileBrowserComponent::saveMode;

    chooser->launchAsync(flags, [this](const juce::FileChooser& chooser) {
        auto file = chooser.getResult();
        if (file != juce::File()) {
            audioProcessor.currentProjectFile = file.getFullPathName();
            saveProject();
        }
    });
}

void SosciPluginEditor::updateTitle() {
    juce::String title = "sosci";
    if (!audioProcessor.currentProjectFile.isEmpty()) {
        title += " - " + audioProcessor.currentProjectFile;
    }
    getTopLevelComponent()->setName(title);
}

void SosciPluginEditor::openAudioSettings() {
    juce::StandalonePluginHolder* standalone = juce::StandalonePluginHolder::getInstance();
    standalone->showAudioSettingsDialog();
}

void SosciPluginEditor::resetToDefault() {
    juce::StandaloneFilterWindow* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
    if (window != nullptr) {
        window->resetToDefaultState();
    }
}

void SosciPluginEditor::openVisualiserSettings() {
    visualiserSettingsWindow.setVisible(true);
    visualiserSettingsWindow.toFront(true);
}
