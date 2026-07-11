#include "CommonPluginProcessor.h"
#include "CommonPluginEditor.h"
#include "components/LicenseAndUpdatesComponent.h"
#include "components/OfflineRenderOverlay.h"
#include "components/OverlayDialogHelpers.h"
#include "components/RecordingSettingsOverlay.h"
#include "JucewrightAutomation.h"
#include <osci_standalone/osci_standalone.h>

#if OSCI_PREMIUM
#include "visualiser/OfflineAudioToVideoRenderer.h"
#endif

std::function<void()> showPremiumSplashScreenGlobal;

namespace {
juce::String feedbackOsName() {
#if JUCE_MAC
    return "macos";
#elif JUCE_WINDOWS
    return "windows";
#elif JUCE_LINUX
    return "linux";
#else
    return "other";
#endif
}

juce::String feedbackArchitecture() {
#if JUCE_ARM && JUCE_64BIT
    return "arm64";
#elif JUCE_ARM
    return "arm";
#elif JUCE_64BIT
    return "x86_64";
#elif JUCE_INTEL
    return "x86";
#else
    return "other";
#endif
}

juce::String feedbackPluginFormat(juce::AudioProcessor::WrapperType wrapperType) {
    switch (wrapperType) {
        case juce::AudioProcessor::wrapperType_Standalone: return "Standalone";
        case juce::AudioProcessor::wrapperType_VST: return "VST";
        case juce::AudioProcessor::wrapperType_VST3: return "VST3";
        case juce::AudioProcessor::wrapperType_AudioUnit: return "AU";
        case juce::AudioProcessor::wrapperType_AudioUnitv3: return "AUv3";
        case juce::AudioProcessor::wrapperType_AAX: return "AAX";
        case juce::AudioProcessor::wrapperType_LV2: return "LV2";
        default: return "Other";
    }
}

juce::String feedbackBuild(juce::StringRef version) {
    juce::StringArray parts;
    parts.addTokens(juce::String(version), ".", {});
    return parts.isEmpty() ? juce::String() : parts[parts.size() - 1];
}

juce::var feedbackClientContext(CommonAudioProcessor& processor) {
    auto* object = new juce::DynamicObject();
    object->setProperty("juce_version", juce::SystemStats::getJUCEVersion());
    object->setProperty("renderer", "opengl");
    object->setProperty("sample_rate", processor.getSampleRate());
    object->setProperty("block_size", processor.getBlockSize());
    object->setProperty("input_channels", processor.getTotalNumInputChannels());
    object->setProperty("output_channels", processor.getTotalNumOutputChannels());
    return juce::var(object);
}
} // namespace

CommonPluginEditor::CommonPluginEditor(CommonAudioProcessor& p, juce::String appName, juce::String projectFileType, int defaultWidth, int defaultHeight)
    : AudioProcessorEditor(&p), audioProcessor(p), appName(appName), projectFileType(projectFileType)
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
    showPremiumSplashScreenGlobal = [safeThis = juce::Component::SafePointer<CommonPluginEditor>(this)]() {
        if (safeThis) safeThis->showPremiumSplashScreen();
    };
#endif

    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        if (juce::TopLevelWindow::getNumTopLevelWindows() > 0) {
            juce::TopLevelWindow* w = juce::TopLevelWindow::getTopLevelWindow(0);
            juce::DocumentWindow* dw = dynamic_cast<juce::DocumentWindow*>(w);
            if (dw != nullptr) {
                dw->setBackgroundColour(osci::Colours::veryDark());
                dw->setColour(juce::ResizableWindow::backgroundColourId, osci::Colours::veryDark());
                dw->setTitleBarButtonsRequired(juce::DocumentWindow::allButtons, false);
                dw->setUsingNativeTitleBar(true);
            }
        }

        juce::StandalonePluginHolder* standalone = juce::StandalonePluginHolder::getInstance();
        if (standalone != nullptr) {
            standalone->getMuteInputValue().setValue(false);
            juce::Component::SafePointer<CommonPluginEditor> safeThis(this);
            standalone->commandLineCallback = [safeThis](const juce::String& commandLine) {
                if (safeThis != nullptr) {
                    safeThis->handleCommandLine(commandLine);
                }
            };
            standalone->showAudioSettingsOverlay = [safeThis] {
                if (safeThis == nullptr) {
                    return false;
                }

                safeThis->openAudioSettings();
                return true;
            };
        }
    }

    addAndMakeVisible(visualiser);

    int width = std::any_cast<int>(audioProcessor.getProperty("appWidth", defaultWidth));
    int height = std::any_cast<int>(audioProcessor.getProperty("appHeight", defaultHeight));

    visualiserSettings.setLookAndFeel(&getLookAndFeel());
    visualiserSettings.setSize(550, VISUALISER_SETTINGS_HEIGHT);
    visualiserSettings.setColour(juce::ResizableWindow::backgroundColourId, osci::Colours::dark());

    recordingSettings.setLookAndFeel(&getLookAndFeel());
    recordingSettings.setSize(430, 430);

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

    // Enable keyboard focus so F11 key works immediately
    setWantsKeyboardFocus(true);

    updatePrompt.showPendingInstallStatusIfNeeded();
    updatePrompt.scheduleInitialCheck();
}

