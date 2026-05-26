#include "VisualiserComponent.h"

#include "../CommonPluginEditor.h"
#include "../CommonPluginProcessor.h"
#include "../LookAndFeel.h"
#include "VisualiserTextureAssets.h"

#include <cstdint>

VisualiserComponent::FadeCoverComponent::FadeCoverComponent() {
    setOpaque(false);
    setInterceptsMouseClicks(false, false);
    setVisible(false);
}

void VisualiserComponent::FadeCoverComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
}

VisualiserComponent::VisualiserComponent(
    CommonAudioProcessor &processor,
    CommonPluginEditor &pluginEditor,
    juce::File ffmpegFile,
    VisualiserSettings &settings,
    RecordingSettings &recordingSettings,
    VisualiserComponent *parent,
    bool visualiserOnly) : VisualiserRenderer(settings.parameters, processor.threadManager),
                           settings(settings),
                           audioProcessor(processor),
                           ffmpegFile(ffmpegFile),
#if OSCI_PREMIUM
                           ffmpegEncoderManager(ffmpegFile),
#endif
                           recordingSettings(recordingSettings),
                           visualiserOnly(visualiserOnly),
                           parent(parent),
                           editor(pluginEditor) {
    setAssets(createVisualiserTextureAssets());

    // Sync active state with the parameter for the primary visualiser
    if (isPrimaryVisualiser()) {
        active = !audioProcessor.visualiserParameters.visualiserPaused->getBoolValue();
        audioProcessor.visualiserParameters.visualiserPaused->addListener(this);
        audioProcessor.visualiserParameters.textureOutputEnabled->addListener(this);
    }

    setShouldBeRunning(active);

#if OSCI_PREMIUM
    addAndMakeVisible(editor.ffmpegDownloader);
#endif

    audioProcessor.haltRecording = [this] {
        setRecording(false);
    };

    addAndMakeVisible(record);
#if OSCI_PREMIUM
    record.setTooltip("Toggles recording of the oscilloscope's visuals and audio.");
#else
    record.setTooltip("Toggles recording of the audio.");
#endif
    record.setPulseAnimation(true);
    record.onClick = [this] {
        setRecording(record.getToggleState());
    };

    addAndMakeVisible(stopwatch);

    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus(true);
    overlayFadeController.setValueChangedCallback([this](float progress) {
        setOverlayFadeProgress(progress);
    });
    overlayFadeController.snapTo(true);
    addChildComponent(overlayFadeCover);

    if (parent == nullptr || juce::JUCEApplicationBase::isStandaloneApp()) {
        addAndMakeVisible(fullScreenButton);
        fullScreenButton.setTooltip("Toggles fullscreen mode.");
    }
#if OSCI_PREMIUM
    if (child == nullptr && parent == nullptr) {
        addAndMakeVisible(popOutButton);
        popOutButton.setTooltip("Opens the oscilloscope in a new window.");
    }
#endif
    addAndMakeVisible(settingsButton);
    settingsButton.setTooltip("Opens the visualiser settings window.");

    addAndMakeVisible(textureOutputButton);
    textureOutputButton.setClickingTogglesState(false);
    textureOutputButton.setToggleState(false, juce::NotificationType::dontSendNotification);
    textureOutputButton.onClick = [this] {
#if OSCI_PREMIUM
        const bool currentlyRequestedOrRunning = this->settings.parameters.textureOutputEnabled->getBoolValue() || textureOutputSender.isRunning();
        setTextureOutputEnabled(!currentlyRequestedOrRunning);
#else
        editor.showPremiumSplashScreen();
#endif
    };
    refreshTextureOutputButton();

    fullScreenButton.onClick = [this]() {
        if (this->parent != nullptr) {
#if OSCI_PREMIUM
            if (auto* window = dynamic_cast<VisualiserWindow*>(getTopLevelComponent()))
                window->toggleFullScreen();
#endif
        } else {
            enableFullScreen();
        }
    };

    settingsButton.onClick = [this]() {
        if (openSettings != nullptr) {
            openSettings();
        }
    };

#if OSCI_PREMIUM
    popOutButton.onClick = [this]() {
        popoutWindow();
    };
#endif

    if (visualiserOnly && juce::JUCEApplication::isStandaloneApp()) {
        addAndMakeVisible(audioInputButton);
        audioInputButton.setTooltip("Appears red when audio input is being used. Click to enable audio input and close any open audio files.");
        audioInputButton.setClickingTogglesState(false);
        audioInputButton.setToggleState(!audioProcessor.wavParser.isInitialised(), juce::NotificationType::dontSendNotification);
        audioInputButton.onClick = [this] {
            audioProcessor.stopAudioFile();
        };
    }

    // Listen for audio file changes
    audioProcessor.addAudioPlayerListener(this);

    // Initialize timeline for standalone premium builds
    // Controller will be set by parent component
    addChildComponent(timeline);
    timeline.addMouseListener(static_cast<juce::Component *>(this), true);

    preRenderCallback = [this] {
        if (!record.getToggleState()) {
            updateRenderModeFromProcessor();
            setResolution(this->recordingSettings.getResolution());
            setFrameRate(this->recordingSettings.getFrameRate());
        }
    };

    postRenderCallback = [this] {
        serviceTextureOutputFrame();

        if (record.getToggleState()) {
#if OSCI_PREMIUM
            if (recordingVideo) {
                // draw frame to ffmpeg
                Texture renderTexture = getRenderTexture();
                getFrame(framePixels);
                if (ffmpegProcess.write(framePixels.data(), 4 * renderTexture.width * renderTexture.height, 3000) == 0) {
                    record.setToggleState(false, juce::NotificationType::dontSendNotification);

                    juce::MessageManager::callAsync([this] {
                        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                            "Recording Error",
                            "An error occurred while writing the video frame to the ffmpeg process. Recording has been stopped.",
                            "OK");
                    });
                }
            }
#endif
            if (recordingAudio) {
                audioRecorder.audioThreadCallback(audioOutputBuffer);
            }
        }

        stopwatch.addTime(juce::RelativeTime::seconds(1.0 / this->recordingSettings.getFrameRate()));
    };
}

