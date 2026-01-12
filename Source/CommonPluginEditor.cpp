#include "CommonPluginProcessor.h"
#include "CommonPluginEditor.h"
#include "CustomStandaloneFilterWindow.h"

#if OSCI_PREMIUM
#include "visualiser/OfflineAudioToVideoRenderer.h"
#endif

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
            juce::Component::SafePointer<CommonPluginEditor> safeThis(this);
            standalone->commandLineCallback = [safeThis](const juce::String& commandLine) {
                if (safeThis != nullptr)
                    safeThis->handleCommandLine(commandLine);
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

    tooltipWindow->setMillisecondsBeforeTipAppears(100);
    
    updateTitle();

    // On startup (especially standalone state restore), the editor may not yet be attached to a
    // top-level window when updateTitle() is first called. Refresh once the message loop runs.
    juce::Component::SafePointer<CommonPluginEditor> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis != nullptr)
            safeThis->updateTitle();
    });

#if OSCI_PREMIUM
    sharedTextureManager.initGL();
#endif

    // Enable keyboard focus so F11 key works immediately
    setWantsKeyboardFocus(true);
}

void CommonPluginEditor::parentHierarchyChanged()
{
    // Refresh the title when the editor is attached/detached.
    updateTitle();
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

#if OSCI_PREMIUM
    if (offlineRenderDialog != nullptr)
    {
        // Dismiss any active modal dialog so it can auto-delete.
        offlineRenderDialog->exitModalState(0);
        offlineRenderDialog = nullptr;
    }
#endif

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
        // toggle fullscreen
        toggleFullScreen();
    } else if (key.isKeyCode(juce::KeyPress::escapeKey) && juce::JUCEApplicationBase::isStandaloneApp()) {
        // exit fullscreen if we're in fullscreen mode
        // Return true to consume the event and prevent it from reaching child components
        if (fullScreen) {
            toggleFullScreen();
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
        audioProcessor.addRecentProjectFile(file);
        updateTitle();
    }
}

