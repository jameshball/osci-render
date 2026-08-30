#include "VisualiserComponent.h"

#include "../CommonPluginEditor.h"
#include "../CommonPluginProcessor.h"
#include "../LookAndFeel.h"
#include "../components/OverlayDialogHelpers.h"
#include "VisualiserPopout.h"
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
    bool visualiserOnly) : VisualiserRenderer(settings.parameters, processor.threadManager, {1024, 1024}, 60.0, ""),
                           audioProcessor(processor),
                           editor(pluginEditor),
                           settings(settings),
                           recordingSettings(recordingSettings),
                           visualiserOnly(visualiserOnly),
                           ffmpegFile(ffmpegFile),
                           recordingController(ffmpegFile) {
    setAssets(createVisualiserTextureAssets());
    setNativeTransparencySupported(false);

    active = !audioProcessor.visualiserParameters.visualiserPaused->getBoolValue();
    audioProcessor.visualiserParameters.visualiserPaused->addListener(this);
    audioProcessor.visualiserParameters.textureOutputEnabled->addListener(this);
#if OSCI_PREMIUM
    audioProcessor.visualiserParameters.transparentBackground->addListener(this);
#endif
    startTimerHz(30);
    setShouldBeRunning(active);

#if OSCI_PREMIUM
    restorePopoutPending = true;
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

    addAndMakeVisible(fullScreenButton);
    fullScreenButton.setTooltip("Toggles fullscreen mode.");
#if OSCI_PREMIUM
    addAndMakeVisible(popOutButton);
    popOutButton.setClickingTogglesState(false);
    popOutButton.setTooltip("Open Visualiser Popout.");
#endif
    addAndMakeVisible(settingsButton);
    settingsButton.setTooltip("Opens the visualiser settings window.");

    addAndMakeVisible(textureOutputButton);
    textureOutputButton.setClickingTogglesState(false);
    textureOutputButton.setToggleState(false, juce::NotificationType::dontSendNotification);
    textureOutputButton.onClick = [this] {
#if OSCI_PREMIUM
        const bool currentlyRequestedOrRunning = this->settings.parameters.textureOutputEnabled->getBoolValue() || textureOutputController.isRunning();
        setTextureOutputEnabled(!currentlyRequestedOrRunning);
#else
        editor.showPremiumSplashScreen();
#endif
    };
    refreshTextureOutputButton();

    fullScreenButton.onClick = [this]() { enableFullScreen(); };

    settingsButton.onClick = [this]() {
        if (openSettings != nullptr) {
            openSettings();
        }
    };

#if OSCI_PREMIUM
    popOutButton.onClick = [this]() {
        if (popoutVisible) {
            closePopout();
        } else {
            popoutWindow();
        }
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

    // Initialize the timeline for standalone premium builds. Its controller is
    // selected by the editor according to the loaded file type.
    addChildComponent(timeline);
    timeline.addMouseListener(static_cast<juce::Component *>(this), true);

    preRenderCallback = [this] {
        if (!record.getToggleState()) {
            updateRenderModeFromProcessor();
            setRenderSize(this->recordingSettings.getCanvasSize());
            setFrameRate(this->recordingSettings.getFrameRate());
        }
    };

    postRenderCallback = [this] {
        serviceTextureOutputFrame();

        if (recordingController.isRecording()) {
            if (recordingController.capturesVideo()) {
                const Texture renderTexture = getRenderTexture();
                auto frame = recordingController.acquireVideoFrame({ renderTexture.width, renderTexture.height });
                if (frame.isValid()) {
                    getFrame(frame.getBytes());
                    frame.submit();
                }
            }
            if (recordingController.capturesAudio()) {
                recordingController.writeAudioBlock(audioOutputBuffer);
            }

            if (recordingController.hasFailed() && !recordingFailurePending.exchange(true)) {
                juce::Component::SafePointer<VisualiserComponent> safeThis(this);
                juce::MessageManager::callAsync([safeThis] {
                    if (safeThis == nullptr) {
                        return;
                    }
                    const auto message = safeThis->recordingController.getFailureMessage();
                    safeThis->setRecording(false);
                    osci::showOverlayMessage(*safeThis.getComponent(), "Recording Error", message);
                });
            }
        }

        stopwatch.addTime(juce::RelativeTime::seconds(1.0 / this->recordingSettings.getFrameRate()));
    };
}

VisualiserComponent::~VisualiserComponent() {
    stopTimer();
    if (popout != nullptr) {
        popout->saveWindowState();
    }
    // Stop the background thread while VisualiserComponent's vtable is still live.
    // If deferred to ~VisualiserRenderer, the vptr has already changed and the
    // running thread's virtual run()/runTask() dispatch becomes a data race.
    setShouldBeRunning(false, [this] { renderingSemaphore.release(); });
    // Detach while the derived renderer is still alive so OpenGL-owned services
    // are stopped by openGLContextClosing() on the context thread.
    openGLContext.detach();
    recordingController.discard();
    audioProcessor.removeAudioPlayerListener(this);
    audioProcessor.visualiserParameters.visualiserPaused->removeListener(this);
    audioProcessor.visualiserParameters.textureOutputEnabled->removeListener(this);
#if OSCI_PREMIUM
    audioProcessor.visualiserParameters.transparentBackground->removeListener(this);
#endif
    audioProcessor.haltRecording = nullptr;
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

void VisualiserComponent::setPopoutShownCallback(std::function<void()> callback) {
    popoutShownCallback = std::move(callback);
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
    recordingSampleRate = sampleRate;

    return desiredBufferSize;
}

void VisualiserComponent::stopTask() {
    requestRecordingStop();
    VisualiserRenderer::stopTask();
}

void VisualiserComponent::requestRecordingStop() {
    if (!recordingController.isRecording() || recordingStopPending.exchange(true)) {
        return;
    }

    juce::Component::SafePointer<VisualiserComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis == nullptr) {
            return;
        }
        safeThis->recordingStopPending.store(false);
        safeThis->setRecording(false);
    });
}