VisualiserComponent::~VisualiserComponent() {
    // Stop the background thread while VisualiserComponent's vtable is still live.
    // If deferred to ~VisualiserRenderer, the vptr has already changed and the
    // running thread's virtual run()/runTask() dispatch becomes a data race.
    setShouldBeRunning(false, [this] { renderingSemaphore.release(); });
    {
        juce::SpinLock::ScopedLockType lock(textureOutputLock);
        textureOutputSourceName.clear();
    }
    textureOutputSender.stop();
    setRecording(false);
    audioProcessor.removeAudioPlayerListener(this);
    if (isPrimaryVisualiser()) {
        audioProcessor.visualiserParameters.visualiserPaused->removeListener(this);
        audioProcessor.visualiserParameters.textureOutputEnabled->removeListener(this);
    }
    if (parent == nullptr) {
        audioProcessor.haltRecording = nullptr;
    }
}

void VisualiserComponent::setFullScreen(bool fullScreen) {
    this->fullScreen = fullScreen;
    hideButtonRow = false;
    setMouseCursor(juce::MouseCursor::PointingHandCursor);

    // Release renderingSemaphore to prevent deadlocks during layout changes
    renderingSemaphore.release();

    resized();
}

void VisualiserComponent::setFullScreenCallback(std::function<void(FullScreenMode)> callback) {
    fullScreenCallback = callback;
}

void VisualiserComponent::enableFullScreen() {
    if (fullScreenCallback) {
        fullScreenCallback(FullScreenMode::TOGGLE);
    }
    grabKeyboardFocus();
}

void VisualiserComponent::mouseDoubleClick(const juce::MouseEvent &event) {
    if (event.originalComponent == this) {
        enableFullScreen();
    }
}

int VisualiserComponent::prepareTask(double sampleRate, int bufferSize) {
    int desiredBufferSize = VisualiserRenderer::prepareTask(sampleRate, bufferSize);
    audioRecorder.setSampleRate(sampleRate);

    return desiredBufferSize;
}

void VisualiserComponent::stopTask() {
    setRecording(false);
    VisualiserRenderer::stopTask();
}

void VisualiserComponent::setPaused(bool paused, bool affectAudio) {
    active = !paused;
    setShouldBeRunning(active);
    renderingSemaphore.release();
    if (affectAudio) {
        audioProcessor.wavParser.setPaused(paused);
    }

    if (isPrimaryVisualiser()) {
        bool currentParamValue = audioProcessor.visualiserParameters.visualiserPaused->getBoolValue();
        if (currentParamValue != paused) {
            audioProcessor.visualiserParameters.visualiserPaused->setBoolValueNotifyingHost(paused);
        }
    }

    repaint();
    if (child != nullptr) {
        child->repaint();
    }
}

bool VisualiserComponent::isPaused() const {
    return !active;
}

bool VisualiserComponent::isPrimaryVisualiser() const {
    return parent == nullptr;
}

