#include "CommonPluginProcessor.h"
#include "CommonPluginEditor.h"
#include "CustomStandaloneFilterWindow.h"

CommonPluginEditor::CommonPluginEditor(CommonAudioProcessor& p, juce::String appName, juce::String projectFileType, int defaultWidth, int defaultHeight)
    : AudioProcessorEditor(&p), audioProcessor(p), appName(appName), projectFileType(projectFileType)
{
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
            standalone->commandLineCallback = [this](const juce::String& commandLine) {
                handleCommandLine(commandLine);
            };
        }
    }
    
    addAndMakeVisible(visualiser);
    
    int width = std::any_cast<int>(audioProcessor.getProperty("appWidth", defaultWidth));
    int height = std::any_cast<int>(audioProcessor.getProperty("appHeight", defaultHeight));

    visualiserSettings.setLookAndFeel(&getLookAndFeel());
    visualiserSettings.setSize(550, VISUALISER_SETTINGS_HEIGHT);
    visualiserSettings.setColour(juce::ResizableWindow::backgroundColourId, Colours::dark);

    recordingSettings.setLookAndFeel(&getLookAndFeel());
    recordingSettings.setSize(300, 330);
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

    tooltipDropShadow.setOwner(&tooltipWindow.get());
    tooltipWindow->setMillisecondsBeforeTipAppears(0);
    
    updateTitle();

#if OSCI_PREMIUM
    sharedTextureManager.initGL();
#endif
}

void CommonPluginEditor::handleCommandLine(const juce::String& commandLine) {
    if (commandLine.trim().isNotEmpty()) {
        // Split the command line into tokens, using space as delimiter
        // and handling quoted arguments as one token.
        juce::StringArray tokens = juce::StringArray::fromTokens(commandLine, " ", "\"");
        
        if (tokens.size() > 0) {
            // Use the first token as the file path and trim any extra whitespace.
            juce::String filePath = tokens[0].trim();
            filePath = filePath.unquoted();
            juce::File file = juce::File::createFileWithoutCheckingPath(filePath);
            
            if (file.existsAsFile()) {
                if (file.getFileExtension().toLowerCase() == "." + projectFileType.toLowerCase()) {
                    openProject(file);
                } else {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Invalid Command Line", "Invalid file type: " + file.getFullPathName());
                }
            } else {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Invalid Command Line", "File not found: " + filePath);
            }
        }
    }
}

void CommonPluginEditor::resized() {
    audioProcessor.setProperty("appWidth", getWidth());
    audioProcessor.setProperty("appHeight", getHeight());
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
    // If we're not accepting special keys, end early
    if (!audioProcessor.getAcceptsKeys()) return false;

    if (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown() && key.getKeyCode() == 'S') {
        saveProjectAs();
    } else if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S') {
        saveProject();
    } else if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'O') {
        openProject();
    } else if (key.isKeyCode(juce::KeyPress::F11Key) && juce::JUCEApplicationBase::isStandaloneApp()) {
        // set fullscreen
        juce::StandaloneFilterWindow* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
        if (window != nullptr) {
            window->setFullScreen(!fullScreen);
        }
    }

    return false;
}

void CommonPluginEditor::openProject(const juce::File& file) {
    if (file != juce::File()) {
        auto data = juce::MemoryBlock();
        if (file.loadFileAsData(data)) {
            audioProcessor.setStateInformation(data.getData(), data.getSize());
        }
        audioProcessor.currentProjectFile = file.getFullPathName();
        audioProcessor.setLastOpenedDirectory(file.getParentDirectory());
        updateTitle();
    }
}

void CommonPluginEditor::openProject() {
    chooser = std::make_unique<juce::FileChooser>("Load " + appName + " Project", audioProcessor.getLastOpenedDirectory(), "*." + projectFileType);
    auto flags = juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this](const juce::FileChooser& chooser) {
        openProject(chooser.getResult());
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
    chooser = std::make_unique<juce::FileChooser>("Save " + appName + " Project", audioProcessor.getLastOpenedDirectory(), "*." + projectFileType);
    auto flags = juce::FileBrowserComponent::saveMode;

    chooser->launchAsync(flags, [this](const juce::FileChooser& chooser) {
        auto file = chooser.getResult();
        if (file != juce::File()) {
            audioProcessor.setLastOpenedDirectory(file.getParentDirectory());
            audioProcessor.currentProjectFile = file.getFullPathName();
            saveProject();
        }
    });
}

void CommonPluginEditor::updateTitle() {
    juce::String title = appName;
    if (!audioProcessor.currentProjectFile.isEmpty()) {
        title += " - " + audioProcessor.currentProjectFile;
    }
    if (currentFileName.isNotEmpty()) {
        title += " - " + currentFileName;
    }
    getTopLevelComponent()->setName(title);
}

void CommonPluginEditor::fileUpdated(juce::String fileName) {
    currentFileName = fileName;
    updateTitle();
}

void CommonPluginEditor::openAudioSettings() {
    juce::StandalonePluginHolder* standalone = juce::StandalonePluginHolder::getInstance();
    standalone->showAudioSettingsDialog();
}

void CommonPluginEditor::openRecordingSettings() {
    recordingSettingsWindow.setVisible(true);
}

void CommonPluginEditor::resetToDefault() {
    juce::StandaloneFilterWindow* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
    if (window != nullptr) {
        window->resetToDefaultState();
        window->setName(ProjectInfo::projectName);
    }
}
