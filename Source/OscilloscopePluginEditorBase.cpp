#include "CommonPluginProcessor.h"
#include "OscilloscopePluginEditorBase.h"
#include "components/OfflineRenderOverlay.h"
#include "components/OverlayDialogHelpers.h"
#include "components/RecordingSettingsOverlay.h"
#include "feedback/FeedbackReportBuilder.h"
#include "logging/WorkflowLogger.h"
#include <osci_standalone/osci_standalone.h>

#if OSCI_PREMIUM
#include "visualiser/OfflineAudioToVideoRenderer.h"
#endif

namespace {
#if OSCI_PREMIUM
const auto& offlineRenderLog = osci::WorkflowLoggers::offlineAudioToVideo;
#endif
}

OscilloscopePluginEditorBase::OscilloscopePluginEditorBase(CommonAudioProcessor& p, juce::String appName, juce::String projectFileType, int defaultWidth, int defaultHeight)
    : PluginEditorBase(p), audioProcessor(p), defaultEditorWidth(defaultWidth), defaultEditorHeight(defaultHeight), appName(appName), projectFileType(projectFileType)
{
#if JUCE_LINUX
    // use OpenGL on Linux for much better performance. The default on Mac is CoreGraphics, and on Window is Direct2D which is much faster.
    openGlContext.attachTo(*getTopLevelComponent());
#endif

    setLookAndFeel(&lookAndFeel);

    addAndMakeVisible(menuBar);
    addAndMakeVisible(undoRedoControls);
    addAndMakeVisible(betaUpdatesButton);
    addAndMakeVisible(updatePrompt);
    updatePrompt.setVisible(false);
    betaUpdatesButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(0xf2, 0xc9, 0x4c));
    betaUpdatesButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(0xff, 0xd9, 0x68));
    betaUpdatesButton.setColour(juce::TextButton::textColourOffId, osci::Colours::veryDark());
    betaUpdatesButton.setColour(juce::TextButton::textColourOnId, osci::Colours::veryDark());
    betaUpdatesButton.setTooltip("Beta updates are enabled. Click to manage.");
    betaUpdatesButton.onClick = [this] { openLicenseAndUpdates(); };
    updatePrompt.onLicenseRequired = [this] { openLicenseAndUpdates(); };
    refreshBetaUpdatesButton();
#if !OSCI_PREMIUM
    showPremiumSplashScreenGlobal = [safeThis = juce::Component::SafePointer<OscilloscopePluginEditorBase>(this)]() {
        if (safeThis) safeThis->showPremiumSplashScreen();
    };
#endif

    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        juce::StandalonePluginHolder* standalone = juce::StandalonePluginHolder::getInstance();
        if (standalone != nullptr) {
            juce::Component::SafePointer<OscilloscopePluginEditorBase> safeThis(this);
            standalone->commandLineCallback = [safeThis](const juce::String& commandLine) {
                if (safeThis != nullptr) {
                    safeThis->handleCommandLine(commandLine);
                }
            };
        }
    }

    addAndMakeVisible(visualiser);

    int width = std::any_cast<int>(audioProcessor.getProperty("appWidth", defaultWidth));
    int height = std::any_cast<int>(audioProcessor.getProperty("appHeight", defaultHeight));

    visualiserSettings.setLookAndFeel(&getLookAndFeel());
    visualiserSettings.setSizeToFitWidth(550);
    visualiserSettings.setColour(juce::ResizableWindow::backgroundColourId, osci::Colours::dark());

    recordingSettings.setLookAndFeel(&getLookAndFeel());
    recordingSettings.setSize(430, 430);

    menuBar.toFront(true);

    setSize(width, height);
    setResizable(true, true);
    setResizeLimits(250, 250, 999999, 999999);

    updateTitle();

    // On startup (especially standalone state restore), the editor may not yet be attached to a
    // top-level window when updateTitle() is first called. Refresh once the message loop runs.
    juce::Component::SafePointer<OscilloscopePluginEditorBase> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis != nullptr)
            safeThis->updateTitle();
    });

    // Enable keyboard focus so F11 key works immediately
    setWantsKeyboardFocus(true);

    updatePrompt.showPendingInstallStatusIfNeeded();
    updatePrompt.scheduleInitialCheck();
}