void VisualiserComponent::updatePausedState() {
    if (isPrimaryVisualiser()) {
        bool shouldBePaused = audioProcessor.visualiserParameters.visualiserPaused->getBoolValue();
        if (active == shouldBePaused) { // active and paused are opposites
            setPaused(shouldBePaused, true);
        }
    }
}

void VisualiserComponent::parameterValueChanged(int parameterIndex, float newValue) {
    auto safeThis = juce::Component::SafePointer<VisualiserComponent>(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis == nullptr) return;
        safeThis->updatePausedState();
        safeThis->refreshTextureOutputButton();
        safeThis->requestTextureOutputService();
    });
}

void VisualiserComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {
    // Not needed for this parameter
}

void VisualiserComponent::mouseDrag(const juce::MouseEvent &event) {
    timerId = -1;
}

void VisualiserComponent::mouseMove(const juce::MouseEvent &event) {
    if (event.getScreenX() == lastMouseX && event.getScreenY() == lastMouseY) {
        return;
    }
    if (isMirrorMode())
        return;
    hideButtonRow = false;
    setMouseCursor(juce::MouseCursor::PointingHandCursor);

    // Treat both fullScreen mode and pop-out mode (parent != nullptr) as needing auto-hide controls
    if (fullScreen || parent != nullptr) {
        if (!getScreenBounds().removeFromBottom(25).contains(event.getScreenX(), event.getScreenY()) && !event.mods.isLeftButtonDown()) {
            lastMouseX = event.getScreenX();
            lastMouseY = event.getScreenY();

            int newTimerId = juce::Random::getSystemRandom().nextInt();
            timerId = newTimerId;
            auto pos = event.getScreenPosition();
            auto parent = this->parent;

            juce::WeakReference<VisualiserComponent> weakRef = this;
            juce::Timer::callAfterDelay(1000, [this, weakRef, newTimerId, pos, parent]() {
                if (weakRef) {
                    if (parent == nullptr || parent->child == this) {
                        // Check both fullscreen or pop-out mode
                        if (timerId == newTimerId && (fullScreen || this->parent != nullptr)) {
                            hideButtonRow = true;
                            setMouseCursor(juce::MouseCursor::NoCursor);
                            resized();
                        }
                    }
                } });
        }
        resized();
    }
}

void VisualiserComponent::mouseDown(const juce::MouseEvent &event) {
    if (event.originalComponent == this) {
        if (event.mods.isLeftButtonDown() && !record.getToggleState()) {
            if (isMirrorMode() && parent != nullptr) {
                parent->setPaused(parent->active);
            } else {
                setPaused(active);
            }
        }
    }
}

bool VisualiserComponent::keyPressed(const juce::KeyPress &key) {
    // If we're not accepting special keys, end early
    if (!audioProcessor.getAcceptsKeys()) return false;

    if (key.isKeyCode(juce::KeyPress::escapeKey)) {
        // In popout mode, exit popout fullscreen first
        if (parent != nullptr) {
#if OSCI_PREMIUM
            if (auto* window = dynamic_cast<VisualiserWindow*>(getTopLevelComponent())) {
                if (window->getIsFullScreen()) {
                    window->toggleFullScreen();
                    return true;
                }
            }
#endif
        } else if (fullScreenCallback) {
            fullScreenCallback(FullScreenMode::MAIN_COMPONENT);
        }
        return true;
    } else if (key.isKeyCode(juce::KeyPress::F11Key) && juce::JUCEApplicationBase::isStandaloneApp()) {
#if OSCI_PREMIUM
        if (parent != nullptr) {
            if (auto* window = dynamic_cast<VisualiserWindow*>(getTopLevelComponent()))
                window->toggleFullScreen();
        } else {
            enableFullScreen();
        }
#endif
        return true;
    } else if (key.isKeyCode(juce::KeyPress::spaceKey)) {
        if (isMirrorMode() && parent != nullptr) {
            parent->setPaused(parent->active);
        } else {
            setPaused(active);
        }
        return true;
    }

    return false;
}