void CommonPluginEditor::openProject() {
    chooser = std::make_unique<juce::FileChooser>("Load " + appName + " Project", audioProcessor.getLastOpenedDirectory(), "*." + projectFileType);
    auto flags = juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles;

    juce::Component::SafePointer<CommonPluginEditor> safeThis(this);
    chooser->launchAsync(flags, [safeThis](const juce::FileChooser& chooser) {
        if (safeThis != nullptr)
            safeThis->openProject(chooser.getResult());
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

    juce::Component::SafePointer<CommonPluginEditor> safeThis(this);
    chooser->launchAsync(flags, [safeThis](const juce::FileChooser& chooser) {
        if (safeThis == nullptr)
            return;

        auto file = chooser.getResult();
        if (file != juce::File()) {
            safeThis->audioProcessor.setLastOpenedDirectory(file.getParentDirectory());
            safeThis->audioProcessor.currentProjectFile = file.getFullPathName();
            safeThis->audioProcessor.addRecentProjectFile(file);
            safeThis->saveProject();
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

void CommonPluginEditor::showPremiumSplashScreen() {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon,
        "Premium Feature",
        "This feature is available in the premium version.");
}

void CommonPluginEditor::renderAudioFileToVideo() {
#if !OSCI_PREMIUM
    showPremiumSplashScreen();
    return;
#else
    if (offlineRenderDialog != nullptr) {
        offlineRenderDialog->toFront(true);
        return;
    }

    // Step 1: choose input audio file
    chooser = std::make_unique<juce::FileChooser>(
        "Choose an input audio file",
        audioProcessor.getLastOpenedDirectory(),
        "*.wav;*.aiff;*.flac;*.ogg;*.mp3");

    auto openFlags = juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles;

    juce::Component::SafePointer<CommonPluginEditor> safeThis(this);
    chooser->launchAsync(openFlags, [safeThis](const juce::FileChooser& inputChooser) {
        if (safeThis == nullptr)
            return;

        const auto inputFile = inputChooser.getResult();
        if (inputFile == juce::File())
            return;

        safeThis->audioProcessor.setLastOpenedDirectory(inputFile.getParentDirectory());

        // Step 2: choose output video file (default: inputName + codec extension)
        const auto ext = safeThis->recordingSettings.getFileExtensionForCodec();
        const auto suggestedOutput = inputFile.getParentDirectory().getChildFile(
            inputFile.getFileNameWithoutExtension() + "." + ext);

        safeThis->chooser = std::make_unique<juce::FileChooser>(
            "Choose an output video file",
            suggestedOutput,
            "*." + ext);

        auto saveFlags = juce::FileBrowserComponent::saveMode |
            juce::FileBrowserComponent::canSelectFiles;

        safeThis->chooser->launchAsync(saveFlags, [safeThis, inputFile, ext](const juce::FileChooser& outputChooser) {
            if (safeThis == nullptr)
                return;

            auto outputFile = outputChooser.getResult();
            if (outputFile == juce::File())
                return;

            // Ensure the file extension matches the codec container by default.
            if (outputFile.getFileExtension().isEmpty())
                outputFile = outputFile.withFileExtension(ext);

            safeThis->audioProcessor.setLastOpenedDirectory(outputFile.getParentDirectory());

            // Ensure FFmpeg exists. If it doesn't, this will prompt the user to download it.
            if (!safeThis->audioProcessor.ensureFFmpegExists())
                return;

            // Stop any live recording and pause the main visualiser.
            if (safeThis->audioProcessor.haltRecording != nullptr)
                safeThis->audioProcessor.haltRecording();

            const bool wasVisualiserPaused = safeThis->visualiser.isPaused();
            const bool wasOfflineRenderActive = safeThis->audioProcessor.isOfflineRenderActive();

            // Make the plugin output silent and skip heavy processing during offline render.
            safeThis->audioProcessor.setOfflineRenderActive(true);

            safeThis->visualiser.setPaused(true);

            auto resultHolder = std::make_shared<std::optional<OfflineAudioToVideoRendererComponent::Result>>();

            auto content = std::make_unique<OfflineAudioToVideoRendererComponent>(
                safeThis->audioProcessor,
                safeThis->audioProcessor.visualiserParameters,
                safeThis->audioProcessor.threadManager,
                safeThis->recordingSettings,
                inputFile,
                outputFile,
                safeThis->visualiser.getRenderMode());

            content->setSize(700, 520);

            // When the render finishes, store the result and dismiss the modal dialog.
            content->setOnFinished([safeThis, resultHolder](OfflineAudioToVideoRendererComponent::Result r) {
                if (safeThis == nullptr)
                    return;

                *resultHolder = r;

                if (auto* dw = safeThis->offlineRenderDialog.getComponent())
                    dw->exitModalState(1);
            });

            auto* contentPtr = content.get();

            juce::DialogWindow::LaunchOptions options;
            options.dialogTitle = "Render Audio File to Video";
            options.dialogBackgroundColour = Colours::dark;
            options.content.setOwned(content.release());
            options.componentToCentreAround = safeThis.getComponent();
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = true;
            options.resizable = true;
            options.useBottomRightCornerResizer = true;

            // Create the dialog and enter modal state with a callback so we restore state
            // regardless of how the dialog is dismissed. The window will auto-delete.
            auto* window = options.create();
            safeThis->offlineRenderDialog = window;

            // Ensure this dialog doesn't float above other apps.
            window->setAlwaysOnTop(false);

            window->enterModalState(
                true,
                juce::ModalCallbackFunction::create([safeThis, wasVisualiserPaused, wasOfflineRenderActive, resultHolder](int) {
                    if (safeThis == nullptr)
                        return;

                    safeThis->audioProcessor.setOfflineRenderActive(wasOfflineRenderActive);
                    safeThis->visualiser.setPaused(wasVisualiserPaused);
                    safeThis->offlineRenderDialog = nullptr;

                    if (resultHolder != nullptr && resultHolder->has_value())
                    {
                        const auto& r = resultHolder->value();
                        if (!r.success && !r.cancelled)
                        {
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::AlertWindow::WarningIcon,
                                "Render Failed",
                                r.errorMessage.isNotEmpty() ? r.errorMessage : "An error occurred while rendering.");
                        }
                    }
                }),
                true);

            contentPtr->start();
        });
    });
#endif
}

void CommonPluginEditor::resetToDefault() {
    juce::StandaloneFilterWindow* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
    if (window != nullptr) {
        window->resetToDefaultState();
        window->setName(ProjectInfo::projectName);
    }
}

void CommonPluginEditor::toggleFullScreen() {
#if JUCE_WINDOWS
    juce::StandaloneFilterWindow* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
    if (window != nullptr) {
        fullScreen = !fullScreen;
        
        if (fullScreen) {
            // Store the current window bounds before going fullscreen
            windowedBounds = window->getBounds();
            
            // Get the display that contains the window
            auto& displays = juce::Desktop::getInstance().getDisplays();
            auto* display = displays.getDisplayForRect(window->getBounds());
            
            if (display != nullptr) {
                // Set window to cover the entire screen
                window->setFullScreen(true);
                window->setBounds(display->totalArea);
            }
        } else {
            // Restore the previous windowed bounds
            window->setFullScreen(false);
            if (!windowedBounds.isEmpty()) {
                window->setBounds(windowedBounds);
            }
        }
    }
#else
    juce::StandaloneFilterWindow* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
    if (window != nullptr) {
        fullScreen = !fullScreen;
        window->setFullScreen(fullScreen);
    }
#endif
}