void OscilloscopePluginEditorBase::parentHierarchyChanged()
{
    PluginEditorBase::parentHierarchyChanged();
    // Refresh the title when the editor is attached/detached.
    updateTitle();

    if (getPeer() != nullptr) {
        // Register as KeyListener on the top-level component so that
        // shortcuts are received even when no child component has focus
        // (the top-level component is the fallback key target in JUCE).
        auto* tl = getTopLevelComponent();
        if (tl != nullptr && tl != topLevelKeyTarget) {
            if (topLevelKeyTarget != nullptr)
                topLevelKeyTarget->removeKeyListener(this);
            tl->addKeyListener(this);
            topLevelKeyTarget = tl;
        }

        // Grab keyboard focus so that shortcuts are received immediately.
        juce::Component::SafePointer<OscilloscopePluginEditorBase> safeThis(this);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr && safeThis->getPeer() != nullptr)
                safeThis->grabKeyboardFocus();
        });
    }
}

void OscilloscopePluginEditorBase::handleCommandLine(const juce::String& commandLine) {
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
                    osci::showOverlayMessage(*this, "Invalid Command Line", "Invalid file type: " + file.getFullPathName());
                }
            } else {
                osci::showOverlayMessage(*this, "Invalid Command Line", "File not found: " + filePath);
            }
        }
    }
}

void OscilloscopePluginEditorBase::resized() {
    audioProcessor.setProperty("appWidth", getWidth());
    audioProcessor.setProperty("appHeight", getHeight());
    refreshBetaUpdatesButton();

    const int promptWidth = juce::jmin(updatePrompt.getPreferredWidth(), getWidth() - 28);
    if (promptWidth > 340) {
        updatePrompt.setBounds(getWidth() - promptWidth - 14, 42, promptWidth, updatePrompt.getPreferredHeight());
    } else {
        updatePrompt.setBounds({});
    }

    if (!activeOverlays.empty()) {
        for (auto& overlay : activeOverlays) {
            overlay->setBounds(getLocalBounds());
            overlay->toFront(false);
        }
    } else {
        updatePrompt.toFront(false);
    }
}

void OscilloscopePluginEditorBase::refreshBetaUpdatesButton() {
    betaUpdatesButton.setVisible(osci::UpdateSettings(audioProcessor.getProductSlug()).betaUpdatesEnabled());
}

void OscilloscopePluginEditorBase::layoutBetaUpdatesButton(juce::Rectangle<int>& topBar) {
    refreshBetaUpdatesButton();
    if (!betaUpdatesButton.isVisible())
        return;

    const auto width = juce::jmin(118, topBar.getWidth());
    betaUpdatesButton.setBounds(topBar.removeFromRight(width).reduced(2, 2));
    betaUpdatesButton.toFront(false);
}

void OscilloscopePluginEditorBase::showOverlay(std::unique_ptr<osci::OverlayComponent> overlay) {
    bool anyHeavy = false;
    for (auto& o : activeOverlays) {
        if (!o->lightweight) {
            anyHeavy = true;
            break;
        }
    }

    if (!anyHeavy && !overlay->lightweight) {
        visualiser.cancelOverlayFadeIn();
        visualiserWasVisibleBeforeOverlay = visualiser.isVisible();
        visualiser.setVisible(false);
    }

    if (!overlay->lightweight && !overlay->hasCapturedBackdrop()) {
        overlay->captureBackdropFrom(*this);
    }

    auto* ptr = overlay.get();
    auto previousDismissCallback = std::move(overlay->onDismissRequested);
    overlay->onDismissRequested = [this, ptr, previousDismissCallback = std::move(previousDismissCallback)]() mutable {
        dismissOverlay(ptr, std::move(previousDismissCallback));
    };
    overlay->setBounds(getLocalBounds());
    addAndMakeVisible(*overlay);
    overlay->toFront(true);
    activeOverlays.push_back(std::move(overlay));
    resized();
}