void VisualiserComponent::setRecording(bool recording) {
    stopwatch.stop();
    stopwatch.reset();

#if OSCI_PREMIUM
    bool stillRecording = ffmpegProcess.isRunning() || audioRecorder.isRecording();
#else
    bool stillRecording = audioRecorder.isRecording();
#endif

    // Release renderingSemaphore to prevent deadlock
    renderingSemaphore.release();

    if (recording) {
#if OSCI_PREMIUM
        recordingVideo = recordingSettings.recordingVideo();
        recordingAudio = recordingSettings.recordingAudio();
        if (!recordingVideo && !recordingAudio) {
            record.setToggleState(false, juce::NotificationType::dontSendNotification);
            return;
        }

        if (recordingVideo) {
            auto onDownloadSuccess = [this] {
                juce::MessageManager::callAsync([this] {
                    record.setEnabled(true);
                    juce::Timer::callAfterDelay(3000, [this] {
                        juce::MessageManager::callAsync([this] {
                            editor.ffmpegDownloader.setVisible(false);
                            downloading = false;
                            resized();
                        });
                    }); });
            };
            auto onDownloadStart = [this] {
                juce::MessageManager::callAsync([this] {
                    record.setEnabled(false);
                    downloading = true;
                    resized(); });
            };
            if (!audioProcessor.ensureFFmpegExists(onDownloadStart, onDownloadSuccess)) {
                record.setToggleState(false, juce::NotificationType::dontSendNotification);
                return;
            }

            // Get the appropriate file extension based on codec
            juce::String fileExtension = recordingSettings.getFileExtensionForCodec();
            tempVideoFile = std::make_unique<juce::TemporaryFile>("." + fileExtension);

            VideoCodec codec = recordingSettings.getVideoCodec();
            juce::String cmd = ffmpegEncoderManager.buildVideoEncodingCommand(
                codec,
                recordingSettings.getCRF(),
                getRenderWidth(),
                getRenderHeight(),
                recordingSettings.getFrameRate(),
                recordingSettings.getCompressionPreset(),
                tempVideoFile->getFile());

            if (!ffmpegProcess.start(cmd)) {
                juce::Logger::writeToLog("Recording: ffmpegProcess.start() failed for command: " + cmd);
                record.setToggleState(false, juce::NotificationType::dontSendNotification);
                juce::MessageManager::callAsync([this] {
                    juce::MessageBoxOptions options = juce::MessageBoxOptions()
                        .withTitle("Recording Error")
                        .withMessage("Failed to start the FFmpeg video encoder.\n\n"
                                     "Please check that FFmpeg is compatible with your system.")
                        .withButton("OK")
                        .withIconType(juce::AlertWindow::WarningIcon)
                        .withAssociatedComponent(this);
                    juce::AlertWindow::showAsync(options, nullptr);
                });
                return;
            }
            framePixels.resize(getRenderWidth() * getRenderHeight() * 4);
        }

        if (recordingAudio) {
            tempAudioFile = std::make_unique<juce::TemporaryFile>(".wav");
            audioRecorder.startRecording(tempAudioFile->getFile());
        }
#else
        // audio only recording
        tempAudioFile = std::make_unique<juce::TemporaryFile>(".wav");
        audioRecorder.startRecording(tempAudioFile->getFile());
#endif

        setPaused(false);
        stopwatch.start();
    } else if (stillRecording) {
#if OSCI_PREMIUM
        bool wasRecordingAudio = recordingAudio;
        bool wasRecordingVideo = recordingVideo;
        recordingAudio = false;
        recordingVideo = false;

        juce::String extension = wasRecordingVideo ? recordingSettings.getFileExtensionForCodec() : "wav";
        if (wasRecordingAudio) {
            audioRecorder.stop();
        }
        if (wasRecordingVideo) {
            ffmpegProcess.close();
        }
#else
        audioRecorder.stop();
        juce::String extension = "wav";
#endif
        chooser = std::make_unique<juce::FileChooser>("Save recording", audioProcessor.getLastOpenedDirectory(), "*." + extension);
        auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting;

#if OSCI_PREMIUM
        chooser->launchAsync(flags, [this, wasRecordingAudio, wasRecordingVideo, extension](const juce::FileChooser &chooser) {
            auto file = chooser.getResult();
            if (file != juce::File()) {
                // Ensure the file has the correct extension
                if (!file.hasFileExtension(extension)) {
                    file = file.withFileExtension(extension);
                }

                if (wasRecordingAudio && wasRecordingVideo) {
                    // delete the file if it exists
                    if (file.existsAsFile()) {
                        file.deleteFile();
                    }
                    ffmpegProcess.start("\"" + ffmpegFile.getFullPathName() + "\" -i \"" + tempVideoFile->getFile().getFullPathName() + "\" -i \"" + tempAudioFile->getFile().getFullPathName() + "\" -c:v copy " + recordingSettings.getAudioCodecArgs().joinIntoString(" ") + " -y \"" + file.getFullPathName() + "\"");
                    ffmpegProcess.close();
                } else if (wasRecordingAudio) {
                    tempAudioFile->getFile().copyFileTo(file);
                } else if (wasRecordingVideo) {
                    tempVideoFile->getFile().copyFileTo(file);
                }
                audioProcessor.setLastOpenedDirectory(file.getParentDirectory());
            } });
#else
        chooser->launchAsync(flags, [this, extension](const juce::FileChooser &chooser) {
            auto file = chooser.getResult();
            if (file != juce::File()) {
                // Ensure the file has the correct extension
                if (!file.hasFileExtension(extension)) {
                    file = file.withFileExtension(extension);
                }

                tempAudioFile->getFile().copyFileTo(file);
                audioProcessor.setLastOpenedDirectory(file.getParentDirectory());
            } });
#endif
    }

    setBlockOnAudioThread(recording);
#if OSCI_PREMIUM
    numFrames = 0;
#endif
    record.setToggleState(recording, juce::NotificationType::dontSendNotification);
    resized();
}