void CommonPluginEditor::parentHierarchyChanged()
{
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
        juce::Component::SafePointer<CommonPluginEditor> safeThis(this);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr && safeThis->getPeer() != nullptr)
                safeThis->grabKeyboardFocus();
        });
    }
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
                    osci::showOverlayMessage(*this, "Invalid Command Line", "Invalid file type: " + file.getFullPathName());
                }
            } else {
                osci::showOverlayMessage(*this, "Invalid Command Line", "File not found: " + filePath);
            }
        }
    }
}

void CommonPluginEditor::resized() {
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

void CommonPluginEditor::refreshBetaUpdatesButton() {
    betaUpdatesButton.setVisible(osci::UpdateSettings(audioProcessor.getProductSlug()).betaUpdatesEnabled());
}

void CommonPluginEditor::layoutBetaUpdatesButton(juce::Rectangle<int>& topBar) {
    refreshBetaUpdatesButton();
    if (!betaUpdatesButton.isVisible())
        return;

    const auto width = juce::jmin(118, topBar.getWidth());
    betaUpdatesButton.setBounds(topBar.removeFromRight(width).reduced(2, 2));
    betaUpdatesButton.toFront(false);
}

void CommonPluginEditor::showOverlay(std::unique_ptr<osci::OverlayComponent> overlay) {
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

void CommonPluginEditor::dismissOverlay(osci::OverlayComponent* overlay,
                                        std::function<void()> beforeVisualiserRestore) {
    const juce::Component::SafePointer<CommonPluginEditor> safeThis(this);
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

void CommonPluginEditor::initialiseMenuBar(juce::MenuBarModel& menuBarModel) {
    menuBar.setModel(&menuBarModel);
}

CommonPluginEditor::~CommonPluginEditor() {
#if !OSCI_PREMIUM
    showPremiumSplashScreenGlobal = nullptr;
#endif
    juce::StandalonePluginHolder* standalone = juce::StandalonePluginHolder::getInstance();
    if (standalone != nullptr) {
        standalone->showAudioSettingsOverlay = nullptr;
    }

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

bool CommonPluginEditor::handleShortcut(const juce::KeyPress& key) {
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
    }
    return false;
}

bool CommonPluginEditor::keyPressed(const juce::KeyPress& key) {
    // Standard OS shortcuts always work (not gated by getAcceptsKeys)
    if (handleShortcut(key))
        return true;

    // Other special keys gated by user preference
    if (!audioProcessor.getAcceptsKeys()) return false;

    if (key.isKeyCode(juce::KeyPress::F11Key) && juce::JUCEApplicationBase::isStandaloneApp()) {
#if OSCI_PREMIUM
        toggleFullScreen();
        return true;
#endif
    } else if (key.isKeyCode(juce::KeyPress::escapeKey) && juce::JUCEApplicationBase::isStandaloneApp()) {
        if (fullScreen) {
            toggleFullScreen();
            return true;
        }
    }

    return false;
}

// KeyListener callback — fires on the top-level component when no child has focus
bool CommonPluginEditor::keyPressed(const juce::KeyPress& key, juce::Component*) {
    return handleShortcut(key);
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
    osci::showStandaloneAudioSettingsOverlay(
        *this,
        juce::String::createStringFromData(BinaryData::close_svg, BinaryData::close_svgSize));
}

void CommonPluginEditor::openLicenseAndUpdates() {
    if (findActiveOverlay<LicenseAndUpdatesComponent>() != nullptr)
        return;

    showOverlay(std::make_unique<LicenseAndUpdatesComponent>(audioProcessor));
}

void CommonPluginEditor::openFeedback() {
    if (findActiveOverlay<osci::FeedbackOverlay>() != nullptr) {
        return;
    }
    if (!activeOverlays.empty()) {
        auto* overlay = activeOverlays.back().get();
        auto dismissAndContinue = std::move(overlay->onDismissRequested);
        const juce::Component::SafePointer<CommonPluginEditor> safeThis(this);
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

    osci::FeedbackOverlayConfig feedback;
    feedback.closeButtonSvg = juce::String::createStringFromData(BinaryData::close_svg, BinaryData::close_svgSize);
    feedback.settingsButtonSvg = juce::String::createStringFromData(BinaryData::cog_svg, BinaryData::cog_svgSize);
    feedback.context.productSlug = audioProcessor.getProductSlug();
    feedback.context.productVersion = ProjectInfo::versionString;
    feedback.context.productBuild = feedbackBuild(ProjectInfo::versionString);
#if OSCI_PREMIUM
    feedback.context.productVariant = "premium";
#else
    feedback.context.productVariant = "free";
#endif
    const auto payload = audioProcessor.licenseManager.getPayload();
    if (payload.has_value()) {
        feedback.context.contactEmail = payload->email;
    }

    double displayScale = 1.0;
    int displayWidth = 0;
    int displayHeight = 0;
    auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());
    if (display != nullptr) {
        displayScale = display->scale;
        displayWidth = juce::roundToInt(display->totalArea.getWidth() * display->scale);
        displayHeight = juce::roundToInt(display->totalArea.getHeight() * display->scale);
    }

    const auto wrapperType = audioProcessor.wrapperType;
    feedback.submissionProvider = [processor = &audioProcessor, wrapperType, displayScale, displayWidth, displayHeight](
                                      osci::FeedbackRequest& request,
                                      osci::FeedbackAttachmentData& projectSnapshot,
                                      bool includeProjectSnapshot) {
        request.releaseTrack = osci::BackendClient::toString(osci::UpdateSettings(request.productSlug).releaseTrack());
        request.platform = osci::HardwareInfo::getCurrentPlatform();
        request.osName = feedbackOsName();
        request.osVersion = juce::SystemStats::getOperatingSystemName();
        request.architecture = feedbackArchitecture();
        request.locale = juce::SystemStats::getUserLanguage() + "-" + juce::SystemStats::getUserRegion();
        const juce::PluginHostType host;
        request.hostApplication = wrapperType == juce::AudioProcessor::wrapperType_Standalone
            ? "Standalone"
            : juce::String(host.getHostDescription());
        request.pluginFormat = feedbackPluginFormat(wrapperType);
        request.clientContextSchemaVersion = 1;
        request.clientContext = feedbackClientContext(*processor);
        request.licenseToken = processor->licenseManager.getCachedToken();
        request.displayScale = displayScale;
        request.displayWidth = displayWidth;
        request.displayHeight = displayHeight;
        request.log = processor->getFeedbackLogSnapshot(request.logTruncated);
        if (includeProjectSnapshot && projectSnapshot.data.isEmpty()) {
            processor->getFeedbackProjectSnapshot(projectSnapshot.data);
        }
    };

    auto screenshot = createComponentSnapshot(getLocalBounds(), true, 1.0f);
    feedback.automaticScreenshotPreview = screenshot;
    feedback.automaticScreenshot.kind = osci::FeedbackAttachmentKind::screenshot;
    feedback.automaticScreenshot.filename = feedback.context.productSlug + "-ui.png";
    feedback.automaticScreenshot.contentType = "image/png";

    feedback.projectSnapshot.kind = osci::FeedbackAttachmentKind::project;
    feedback.projectSnapshot.filename = feedback.context.productSlug + "-feedback." + projectFileType;
    feedback.projectSnapshot.contentType = "application/octet-stream";

#if DEBUG
    const auto automationBaseUrl = juce::SystemStats::getEnvironmentVariable("OSCI_FEEDBACK_API_BASE_URL", {});
    if (osci::isJucewrightAutomationLaunch() && automationBaseUrl.isNotEmpty()) {
        feedback.backend.apiBaseUrl = automationBaseUrl;
    }
#endif
    auto overlay = std::make_unique<osci::FeedbackOverlay>(std::move(feedback));
    overlay->captureBackdropFrom(screenshot);
    showOverlay(std::move(overlay));
}

void CommonPluginEditor::openRecordingSettings() {
    if (findActiveOverlay<RecordingSettingsOverlay>() != nullptr) {
        return;
    }

    const juce::Point<int> preferredContentSize { 430, 430 };
    recordingSettings.setSize(preferredContentSize.x, preferredContentSize.y);
    showOverlay(std::make_unique<RecordingSettingsOverlay>(recordingSettings, preferredContentSize));
}

void CommonPluginEditor::showPremiumSplashScreen() {
    openLicenseAndUpdates();
}

void CommonPluginEditor::renderAudioFileToVideo() {
#if !OSCI_PREMIUM
    showPremiumSplashScreen();
    return;
#else
    if (auto* existing = findActiveOverlay<OfflineRenderOverlay>()) {
        existing->toFront(true);
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

            content->setOnFinished([safeThis, resultHolder, overlayHolder](OfflineAudioToVideoRendererComponent::Result r) {
                if (safeThis == nullptr)
                    return;

                *resultHolder = r;

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