void OscilloscopePluginEditorBase::dismissOverlay(osci::OverlayComponent* overlay,
                                        std::function<void()> beforeVisualiserRestore) {
    const juce::Component::SafePointer<OscilloscopePluginEditorBase> safeThis(this);
    std::unique_ptr<osci::OverlayComponent> removedOverlay;
    for (auto it = activeOverlays.begin(); it != activeOverlays.end(); ++it) {
        if (it->get() == overlay) {
            removeChildComponent(overlay);
            removedOverlay = std::move(*it);
            activeOverlays.erase(it);
            break;
        }
    }

    bool anyHeavy = false;
    for (auto& o : activeOverlays) {
        if (!o->lightweight) {
            anyHeavy = true;
            break;
        }
    }

    if (beforeVisualiserRestore != nullptr) {
        beforeVisualiserRestore();
    }

    removedOverlay.reset();

    if (safeThis == nullptr) {
        return;
    }

    if (!anyHeavy && visualiserWasVisibleBeforeOverlay) {
        visualiser.prepareOverlayFadeIn();
        visualiser.setVisible(true);
    }

    resized();

    if (!anyHeavy && visualiserWasVisibleBeforeOverlay) {
        visualiser.fadeInAfterOverlay();
    }
}

void OscilloscopePluginEditorBase::initialiseMenuBar(juce::MenuBarModel& menuBarModel) {
    menuBar.setModel(&menuBarModel);
}

OscilloscopePluginEditorBase::~OscilloscopePluginEditorBase() {
#if !OSCI_PREMIUM
    showPremiumSplashScreenGlobal = nullptr;
#endif
    if (topLevelKeyTarget != nullptr)
        topLevelKeyTarget->removeKeyListener(this);

    if (audioProcessor.haltRecording != nullptr) {
        audioProcessor.haltRecording();
    }

    setLookAndFeel(nullptr);
    juce::Desktop::getInstance().setDefaultLookAndFeel(nullptr);
}

// Shared handler for standard OS shortcuts (undo, redo, save, open).
// These always work regardless of getAcceptsKeys() — they are fundamental
// editor operations, not "special keys" like j/k for file switching.

bool OscilloscopePluginEditorBase::handleShortcut(const juce::KeyPress& key) {
    if (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown() && key.getKeyCode() == 'Z') {
        undoRedoControls.redo();
        return true;
    } else if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z') {
        undoRedoControls.undo();
        return true;
    } else if (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown() && key.getKeyCode() == 'S') {
        saveProjectAs();
        return true;
    } else if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S') {
        saveProject();
        return true;
    } else if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'O') {
        openProject();
        return true;
    } else if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'N' && juce::JUCEApplicationBase::isStandaloneApp()) {
        resetToDefault();
        return true;
    } else if (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown() && key.getKeyCode() == 'M') {
        audioProcessor.muteParameter->setBoolValueNotifyingHost(!audioProcessor.muteParameter->getBoolValue());
        return true;
    } else if (key.isKeyCode(juce::KeyPress::F11Key) && juce::JUCEApplicationBase::isStandaloneApp()) {
        toggleFullScreen();
        return true;
    } else if (key.isKeyCode(juce::KeyPress::escapeKey) && isFullScreen()) {
        toggleFullScreen();
        return true;
    }
    return false;
}

bool OscilloscopePluginEditorBase::keyPressed(const juce::KeyPress& key) {
    // Standard OS shortcuts always work (not gated by getAcceptsKeys)
    if (handleShortcut(key))
        return true;

    // Other special keys gated by user preference
    if (!audioProcessor.getAcceptsKeys()) return false;

    return false;
}

// KeyListener callback — fires on the top-level component when no child has focus
bool OscilloscopePluginEditorBase::keyPressed(const juce::KeyPress& key, juce::Component*) {
    return handleShortcut(key);
}