void VisualiserComponent::resized() {
    auto area = getLocalBounds();
    // Apply hideButtonRow logic to both fullscreen and pop-out modes
    if ((fullScreen || parent != nullptr) && hideButtonRow) {
        buttonRow = area.removeFromBottom(0);
        fullScreenButton.setVisible(false);
        popOutButton.setVisible(false);
        settingsButton.setVisible(false);
        audioInputButton.setVisible(false);
        textureOutputButton.setVisible(false);
        record.setVisible(false);
        stopwatch.setVisible(false);
        timeline.setVisible(false);
        overlayFadeCover.setBounds(getLocalBounds());
        overlayFadeCover.toFront(false);
        setViewportArea(area);
        return;
    } else {
        buttonRow = area.removeFromBottom(25);
    }
    auto buttons = buttonRow;
    if (parent == nullptr || juce::JUCEApplicationBase::isStandaloneApp()) {
        fullScreenButton.setVisible(true);
        fullScreenButton.setBounds(buttons.removeFromRight(30));
    }
#if OSCI_PREMIUM
    if (child == nullptr && parent == nullptr) {
        popOutButton.setVisible(true);
        popOutButton.setBounds(buttons.removeFromRight(30));
    }
#endif
    if (openSettings != nullptr) {
        settingsButton.setVisible(true);
        settingsButton.setBounds(buttons.removeFromRight(30));
    } else {
        settingsButton.setVisible(false);
    }

    if (visualiserOnly && juce::JUCEApplication::isStandaloneApp() && child == nullptr) {
        audioInputButton.setVisible(true);
        audioInputButton.setBounds(buttons.removeFromRight(30));
    }

    textureOutputButton.setVisible(true);
    textureOutputButton.setBounds(buttons.removeFromRight(30));

    record.setVisible(true);
    record.setBounds(buttons.removeFromRight(25));
    if (record.getToggleState()) {
        stopwatch.setVisible(true);
        stopwatch.setBounds(buttons.removeFromRight(100));
    } else {
        stopwatch.setVisible(false);
    }

#if OSCI_PREMIUM
    if (child == nullptr && downloading) {
        auto bounds = buttons.removeFromRight(160);
        editor.ffmpegDownloader.setBounds(bounds.withSizeKeepingCentre(bounds.getWidth() - 10, bounds.getHeight() - 10));
    }
#endif

    buttons.removeFromRight(10); // padding

    if (child == nullptr && timeline.getController() != nullptr) {
        // Timeline replaces the old audioPlayer UI
        timeline.setVisible(true);
        timeline.setBounds(buttons);
    }

    overlayFadeCover.setBounds(getLocalBounds());
    overlayFadeCover.toFront(false);

    setViewportArea(area);
}

void VisualiserComponent::popoutWindow() {
#if OSCI_PREMIUM
    setRecording(false);

    // Release renderingSemaphore to prevent deadlock when creating a child visualizer
    renderingSemaphore.release();

    auto visualiser = new VisualiserComponent(
        audioProcessor,
        editor,
        ffmpegFile,
        settings,
        recordingSettings,
        this,
        visualiserOnly);
    visualiser->settings.setLookAndFeel(&getLookAndFeel());
    visualiser->openSettings = openSettings;
    visualiser->closeSettings = closeSettings;
    // Pop-out visualiser is created with parent set to this component
    child = visualiser;
    childUpdated();
    visualiser->setSize(350, 350);
    popout = std::make_unique<VisualiserWindow>("Software Oscilloscope", this);
    popout->setContentOwned(visualiser, true);
    popout->setUsingNativeTitleBar(true);
    popout->setResizable(true, false);
    // Register editor as KeyListener so undo/redo shortcuts work in the popout window
    popout->addKeyListener(&editor);
    popout->setVisible(true);
    popout->centreWithSize(350, 350);
    // Hide all buttons on the popout and set up mirror mode
    visualiser->hideButtonRow = true;
    visualiser->resized();
    // Set up mirror mode AFTER the window is visible so the GL context is active
    visualiser->setMirrorSource(this);
    setHasMirrorConsumer(true);
    resized();
#endif
}