void VisualiserComponent::setPaused(bool paused, bool affectAudio) {
    active = !paused;
    setShouldBeRunning(active);
    renderingSemaphore.release();
    if (affectAudio) {
        audioProcessor.wavParser.setPaused(paused);
    }

    bool currentParamValue = audioProcessor.visualiserParameters.visualiserPaused->getBoolValue();
    if (currentParamValue != paused) {
        audioProcessor.visualiserParameters.visualiserPaused->setBoolValueNotifyingHost(paused);
    }
#if OSCI_PREMIUM
    if (popout != nullptr) {
        popout->setPresentationPaused(paused);
    }
#endif

    repaint();
}

bool VisualiserComponent::isPaused() const {
    return !active;
}

bool VisualiserComponent::isTransparentBackgroundEnabled() const {
    return settings.parameters.isTransparentBackgroundEnabled();
}

void VisualiserComponent::updatePausedState() {
    bool shouldBePaused = audioProcessor.visualiserParameters.visualiserPaused->getBoolValue();
    if (active == shouldBePaused) { // active and paused are opposites
        setPaused(shouldBePaused, true);
    }
}

void VisualiserComponent::parameterValueChanged(int parameterIndex, float newValue) {
    juce::ignoreUnused(newValue);
    unsigned int updates = 0;
    if (parameterIndex == audioProcessor.visualiserParameters.visualiserPaused->getParameterIndex()) {
        updates |= pausedStateUpdate;
    }
    if (parameterIndex == audioProcessor.visualiserParameters.textureOutputEnabled->getParameterIndex()) {
        updates |= textureOutputUpdate;
    }
#if OSCI_PREMIUM
    if (parameterIndex == audioProcessor.visualiserParameters.transparentBackground->getParameterIndex()) {
        updates |= popoutTransparencyUpdate;
    }
#endif
    pendingParameterUpdates.fetch_or(updates, std::memory_order_release);
}

void VisualiserComponent::timerCallback() {
    audioProcessor.serviceDeferredAudioSourceChanges();
#if OSCI_PREMIUM
    if (restorePopoutPending && isShowing() && getPeer() != nullptr) {
        restorePopoutPending = false;
        audioProcessor.globalSettings.save();
        audioProcessor.globalSettings.reload();
        const bool defaultOpen = visualiserOnly && juce::JUCEApplicationBase::isStandaloneApp()
                                 && TransparentWindow::isTransparencySupported();
        if (VisualiserWindow::getOpenPreference(audioProcessor.globalSettings, defaultOpen)) {
            popoutWindow(false);
        }
    }
#endif

    const auto updates = pendingParameterUpdates.exchange(0, std::memory_order_acquire);
    if (updates == 0) {
        return;
    }
    if ((updates & pausedStateUpdate) != 0) {
        updatePausedState();
    }
    if ((updates & textureOutputUpdate) != 0) {
        refreshTextureOutputButton();
        requestTextureOutputService();
    }
#if OSCI_PREMIUM
    if ((updates & popoutTransparencyUpdate) != 0 && popout != nullptr) {
        popout->setTransparencyEnabled(isTransparentBackgroundEnabled());
    }
#endif
}

void VisualiserComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {
    // Not needed for this parameter
}

void VisualiserComponent::mouseDrag(const juce::MouseEvent& event) {
    timerId = -1;
    if (event.getDistanceFromDragStart() > 4) {
        pauseOnMouseUp = false;
    }
}

void VisualiserComponent::mouseMove(const juce::MouseEvent &event) {
    if (event.getScreenX() == lastMouseX && event.getScreenY() == lastMouseY) {
        return;
    }
    hideButtonRow = false;
    setMouseCursor(juce::MouseCursor::PointingHandCursor);

    if (fullScreen) {
        if (!getScreenBounds().removeFromBottom(25).contains(event.getScreenX(), event.getScreenY()) && !event.mods.isLeftButtonDown()) {
            lastMouseX = event.getScreenX();
            lastMouseY = event.getScreenY();

            int newTimerId = juce::Random::getSystemRandom().nextInt();
            timerId = newTimerId;
            juce::WeakReference<VisualiserComponent> weakRef = this;
            juce::Timer::callAfterDelay(1000, [this, weakRef, newTimerId]() {
                if (weakRef) {
                    if (timerId == newTimerId && fullScreen) {
                        hideButtonRow = true;
                        setMouseCursor(juce::MouseCursor::NoCursor);
                        resized();
                    }
                } });
        }
        resized();
    }
}

void VisualiserComponent::mouseDown(const juce::MouseEvent& event) {
    pauseOnMouseUp = false;
    if (event.originalComponent == this) {
        if (event.mods.isLeftButtonDown() && !record.getToggleState()) {
            pauseOnMouseUp = true;
        }
    }
}

void VisualiserComponent::mouseUp(const juce::MouseEvent& event) {
    const bool shouldTogglePause = pauseOnMouseUp && event.getDistanceFromDragStart() <= 4;
    pauseOnMouseUp = false;
    if (!shouldTogglePause || record.getToggleState()) {
        return;
    }

    setPaused(active);
}

bool VisualiserComponent::keyPressed(const juce::KeyPress &key) {
    // If we're not accepting special keys, end early
    if (!audioProcessor.getAcceptsKeys()) return false;

    if (key.isKeyCode(juce::KeyPress::escapeKey)) {
        if (fullScreenCallback) {
            fullScreenCallback(FullScreenMode::MAIN_COMPONENT);
        }
        return true;
    } else if (key.isKeyCode(juce::KeyPress::F11Key) && juce::JUCEApplicationBase::isStandaloneApp()) {
        enableFullScreen();
        return true;
    } else if (key.isKeyCode(juce::KeyPress::spaceKey)) {
        setPaused(active);
        return true;
    }

    return false;
}

void VisualiserComponent::setRecording(bool recording) {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    recordingStopPending.store(false);
    stopwatch.stop();
    stopwatch.reset();
    const bool stillRecording = recordingController.isRecording();

    // Release renderingSemaphore to prevent deadlock
    renderingSemaphore.release();

    if (recording) {
#if OSCI_PREMIUM
        if (recordingController.wantsVideo(recordingSettings)) {
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
            setRenderSize(recordingSettings.getCanvasSize());
        }
#endif

        recordingFailurePending.store(false);
        const auto result = recordingController.start(recordingSettings, recordingSampleRate);
        if (!result) {
            record.setToggleState(false, juce::NotificationType::dontSendNotification);
            osci::showOverlayMessage(*this, "Recording Error", result.message);
            return;
        }

        setPaused(false);
        stopwatch.start();
    } else if (stillRecording) {
        recordingFailurePending.store(false);
        juce::Component::SafePointer<VisualiserComponent> safeThis(this);
        const auto result = recordingController.stopAndChooseExport(
            audioProcessor.getLastOpenedDirectory(), editor.appName,
            [safeThis](RecordingExportResult exportResult, juce::File destination) {
                if (safeThis == nullptr) {
                    return;
                }
                if (!exportResult) {
                    if (exportResult.message.isNotEmpty()) {
                        juce::Logger::writeToLog("Recording export failed: " + exportResult.message.substring(0, 500));
                    }
                    osci::showOverlayMessage(*safeThis.getComponent(),
                                             "Save Recording Failed",
                                             "Could not write:\n" + destination.getFullPathName());
                    return;
                }
                safeThis->audioProcessor.setLastOpenedDirectory(destination.getParentDirectory());
                safeThis->audioProcessor.recordingExportCompleted(destination);
            });
        if (!result) {
            record.setToggleState(false, juce::NotificationType::dontSendNotification);
            settings.setTransparencyControlEnabled(true);
            setBlockOnAudioThread(false);
            resized();
            return;
        }
    }

    const bool nowRecording = recordingController.isRecording();
    settings.setTransparencyControlEnabled(!nowRecording || !recordingController.capturesVideo());
    setBlockOnAudioThread(nowRecording);
    record.setToggleState(nowRecording, juce::NotificationType::dontSendNotification);
    resized();
}