void OscilloscopePluginEditorBase::openProject(const juce::File& file) {
    if (file != juce::File()) {
        auto data = juce::MemoryBlock();
        if (!file.loadFileAsData(data)) {
            osci::showOverlayMessage(*this, "Open Project Failed", "Could not read:\n" + file.getFullPathName());
            return;
        }
        audioProcessor.setStateInformation(data.getData(), data.getSize());
        audioProcessor.currentProjectFile = file.getFullPathName();
        audioProcessor.setLastOpenedDirectory(file.getParentDirectory());
        audioProcessor.addRecentProjectFile(file);
        updateTitle();
    }
}

void OscilloscopePluginEditorBase::openProject() {
    chooser = std::make_unique<juce::FileChooser>("Load " + appName + " Project", audioProcessor.getLastOpenedDirectory(), "*." + projectFileType);
    auto flags = juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles;

    juce::Component::SafePointer<OscilloscopePluginEditorBase> safeThis(this);
    chooser->launchAsync(flags, [safeThis](const juce::FileChooser& chooser) {
        if (safeThis != nullptr)
            safeThis->openProject(chooser.getResult());
    });
}

void OscilloscopePluginEditorBase::saveProject() {
    if (audioProcessor.currentProjectFile.isEmpty()) {
        saveProjectAs();
    } else {
        auto data = juce::MemoryBlock();
        audioProcessor.getStateInformation(data);
        auto file = juce::File(audioProcessor.currentProjectFile);
        if (!file.replaceWithData(data.getData(), data.getSize())) {
            osci::showOverlayMessage(*this, "Save Project Failed", "Could not write:\n" + file.getFullPathName());
            return;
        }
        updateTitle();
    }
}

void OscilloscopePluginEditorBase::saveProjectAs() {
    chooser = std::make_unique<juce::FileChooser>("Save " + appName + " Project", audioProcessor.getLastOpenedDirectory(), "*." + projectFileType);
    auto flags = juce::FileBrowserComponent::saveMode;

    juce::Component::SafePointer<OscilloscopePluginEditorBase> safeThis(this);
    chooser->launchAsync(flags, [safeThis](const juce::FileChooser& chooser) {
        if (safeThis == nullptr)
            return;

        auto file = chooser.getResult();
        if (file != juce::File()) {
            if (!file.hasFileExtension(safeThis->projectFileType)) {
                file = file.withFileExtension(safeThis->projectFileType);
            }
            safeThis->audioProcessor.setLastOpenedDirectory(file.getParentDirectory());
            safeThis->audioProcessor.currentProjectFile = file.getFullPathName();
            safeThis->audioProcessor.addRecentProjectFile(file);
            safeThis->saveProject();
        }
    });
}

void OscilloscopePluginEditorBase::resetWindowSizeAndPosition() {
    auto* standaloneWindow = findParentComponentOfClass<juce::StandaloneFilterWindow>();
    if (standaloneWindow != nullptr) {
        standaloneWindow->setFullScreen(false);
    }
    fullScreen = false;
    setSize(defaultEditorWidth, defaultEditorHeight);
    auto* window = getTopLevelComponent();
    if (window != nullptr && window != this) {
        window->centreWithSize(window->getWidth(), window->getHeight());
    }
}

void OscilloscopePluginEditorBase::updateTitle() {
    juce::String title = appName;
    if (!audioProcessor.currentProjectFile.isEmpty()) {
        title += " - " + audioProcessor.currentProjectFile;
    }
    if (currentFileName.isNotEmpty()) {
        title += " - " + currentFileName;
    }
    getTopLevelComponent()->setName(title);
}

void OscilloscopePluginEditorBase::fileUpdated(juce::String fileName) {
    currentFileName = fileName;
    updateTitle();
}

void OscilloscopePluginEditorBase::openAudioSettings() {
    openStandaloneAudioSettings();
}