void VisualiserComponent::childUpdated() {
#if OSCI_PREMIUM
    popOutButton.setVisible(child == nullptr);
#endif
#if OSCI_PREMIUM
    editor.ffmpegDownloader.setVisible(child == nullptr);
#endif
    record.setVisible(child == nullptr);
    if (child != nullptr) {
        audioProcessor.haltRecording = [this] {
            setRecording(false);
            child->setRecording(false);
        };
    } else {
        audioProcessor.haltRecording = [this] {
            setRecording(false);
        };
    }
}

void VisualiserComponent::prepareOverlayFadeIn() {
    overlayFadeCover.toFront(false);
    overlayFadeController.snapTo(false);
}

void VisualiserComponent::fadeInAfterOverlay() {
    overlayFadeController.animateTo(true,
                                    overlayFadeDurationMs,
                                    juce::Easings::createCubicBezier(0.42f, 0.0f, 0.58f, 1.0f));
}

void VisualiserComponent::cancelOverlayFadeIn() {
    overlayFadeController.snapTo(true);
}

void VisualiserComponent::setOverlayFadeProgress(float progress) {
    const auto fadeAlpha = 1.0f - juce::jlimit(0.0f, 1.0f, progress);
    setPresentationFadeAlpha(fadeAlpha);
    overlayFadeCover.setAlpha(fadeAlpha);
    overlayFadeCover.setVisible(fadeAlpha > 0.001f);
}

void VisualiserComponent::refreshTextureOutputButton() {
    const bool wanted = settings.parameters.textureOutputEnabled->getBoolValue();
    const bool running = textureOutputSender.isRunning();

#if !OSCI_PREMIUM
    textureOutputButton.setEnabled(true);
    textureOutputButton.setToggleState(false, juce::NotificationType::dontSendNotification);
    textureOutputButton.setTooltip("Texture sharing via Syphon/Spout is a Premium feature. Click to learn more.");
    return;
#endif

    textureOutputButton.setEnabled(isPrimaryVisualiser());
    textureOutputButton.setToggleState(wanted || running, juce::NotificationType::dontSendNotification);

    if (!isPrimaryVisualiser()) {
        textureOutputButton.setTooltip("Texture output can only be started from the main visualiser.");
        return;
    }

    if (wanted && !running) {
        textureOutputButton.setTooltip("Texture output will start on the next rendered frame.");
        return;
    }

    textureOutputButton.setTooltip(running ? "Stops texture output." : "Starts texture output.");
}

void VisualiserComponent::setTextureOutputEnabled(bool enabled) {
#if !OSCI_PREMIUM
    if (enabled) {
        editor.showPremiumSplashScreen();
    }
    settings.parameters.textureOutputEnabled->setBoolValueNotifyingHost(false);
    refreshTextureOutputButton();
    requestTextureOutputService();
    return;
#endif

    if (enabled == settings.parameters.textureOutputEnabled->getBoolValue()) {
        refreshTextureOutputButton();
        requestTextureOutputService();
        return;
    }

    if (!enabled) {
        settings.parameters.textureOutputEnabled->setBoolValueNotifyingHost(false);
        refreshTextureOutputButton();
        requestTextureOutputService();
        return;
    }

    const Texture renderTexture = getRenderTexture();
    if (renderTexture.id == 0 || renderTexture.width <= 0 || renderTexture.height <= 0) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                               "Texture Output",
                                               "Texture output cannot start until the visualiser has rendered a frame.",
                                               "OK");
        refreshTextureOutputButton();
        requestTextureOutputService();
        return;
    }

    const osci::texture::BackendStatus status = osci::texture::getOpenGLBackendStatus();
    if (!status.isAvailable() || !isPrimaryVisualiser()) {
        const juce::String message = status.message.isNotEmpty()
            ? status.message
            : "Texture output is not available in this build.";
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                                               "Texture Output",
                                               message,
                                               "OK");
        refreshTextureOutputButton();
        requestTextureOutputService();
        return;
    }

    const juce::String requestedSourceName = recordingSettings.getCustomTextureOutputName();
    {
        juce::SpinLock::ScopedLockType lock(textureOutputLock);
        textureOutputSourceName = requestedSourceName;
    }
    settings.parameters.textureOutputEnabled->setBoolValueNotifyingHost(true);
    refreshTextureOutputButton();
    requestTextureOutputService();
}