void VisualiserComponent::resized() {
    auto area = getLocalBounds();
    if (fullScreen && hideButtonRow) {
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
    fullScreenButton.setVisible(true);
    fullScreenButton.setBounds(buttons.removeFromRight(30));
#if OSCI_PREMIUM
    popOutButton.setVisible(true);
    popOutButton.setBounds(buttons.removeFromRight(30));
#endif
    if (openSettings != nullptr) {
        settingsButton.setVisible(true);
        settingsButton.setBounds(buttons.removeFromRight(30));
    } else {
        settingsButton.setVisible(false);
    }

    if (visualiserOnly && juce::JUCEApplication::isStandaloneApp()) {
        audioInputButton.setVisible(true);
        audioInputButton.setBounds(buttons.removeFromRight(30));
    } else {
        audioInputButton.setVisible(false);
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
    if (!popoutVisible && downloading) {
        auto bounds = buttons.removeFromRight(160);
        editor.ffmpegDownloader.setBounds(bounds.withSizeKeepingCentre(bounds.getWidth() - 10, bounds.getHeight() - 10));
    }
#endif

    buttons.removeFromRight(10); // padding

    if (!popoutVisible && timeline.getController() != nullptr) {
        // Timeline replaces the old audioPlayer UI
        timeline.setVisible(true);
        timeline.setBounds(buttons);
    }

    overlayFadeCover.setBounds(getLocalBounds());
    overlayFadeCover.toFront(false);

    setViewportArea(area);
}

void VisualiserComponent::popoutWindow(bool saveOpenPreference) {
#if OSCI_PREMIUM
    restorePopoutPending = false;
    audioProcessor.globalSettings.save();
    audioProcessor.globalSettings.reload();
    if (saveOpenPreference) {
        VisualiserWindow::setOpenPreference(audioProcessor.globalSettings, true);
    }
    setRecording(false);

    // Ensure any blocked render completes before changing presentation state.
    renderingSemaphore.release();

#if JUCE_LINUX
    if (popout != nullptr) {
        popout->showPresentation();
        popoutVisible = true;
        popoutUpdated();
        if (popoutShownCallback != nullptr) {
            popoutShownCallback();
        }
        resized();
        return;
    }
#endif

    const auto windowTitle = editor.appName + " - Software Oscilloscope";
    const bool useSosciStandaloneDefaults = visualiserOnly && juce::JUCEApplicationBase::isStandaloneApp();
    popout = std::make_unique<VisualiserWindow>(windowTitle, *this, audioProcessor.globalSettings, useSosciStandaloneDefaults);
    popoutVisible = true;
    popoutUpdated();
    popout->showPresentation();
    if (popoutShownCallback != nullptr) {
        popoutShownCallback();
    }
    resized();
#endif
}

void VisualiserComponent::closePopout() {
#if OSCI_PREMIUM
    restorePopoutPending = false;
    VisualiserWindow::setOpenPreference(audioProcessor.globalSettings, false);
    if (popout == nullptr) {
        return;
    }
    popout->saveWindowState();
#if JUCE_LINUX
    popout->suspendPresentation();
    popoutVisible = false;
    popoutUpdated();
    resized();
#else
    const juce::Component::SafePointer<VisualiserComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis == nullptr || safeThis->popout == nullptr) {
            return;
        }
        safeThis->popout.reset();
        safeThis->popoutVisible = false;
        safeThis->popoutUpdated();
        safeThis->resized();
    });
#endif
#endif
}

void VisualiserComponent::popoutUpdated() {
#if OSCI_PREMIUM
    popOutButton.setVisible(true);
    popOutButton.setToggleState(popoutVisible, juce::NotificationType::dontSendNotification);
    popOutButton.setTooltip(popoutVisible ? "Close Visualiser Popout." : "Open Visualiser Popout.");
#endif
#if OSCI_PREMIUM
    editor.ffmpegDownloader.setVisible(!popoutVisible);
#endif
    record.setVisible(!popoutVisible);
    audioProcessor.haltRecording = [this] { setRecording(false); };
}

void VisualiserComponent::setPopoutAlwaysOnTop(bool alwaysOnTop) {
    if (popout != nullptr) {
        popout->setPinned(alwaysOnTop);
    } else {
        VisualiserWindow::setAlwaysOnTopPreference(audioProcessor.globalSettings, alwaysOnTop);
    }
}