void OscilloscopePluginEditorBase::openLicenseAndUpdates() {
    if (findActiveOverlay<osci::LicenseAndUpdatesComponent>() != nullptr)
        return;

    showOverlay(std::make_unique<osci::LicenseAndUpdatesComponent>(
        audioProcessor.licenseManager,
        osci::makeProductUpdateConfig ([this] {
            refreshBetaUpdatesButton();
            resized();
        })));
}

void OscilloscopePluginEditorBase::openFeedback() {
    if (findActiveOverlay<osci::FeedbackOverlay>() != nullptr) {
        return;
    }
    if (!activeOverlays.empty()) {
        auto* overlay = activeOverlays.back().get();
        auto dismissAndContinue = std::move(overlay->onDismissRequested);
        const juce::Component::SafePointer<OscilloscopePluginEditorBase> safeThis(this);
        overlay->onDismissRequested = [safeThis, dismissAndContinue = std::move(dismissAndContinue)]() mutable {
            juce::MessageManager::callAsync([safeThis] {
                if (safeThis != nullptr) {
                    safeThis->openFeedback();
                }
            });
            if (dismissAndContinue != nullptr) {
                dismissAndContinue();
            }
        };
        overlay->requestDismiss();
        return;
    }

    auto feedback = FeedbackReportBuilder::create(audioProcessor, *this, projectFileType);
    const auto screenshot = feedback.automaticScreenshotPreview;
    auto overlay = std::make_unique<osci::FeedbackOverlay>(std::move(feedback));
    overlay->captureBackdropFrom(screenshot);
    showOverlay(std::move(overlay));
}

void OscilloscopePluginEditorBase::openRecordingSettings() {
    if (findActiveOverlay<RecordingSettingsOverlay>() != nullptr) {
        return;
    }

    const juce::Point<int> preferredContentSize { 430, 430 };
    recordingSettings.setSize(preferredContentSize.x, preferredContentSize.y);
    showOverlay(std::make_unique<RecordingSettingsOverlay>(recordingSettings, preferredContentSize));
}

void OscilloscopePluginEditorBase::showPremiumSplashScreen() {
    openLicenseAndUpdates();
}