void VisualiserComponent::requestTextureOutputService() {
    openGLContext.triggerRepaint();
}

osci::texture::ErrorCode VisualiserComponent::startTextureOutputOnRenderThread() {
    const Texture renderTexture = getRenderTexture();

    osci::texture::OpenGLSenderDescription description;
    {
        juce::SpinLock::ScopedLockType lock(textureOutputLock);
        description.sourceName = textureOutputSourceName;
    }
    if (description.sourceName.isEmpty()) {
        description.sourceName = recordingSettings.getCustomTextureOutputName();
        juce::SpinLock::ScopedLockType lock(textureOutputLock);
        textureOutputSourceName = description.sourceName;
    }
    description.width = renderTexture.width;
    description.height = renderTexture.height;

    const osci::texture::ErrorCode error = textureOutputSender.start(std::move(description));
    if (error == osci::texture::ErrorCode::none) {
        textureOutputFrameIndex = 0;
        textureOutputSenderWidth = renderTexture.width;
        textureOutputSenderHeight = renderTexture.height;
    }

    return error;
}

void VisualiserComponent::publishTextureOutputFrame() {
    const Texture renderTexture = getRenderTexture();

    osci::texture::OpenGLTextureFrame frame;
    frame.textureId = static_cast<std::uint32_t>(renderTexture.id);
    frame.textureTarget = osci::texture::openGLTexture2D;
    frame.width = renderTexture.width;
    frame.height = renderTexture.height;
    frame.verticallyFlipped = false;
    frame.frameIndex = textureOutputFrameIndex++;

    const osci::texture::ErrorCode error = textureOutputSender.publish(frame);
    if (error == osci::texture::ErrorCode::none) {
        return;
    }

    textureOutputSender.stop();
    settings.parameters.textureOutputEnabled->setBoolValue(false);
    textureOutputSenderWidth = 0;
    textureOutputSenderHeight = 0;
    {
        juce::SpinLock::ScopedLockType lock(textureOutputLock);
        textureOutputSourceName.clear();
    }

    juce::Component::SafePointer<VisualiserComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis, error] {
        if (safeThis == nullptr) {
            return;
        }

        safeThis->settings.parameters.textureOutputEnabled->setBoolValueNotifyingHost(false);
        safeThis->refreshTextureOutputButton();
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                               "Texture Output",
                                               osci::texture::toString(error),
                                               "OK");
    });
}

void VisualiserComponent::serviceTextureOutputFrame() {
#if !OSCI_PREMIUM
    if (textureOutputSender.isRunning()) {
        textureOutputSender.stop();
    }

    if (settings.parameters.textureOutputEnabled->getBoolValue()) {
        settings.parameters.textureOutputEnabled->setBoolValue(false);
        juce::Component::SafePointer<VisualiserComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr) {
                safeThis->settings.parameters.textureOutputEnabled->setBoolValueNotifyingHost(false);
                safeThis->refreshTextureOutputButton();
            }
        });
    }
    return;
#else
    if (!isPrimaryVisualiser()) {
        return;
    }

    auto resetFailedStart = [this](osci::texture::ErrorCode error) {
        settings.parameters.textureOutputEnabled->setBoolValue(false);
        textureOutputSenderWidth = 0;
        textureOutputSenderHeight = 0;
        {
            juce::SpinLock::ScopedLockType lock(textureOutputLock);
            textureOutputSourceName.clear();
        }

        const osci::texture::BackendStatus status = osci::texture::getOpenGLBackendStatus();
        const juce::String message = status.isAvailable() || status.message.isEmpty()
            ? osci::texture::toString(error)
            : status.message;

        juce::Component::SafePointer<VisualiserComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis, message] {
            if (safeThis == nullptr) {
                return;
            }

            safeThis->settings.parameters.textureOutputEnabled->setBoolValueNotifyingHost(false);
            safeThis->refreshTextureOutputButton();
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                                                   "Texture Output",
                                                   message,
                                                   "OK");
        });
    };

    if (!settings.parameters.textureOutputEnabled->getBoolValue()) {
        if (textureOutputSender.isRunning()) {
            textureOutputSender.stop();
            textureOutputFrameIndex = 0;
            textureOutputSenderWidth = 0;
            textureOutputSenderHeight = 0;
            {
                juce::SpinLock::ScopedLockType lock(textureOutputLock);
                textureOutputSourceName.clear();
            }
            juce::Component::SafePointer<VisualiserComponent> safeThis(this);
            juce::MessageManager::callAsync([safeThis] {
                if (safeThis != nullptr) {
                    safeThis->refreshTextureOutputButton();
                }
            });
        }

        return;
    }

    if (!textureOutputSender.isRunning()) {
        const osci::texture::ErrorCode error = startTextureOutputOnRenderThread();
        if (error != osci::texture::ErrorCode::none) {
            resetFailedStart(error);
            return;
        }

        juce::Component::SafePointer<VisualiserComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr) {
                safeThis->refreshTextureOutputButton();
            }
        });
    }

    const Texture renderTexture = getRenderTexture();
    if (renderTexture.width != textureOutputSenderWidth || renderTexture.height != textureOutputSenderHeight) {
        textureOutputSender.stop();
        textureOutputSenderWidth = 0;
        textureOutputSenderHeight = 0;
        const osci::texture::ErrorCode error = startTextureOutputOnRenderThread();
        if (error != osci::texture::ErrorCode::none) {
            resetFailedStart(error);
            return;
        }
    }

    publishTextureOutputFrame();