bool VisualiserComponent::isPopoutAlwaysOnTop() const {
    return VisualiserWindow::getAlwaysOnTopPreference(audioProcessor.globalSettings);
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
    if (popout != nullptr) {
        popout->setPresentationFadeAlpha(fadeAlpha);
    }
    overlayFadeCover.setAlpha(fadeAlpha);
    overlayFadeCover.setVisible(fadeAlpha > 0.001f);
}

void VisualiserComponent::refreshTextureOutputButton() {
    const bool wanted = settings.parameters.textureOutputEnabled->getBoolValue();
    const bool running = textureOutputController.isRunning();

#if !OSCI_PREMIUM
    textureOutputButton.setEnabled(true);
    textureOutputButton.setToggleState(false, juce::NotificationType::dontSendNotification);
    textureOutputButton.setTooltip("Texture sharing via Syphon/Spout is a Premium feature. Click to learn more.");
    return;
#endif

    textureOutputButton.setEnabled(true);
    textureOutputButton.setToggleState(wanted || running, juce::NotificationType::dontSendNotification);

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
        textureOutputController.setRequested(false);
        settings.parameters.textureOutputEnabled->setBoolValueNotifyingHost(false);
        refreshTextureOutputButton();
        requestTextureOutputService();
        return;
    }

    const Texture renderTexture = getRenderTexture();
    if (renderTexture.id == 0 || renderTexture.width <= 0 || renderTexture.height <= 0) {
        osci::showOverlayMessage(*this,
                                 "Texture Output",
                                 "Texture output cannot start until the visualiser has rendered a frame.");
        refreshTextureOutputButton();
        requestTextureOutputService();
        return;
    }

    const osci::texture::BackendStatus status = osci::texture::getOpenGLBackendStatus();
    if (!status.isAvailable()) {
        const juce::String message = status.message.isNotEmpty()
            ? status.message
            : "Texture output is not available in this build.";
        osci::showOverlayMessage(*this, "Texture Output", message, osci::ErrorOverlay::Icon::None);
        refreshTextureOutputButton();
        requestTextureOutputService();
        return;
    }

    textureOutputController.setSourceName(recordingSettings.getCustomTextureOutputName());
    textureOutputController.setRequested(true);
    settings.parameters.textureOutputEnabled->setBoolValueNotifyingHost(true);
    refreshTextureOutputButton();
    requestTextureOutputService();
}

void VisualiserComponent::requestTextureOutputService() {
    openGLContext.triggerRepaint();
}

void VisualiserComponent::serviceTextureOutputFrame() {
#if !OSCI_PREMIUM
    textureOutputController.setRequested(false);
    textureOutputController.stop();

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
    const bool shouldRun = settings.parameters.textureOutputEnabled->getBoolValue();
    textureOutputController.setRequested(shouldRun);
    if (shouldRun && !textureOutputController.isRunning()) {
        textureOutputController.setSourceName(recordingSettings.getCustomTextureOutputName());
    }

    const Texture renderTexture = getRenderTexture();
    handleTextureOutputServiceResult(textureOutputController.serviceTexture2D(static_cast<std::uint32_t>(renderTexture.id),
                                                                               renderTexture.width,
                                                                               renderTexture.height));
#endif
}

void VisualiserComponent::handleTextureOutputServiceResult(osci::texture::ServiceResult result) {
    if (!result.changed()) {
        return;
    }

    if (result.failed()) {
        settings.parameters.textureOutputEnabled->setBoolValue(false);
    }

    juce::Component::SafePointer<VisualiserComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis, result] {
        if (safeThis == nullptr) {
            return;
        }

        if (result.failed()) {
            safeThis->settings.parameters.textureOutputEnabled->setBoolValueNotifyingHost(false);
        }

        safeThis->refreshTextureOutputButton();

        if (result.failed()) {
            const bool publishFailure = result.error == osci::texture::ErrorCode::publishFailed
                || result.error == osci::texture::ErrorCode::invalidTexture;
            osci::showOverlayMessage(*safeThis.getComponent(),
                                     "Texture Output",
                                     result.message,
                                     publishFailure ? osci::ErrorOverlay::Icon::Warning : osci::ErrorOverlay::Icon::None);
        }
    });
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
    textureOutputController.stop();

    VisualiserRenderer::openGLContextClosing();
}

void VisualiserComponent::newOpenGLContextCreated() {
    VisualiserRenderer::newOpenGLContextCreated();
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