void OscilloscopePluginEditorBase::renderAudioFileToVideo() {
#if !OSCI_PREMIUM
    showPremiumSplashScreen();
    return;
#else
    if (auto* existing = findActiveOverlay<OfflineRenderOverlay>()) {
        offlineRenderLog.event("existing render brought to front");
        existing->toFront(true);
        return;
    }

    offlineRenderLog.started();

    // Step 1: choose input audio file
    chooser = std::make_unique<juce::FileChooser>(
        "Choose an input audio file",
        audioProcessor.getLastOpenedDirectory(),
        "*.wav;*.aiff;*.flac;*.ogg;*.mp3");

    auto openFlags = juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles;

    juce::Component::SafePointer<OscilloscopePluginEditorBase> safeThis(this);
    offlineRenderLog.event("input selection opened");
    chooser->launchAsync(openFlags, [safeThis](const juce::FileChooser& inputChooser) {
        if (safeThis == nullptr) {
            offlineRenderLog.cancelled("input selection because editor closed");
            return;
        }

        const auto inputFile = inputChooser.getResult();
        if (inputFile == juce::File()) {
            offlineRenderLog.cancelled("input selection");
            return;
        }

        offlineRenderLog.event("input selected", "file=" + inputFile.getFileName());

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

        offlineRenderLog.event("output selection opened");
        safeThis->chooser->launchAsync(saveFlags, [safeThis, inputFile, ext](const juce::FileChooser& outputChooser) {
            if (safeThis == nullptr) {
                offlineRenderLog.cancelled("output selection because editor closed");
                return;
            }

            auto outputFile = outputChooser.getResult();
            if (outputFile == juce::File()) {
                offlineRenderLog.cancelled("output selection");
                return;
            }

            // Ensure the file extension matches the codec container by default.
            if (outputFile.getFileExtension().isEmpty())
                outputFile = outputFile.withFileExtension(ext);

            offlineRenderLog.event("output selected", "file=" + outputFile.getFileName());
            safeThis->audioProcessor.setLastOpenedDirectory(outputFile.getParentDirectory());

            // Ensure FFmpeg exists. If it doesn't, this will prompt the user to download it.
            if (!safeThis->audioProcessor.ensureFFmpegExists()) {
                offlineRenderLog.event("render deferred", "FFmpeg unavailable");
                return;
            }

            offlineRenderLog.event("FFmpeg ready");

            // Stop any live recording and pause the main visualiser.
            if (safeThis->audioProcessor.haltRecording != nullptr)
                safeThis->audioProcessor.haltRecording();

            const bool wasVisualiserPaused = safeThis->visualiser.isPaused();
            const bool wasOfflineRenderActive = safeThis->audioProcessor.isOfflineRenderActive();

            // Make the plugin output silent and skip heavy processing during offline render.
            safeThis->audioProcessor.setOfflineRenderActive(true);

            auto resultHolder = std::make_shared<std::optional<OfflineAudioToVideoRendererComponent::Result>>();
            auto overlayHolder = std::make_shared<juce::Component::SafePointer<OfflineRenderOverlay>>();

            auto content = std::make_unique<OfflineAudioToVideoRendererComponent>(
                safeThis->audioProcessor,
                safeThis->audioProcessor.visualiserParameters,
                safeThis->audioProcessor.threadManager,
                safeThis->recordingSettings,
                inputFile,
                outputFile,
                safeThis->visualiser.getRenderMode());

            content->setSize(700, 520);

            content->setOnFinished([safeThis, resultHolder, overlayHolder, outputFile](OfflineAudioToVideoRendererComponent::Result r) {
                if (safeThis == nullptr) {
                    offlineRenderLog.cancelled("result delivery because editor closed");
                    return;
                }

                *resultHolder = r;

                if (r.success) {
                    offlineRenderLog.completed();
                    safeThis->audioProcessor.recordingExportCompleted(outputFile);
                } else if (r.cancelled) {
                    offlineRenderLog.cancelled("render");
                } else {
                    offlineRenderLog.failed("render", r.errorMessage);
                }

                if (auto* overlay = overlayHolder->getComponent()) {
                    overlay->requestDismiss();
                }
            });

            auto* contentPtr = content.get();
            const juce::Point<int> preferredContentSize { content->getWidth(), content->getHeight() };
            auto overlay = std::make_unique<OfflineRenderOverlay>(std::move(content), preferredContentSize);
            *overlayHolder = overlay.get();

            overlay->onDismissRequested = [safeThis, wasVisualiserPaused, wasOfflineRenderActive, resultHolder] {
                if (safeThis == nullptr) {
                    return;
                }

                safeThis->audioProcessor.setOfflineRenderActive(wasOfflineRenderActive);
                safeThis->visualiser.setPaused(wasVisualiserPaused, false);

                if (resultHolder != nullptr && resultHolder->has_value()) {
                    const auto& r = resultHolder->value();
                    if (!r.success && !r.cancelled) {
                        osci::showOverlayMessage(*safeThis.getComponent(),
                                                 "Render Failed",
                                                 r.errorMessage.isNotEmpty() ? r.errorMessage : "An error occurred while rendering.");
                    }
                }
            };

            safeThis->showOverlay(std::move(overlay));
            offlineRenderLog.event("render UI opened");
            contentPtr->start();
        });
    });
#endif
}

void OscilloscopePluginEditorBase::resetToDefault() {
    juce::StandaloneFilterWindow* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
    if (window != nullptr) {
        window->resetToDefaultState();
        window->setName(ProjectInfo::projectName);
    }
}

void OscilloscopePluginEditorBase::toggleFullScreen() {
#if JUCE_WINDOWS
    juce::StandaloneFilterWindow* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
    if (window != nullptr) {
        fullScreen = !window->isFullScreen();

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
        fullScreen = !window->isFullScreen();
        window->setFullScreen(fullScreen);
    }
#endif
}

bool OscilloscopePluginEditorBase::isFullScreen() {
    auto* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
    return window != nullptr && window->isFullScreen();
}