#endif
}

void VisualiserComponent::updateRenderModeFromProcessor() {
    // Called on message thread
    if (!visualiserOnly) {
        // osci-render always provides 6 channels (x, y, z, r, g, b)
        setRenderMode(RenderMode::XYRGB);
        return;
    }
    // Determine based on whether brightness and RGB are enabled and not force-disabled
    bool brightnessAllowed = !audioProcessor.getForceDisableBrightnessInput();
    bool rgbAllowed = !audioProcessor.getForceDisableRgbInput();
    // Prefer RGB if we have 4th/5th channels effectively
    if (rgbAllowed && audioProcessor.isRgbEnabled()) {
        setRenderMode(RenderMode::XYRGB);
    } else if (brightnessAllowed && audioProcessor.isBrightnessEnabled()) {
        setRenderMode(RenderMode::XYZ);
    } else {
        setRenderMode(RenderMode::XY);
    }
}

void VisualiserComponent::openGLContextClosing() {
    textureOutputSender.stop();
    textureOutputSenderWidth = 0;
    textureOutputSenderHeight = 0;
    if (!settings.parameters.textureOutputEnabled->getBoolValue()) {
        juce::SpinLock::ScopedLockType lock(textureOutputLock);
        textureOutputSourceName.clear();
    }

    VisualiserRenderer::openGLContextClosing();
}

void VisualiserComponent::parserChanged() {
    // Update audio input button when audio file changes
    juce::MessageManager::callAsync([this] {
        if (visualiserOnly && juce::JUCEApplication::isStandaloneApp()) {
            audioInputButton.setToggleState(!audioProcessor.wavParser.isInitialised(), juce::NotificationType::dontSendNotification);
        }
        // Parent component should update timeline controller/visibility as needed
    });
}

void VisualiserComponent::setTimelineController(std::shared_ptr<TimelineController> controller) {
    bool shouldShow = controller != nullptr &&
                      juce::JUCEApplicationBase::isStandaloneApp();

#if !OSCI_PREMIUM
    shouldShow = false;
#endif

    if (shouldShow) {
        timeline.setController(controller);
        timeline.setVisible(true);
    } else {
        // Clear the controller so resized() doesn't re-show the timeline based on a stale controller.
        timeline.setController(nullptr);
        timeline.setVisible(false);
    }

    resized();
}

void VisualiserComponent::paint(juce::Graphics &g) {
    // Mirror mode: draw paused overlay over GL content
    if (isMirrorMode()) {
        if (parent != nullptr && parent->isPaused()) {
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.fillRect(getLocalBounds());
            g.setColour(juce::Colours::white);
            g.setFont(30.0f);
            g.drawText("Paused", getLocalBounds(), juce::Justification::centred);
        }
        return;
    }

    bool colourSpecified = isColourSpecified(buttonRowColourId);
    auto buttonRowColour = osci::Colours::veryDark();
    if (colourSpecified) {
        buttonRowColour = findColour(buttonRowColourId, true);
    }
    g.setColour(buttonRowColour);
    g.fillRect(buttonRow);
    if (!active) {
        // draw a translucent overlay
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRect(getViewportArea());

        g.setColour(juce::Colours::white);
        g.setFont(30.0f);
        g.drawText("Paused", getViewportArea(), juce::Justification::centred);
    }
}
