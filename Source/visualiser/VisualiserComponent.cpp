#include "VisualiserComponent.h"

#include "../CommonPluginEditor.h"
#include "../CommonPluginProcessor.h"
#include "../LookAndFeel.h"
#include "../components/OverlayDialogHelpers.h"
#include "../video/VideoEncodingConstants.h"
#include "VisualiserTextureAssets.h"

#include <cstdint>

#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
PopoutToolbar::PopoutToolbar() {
    addAndMakeVisible(closeButton);
    addAndMakeVisible(fullscreenButton);
    addAndMakeVisible(frameButton);
    addAndMakeVisible(alwaysOnTopButton);
    addAndMakeVisible(mouseInteractionButton);
    closeButton.setIconColours(juce::Colours::white, juce::Colours::white.withAlpha(0.8f));
    closeButton.onClick = [this] {
        if (onClose != nullptr) {
            onClose();
        }
    };
    fullscreenButton.onClick = [this] {
        if (onFullScreen != nullptr) {
            onFullScreen();
        }
    };
    frameButton.setClickingTogglesState(false);
    frameButton.onClick = [this] {
        if (onToggleFrame != nullptr) {
            onToggleFrame();
        }
    };
    alwaysOnTopButton.setClickingTogglesState(false);
    alwaysOnTopButton.onClick = [this] {
        if (onToggleAlwaysOnTop != nullptr) {
            onToggleAlwaysOnTop();
        }
    };
    mouseInteractionButton.setClickingTogglesState(false);
    mouseInteractionButton.onClick = [this] {
        if (onToggleMouseInteraction != nullptr) {
            onToggleMouseInteraction();
        }
    };
    setState(true, true, true, false, false, false, false);
    setInterceptsMouseClicks(true, true);
}

void PopoutToolbar::setState(bool visible, bool requestedVisible, bool alwaysOnTop, bool isFullScreen,
                             bool allMouseEventsPassThrough, bool paused, bool showClickThroughHint,
                             bool transparencyEnabled) {
    frameVisible = visible;
    fullScreen = isFullScreen;
    closeButton.setVisible(visible);
    fullscreenButton.setVisible(visible);
    frameButton.setVisible(visible);
    alwaysOnTopButton.setVisible(visible);
    mouseInteractionButton.setVisible(visible && transparencyEnabled);
    frameButton.setToggleState(!requestedVisible, juce::NotificationType::dontSendNotification);
    frameButton.setTooltip(requestedVisible ? "Hide Window Frame." : "Show Window Frame.");
    alwaysOnTopButton.setToggleState(alwaysOnTop, juce::NotificationType::dontSendNotification);
    alwaysOnTopButton.setTooltip(alwaysOnTop ? "Disable Always on Top." : "Enable Always on Top.");
    mouseInteractionButton.setToggleState(allMouseEventsPassThrough, juce::NotificationType::dontSendNotification);
    if (allMouseEventsPassThrough) {
        mouseInteractionButton.setTooltip("Keep This Window Interactive.");
    } else {
        mouseInteractionButton.setTooltip(paused ? "Let Clicks Pass Through After Resuming."
                                                 : "Let Clicks Pass Through.");
    }
    fullscreenButton.setTooltip(fullScreen ? "Exit Fullscreen." : "Enter Fullscreen.");
    clickThroughHintVisible = showClickThroughHint;
    repaint();
}

bool PopoutToolbar::hitTest(int x, int y) {
    if (!frameVisible) {
        return false;
    }
    if (y < toolbarHeight) {
        return true;
    }
    for (auto* child : getChildren()) {
        if (child->getBounds().contains(x, y)) {
            return true;
        }
    }
    return false;
}

void PopoutToolbar::paint(juce::Graphics& g) {
    if (frameVisible) {
        constexpr float cornerRadius = 11.0f;
        const auto alphaFloor = osci::windowing::getInteractiveAlphaFloor();
        if (alphaFloor > 0.0f) {
            g.fillAll(juce::Colours::black.withAlpha(alphaFloor));
        }
        auto bounds = getLocalBounds();
        g.setColour(osci::Colours::veryDark());
        g.fillRect(bounds.removeFromTop(toolbarHeight));
        if (!fullScreen) {
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), cornerRadius - 0.5f, 1.0f);
        }
    }
    if (clickThroughHintVisible) {
        const auto hintWidth = juce::jmax(1, juce::jmin(620, getWidth() - 24));
        const auto hintBounds = getLocalBounds().withSizeKeepingCentre(hintWidth, 58).toFloat();
        g.setColour(osci::Colours::veryDark().withAlpha(0.94f));
        g.fillRoundedRectangle(hintBounds, 8.0f);
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawFittedText("Clicks now pass through this window. Pause the main visualiser to bring the controls "
                         "back, or close and reopen the popout.",
                         hintBounds.reduced(14.0f).toNearestInt(), juce::Justification::centred, 2);
    }
}

void PopoutToolbar::resized() {
    auto bar = getLocalBounds().removeFromTop(toolbarHeight);
#if JUCE_WINDOWS || JUCE_LINUX
    closeButton.setBounds(bar.removeFromRight(toolbarHeight).reduced(3));
    fullscreenButton.setBounds(bar.removeFromLeft(toolbarHeight).reduced(4));
    frameButton.setBounds(bar.removeFromLeft(toolbarHeight).reduced(4));
    mouseInteractionButton.setBounds(bar.removeFromLeft(toolbarHeight).reduced(4));
    alwaysOnTopButton.setBounds(bar.removeFromLeft(toolbarHeight).reduced(4));
#else
    closeButton.setBounds(bar.removeFromLeft(toolbarHeight).reduced(3));
    fullscreenButton.setBounds(bar.removeFromRight(toolbarHeight).reduced(4));
    frameButton.setBounds(bar.removeFromRight(toolbarHeight).reduced(4));
    mouseInteractionButton.setBounds(bar.removeFromRight(toolbarHeight).reduced(4));
    alwaysOnTopButton.setBounds(bar.removeFromRight(toolbarHeight).reduced(4));
#endif
}

void PopoutToolbar::mouseDown(const juce::MouseEvent& event) {
    if (!event.mods.isLeftButtonDown() || fullScreen) {
        return;
    }
    dragger.startDraggingComponent(getTopLevelComponent(), event.getEventRelativeTo(getTopLevelComponent()));
}

void PopoutToolbar::mouseDoubleClick(const juce::MouseEvent& event) {
#if JUCE_WINDOWS
    if (event.mods.isLeftButtonDown() && onFullScreen != nullptr) {
        onFullScreen();
    }
#else
    juce::ignoreUnused(event);
#endif
}

void PopoutToolbar::mouseDrag(const juce::MouseEvent& event) {
    if (!fullScreen) {
        dragger.dragComponent(getTopLevelComponent(), event.getEventRelativeTo(getTopLevelComponent()), nullptr);
    }
}

void PopoutToolbar::mouseUp(const juce::MouseEvent&) {}

#endif

VisualiserWindow::VisualiserWindow(juce::String name, VisualiserComponent* parent, bool pinned)
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    : juce::DocumentWindow(name,
                           osci::windowing::isTransparencySupported() ? juce::Colours::transparentBlack : juce::Colours::black,
                           osci::windowing::isTransparencySupported() ? 0 : juce::DocumentWindow::TitleBarButtons::allButtons),
#else
    : juce::DocumentWindow(name, juce::Colours::black, juce::DocumentWindow::TitleBarButtons::allButtons),
#endif
      parent(parent),
      pinned(pinned) {
    setAlwaysOnTop(pinned);
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    const bool transparencySupported = osci::windowing::isTransparencySupported();
    if (transparencySupported) {
        setTitleBarHeight(0);
    }
    setOpaque(!transparencySupported);
#if JUCE_WINDOWS
    if (transparencySupported) {
        // JUCE implements non-native shadows as four separate windows. They remain visible and
        // interactive after the popout frame is hidden, so transparent popouts must not use them.
        setDropShadowEnabled(false);
    }
#endif
#endif
}

VisualiserWindow::~VisualiserWindow() {
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    stopTimer();
    osci::windowing::setIgnoresMouseEvents(this, false);
#endif
}

bool VisualiserWindow::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (fullScreenRequested) {
            toggleFullScreen();
        } else {
            closeButtonPressed();
        }
        return true;
    }
    return juce::DocumentWindow::keyPressed(key);
}

void VisualiserWindow::closeButtonPressed() {
    VisualiserComponent* parent = this->parent;
    saveWindowState();
    parent->child = nullptr;
    parent->popout.reset();
    parent->childUpdated();
    parent->resized();
}

void VisualiserWindow::toggleFullScreen() {
    fullScreenRequested = !fullScreenRequested;
    fullScreenTransitionPending = true;
#if OSCI_PREMIUM && (JUCE_WINDOWS || JUCE_MAC)
    reenterFullScreenAfterTransition = false;
    setAlwaysOnTop(pinned);
    if (fullScreenRequested) {
        transparentFullScreen = parent != nullptr && parent->isTransparentBackgroundEnabled();
        if (transparentFullScreen) {
            boundsBeforeFullScreen = getBounds();
            enterTransparentFullScreen();
        } else {
#if JUCE_MAC
            osci::windowing::setMovesToActiveSpace(this, false);
#endif
            juce::ResizableWindow::setFullScreen(true);
        }
    } else {
        if (transparentFullScreen) {
            if (!boundsBeforeFullScreen.isEmpty()) {
                const juce::ScopedValueSetter<bool> settingBounds(settingTransparentFullScreenBounds, true);
                setBounds(boundsBeforeFullScreen);
            }
            transparentFullScreen = false;
#if JUCE_MAC
            osci::windowing::setMovesToActiveSpace(this, false);
#endif
        } else {
            juce::ResizableWindow::setFullScreen(false);
        }
        transparentFullScreenBounds = {};
    }
#else
    juce::ResizableWindow::setFullScreen(fullScreenRequested);
#endif
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    updatePresentationState();
#endif
}

void VisualiserWindow::setPinned(bool shouldBePinned) {
    pinned = shouldBePinned;
    setAlwaysOnTop(pinned);
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
#if JUCE_WINDOWS
    if (osci::windowing::isTransparencySupported()) {
        refreshNativePresentation();
        return;
    }
#endif
    updatePresentationState();
#endif
}

void VisualiserWindow::saveWindowState() {
    auto normalBounds = getBounds();
#if JUCE_WINDOWS || JUCE_MAC
    if (fullScreenRequested && !boundsBeforeFullScreen.isEmpty()) {
        normalBounds = boundsBeforeFullScreen;
    }
#endif
    if (parent != nullptr) {
        parent->savePopoutWindowState(normalBounds, fullScreenRequested);
    }
}

#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
void VisualiserWindow::setRequestedFrameVisible(bool visible) {
    presentationState.requestedFrameVisible = visible;
    updatePresentationState();
}

void VisualiserWindow::setAllMouseEventsPassThrough(bool shouldPassThrough, bool showHint) {
    allMouseEventsPassThrough = shouldPassThrough;
    if (shouldPassThrough) {
        presentationState.requestedFrameVisible = false;
        const bool paused = parent != nullptr && parent->isPaused();
        const bool transparencyEnabled = parent != nullptr && parent->isTransparentBackgroundEnabled();
        clickThroughHintVisible = showHint && transparencyEnabled && !paused;
        if (clickThroughHintVisible) {
            clickThroughHintEndTime = juce::Time::getMillisecondCounter() + 5000;
        }
    } else {
        clickThroughHintVisible = false;
    }
    updatePresentationState();
}

void VisualiserWindow::refreshNativePresentation() {
    if (parent == nullptr || parent->child == nullptr) {
        return;
    }
    presentationRefreshGeneration = parent->child->getAlphaMaskGeneration();
    presentationRefreshActive = true;
    updatePresentationState();
}

void VisualiserWindow::transparencyModeChanged() {
    if (fullScreenRequested) {
        const bool shouldUseTransparentFullScreen = parent != nullptr && parent->isTransparentBackgroundEnabled();
        if (shouldUseTransparentFullScreen && !transparentFullScreen) {
            reenterFullScreenAfterTransition = true;
            fullScreenTransitionPending = true;
            juce::ResizableWindow::setFullScreen(false);
        } else if (!shouldUseTransparentFullScreen) {
            reenterFullScreenAfterTransition = false;
            if (transparentFullScreen) {
                transparentFullScreen = false;
                transparentFullScreenBounds = {};
                if (!boundsBeforeFullScreen.isEmpty()) {
                    const juce::ScopedValueSetter<bool> settingBounds(settingTransparentFullScreenBounds, true);
                    setBounds(boundsBeforeFullScreen);
                }
#if JUCE_MAC
                osci::windowing::setMovesToActiveSpace(this, false);
#endif
            }
            fullScreenTransitionPending = true;
            juce::ResizableWindow::setFullScreen(true);
        }
        updatePresentationState();
        return;
    }
    if (reenterFullScreenAfterTransition) {
        return;
    }
    if (parent != nullptr && parent->isTransparentBackgroundEnabled()) {
        refreshNativePresentation();
    } else {
        presentationRefreshActive = false;
        updatePresentationState();
    }
}

void VisualiserWindow::pauseStateChanged() {
    updatePresentationState();
}

void VisualiserWindow::resized() {
    juce::ResizableWindow::resized();
#if JUCE_MAC || JUCE_WINDOWS
    if (osci::windowing::isTransparencySupported() && getContentComponent() != nullptr) {
        getContentComponent()->setBounds(getLocalBounds());
    }
    if (fullScreenRequested && transparentFullScreen && !settingTransparentFullScreenBounds
        && !transparentFullScreenBounds.isEmpty() && getBounds() != transparentFullScreenBounds) {
        leaveTransparentFullScreenAfterResize();
    }
#endif
    const bool nativeFullScreen = juce::ResizableWindow::isFullScreen();
    updateResizeBorderVisibility(presentationState.isFrameVisible() && !fullScreenRequested && !nativeFullScreen);
}

void VisualiserWindow::leaveTransparentFullScreenAfterResize() {
    fullScreenRequested = false;
    fullScreenTransitionPending = false;
    transparentFullScreen = false;
    transparentFullScreenBounds = {};
    reenterFullScreenAfterTransition = false;
#if JUCE_MAC
    osci::windowing::setMovesToActiveSpace(this, false);
#endif
    setAlwaysOnTop(pinned);
    updatePresentationState();
}

void VisualiserWindow::enterTransparentFullScreen() {
    transparentFullScreen = true;
#if JUCE_MAC
    osci::windowing::setMovesToActiveSpace(this, true);
#endif
    const auto displayBounds = boundsBeforeFullScreen.isEmpty() ? getBounds() : boundsBeforeFullScreen;
    auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(displayBounds);
    if (display != nullptr) {
#if JUCE_WINDOWS
        // An exact monitor-sized window can be promoted out of desktop composition on Windows,
        // which loses per-pixel alpha. A one-pixel inset keeps the same window composited.
        transparentFullScreenBounds = display->totalArea.reduced(1);
#else
        transparentFullScreenBounds = display->totalArea;
#endif
        const juce::ScopedValueSetter<bool> settingBounds(settingTransparentFullScreenBounds, true);
        setBounds(transparentFullScreenBounds);
    }
    fullScreenTransitionPending = true;
}

void VisualiserWindow::timerCallback() {
    updatePresentationState();
}

void VisualiserWindow::updatePresentationState() {
    const auto now = juce::Time::getMillisecondCounter();
    const bool transparencyEnabled = parent != nullptr && parent->isTransparentBackgroundEnabled();
    presentationState.paused = parent != nullptr && parent->isPaused();
    if (presentationState.paused) {
        clickThroughHintVisible = false;
    }
    if (clickThroughHintVisible && static_cast<std::int32_t>(now - clickThroughHintEndTime) >= 0) {
        clickThroughHintVisible = false;
    }
    if (!transparencyEnabled) {
        presentationRefreshActive = false;
        clickThroughHintVisible = false;
        presentationState.resetAlphaInteraction();
        hasAlphaPassThroughAnchor = false;
    }

    const bool nativeFullScreen = juce::ResizableWindow::isFullScreen();
#if JUCE_WINDOWS || JUCE_MAC
    if (reenterFullScreenAfterTransition && !nativeFullScreen) {
        reenterFullScreenAfterTransition = false;
        boundsBeforeFullScreen = getBounds();
        enterTransparentFullScreen();
    }
    const bool fullScreenTransitionComplete = !reenterFullScreenAfterTransition
                                           && (transparentFullScreen || nativeFullScreen == fullScreenRequested);
    if (fullScreenTransitionPending && fullScreenTransitionComplete) {
        fullScreenTransitionPending = false;
        osci::windowing::configureTransparency(this);
        setAlwaysOnTop(pinned);
        if (parent != nullptr && parent->child != nullptr) {
            parent->child->refreshOpenGLSurfaceTransparency();
#if JUCE_WINDOWS
            presentationRefreshGeneration = parent->child->getAlphaMaskGeneration();
            presentationRefreshActive = true;
#endif
        }
        if (!fullScreenRequested) {
            transparentFullScreen = false;
        }
    }
#endif

    if (presentationRefreshActive && parent != nullptr && parent->child != nullptr
        && parent->child->getAlphaMaskGeneration() != presentationRefreshGeneration) {
        presentationRefreshActive = false;
        osci::windowing::configureTransparency(this);
        setAlwaysOnTop(pinned);
    }

    const bool fullPassThroughActive = transparencyEnabled && allMouseEventsPassThrough && !presentationState.paused;
    if (fullPassThroughActive) {
        presentationState.resetAlphaInteraction();
        hasAlphaPassThroughAnchor = false;
    }
    const bool frameVisible = presentationState.isFrameVisible()
                           && !presentationRefreshActive
                           && !fullPassThroughActive;
    const bool fullScreenActive = fullScreenRequested || nativeFullScreen;
    const bool framedWindow = frameVisible && !fullScreenActive;
    updateResizeBorderVisibility(framedWindow);
    if (!windowCornersConfigured || windowCornersRounded != framedWindow) {
        osci::windowing::setRoundedWindowRegion(this, framedWindow ? 11.0f : 0.0f);
        windowCornersConfigured = true;
        windowCornersRounded = framedWindow;
    }
    if (parent != nullptr && parent->child != nullptr) {
        const bool alphaMaskNeeded = transparencyEnabled && !fullPassThroughActive
                                  && (!frameVisible || presentationRefreshActive);
        parent->child->setAlphaMaskCaptureEnabled(alphaMaskNeeded);
        parent->child->setPopoutPresentationOverlay(frameVisible, presentationState.requestedFrameVisible,
                                                     pinned, fullScreenActive,
                                                     transparencyEnabled && allMouseEventsPassThrough,
                                                     presentationState.paused, clickThroughHintVisible,
                                                     transparencyEnabled);
    }
#if JUCE_WINDOWS
    const bool forceFullScreenInteraction = fullScreenActive;
#else
    const bool forceFullScreenInteraction = false;
#endif
    if (!transparencyEnabled || forceFullScreenInteraction) {
        // WS_EX_LAYERED is required for cross-process click-through, but it makes a fullscreen
        // OpenGL window composite as opaque black. Keep fullscreen on the GPU transparency path.
        applyNativeInteraction(false);
    } else if (presentationRefreshActive || fullPassThroughActive) {
        applyNativeInteraction(true);
    } else if (frameVisible) {
        applyNativeInteraction(false);
    } else {
        bool alphaHit = false;
        const auto screenCursor = juce::Desktop::getMousePosition();
        if (parent != nullptr && parent->child != nullptr) {
            const auto frameSize = parent->child->getAlphaMaskSize();
            const auto cursor = parent->child->getLocalPoint(nullptr, screenCursor);
            const float scale = getPeer() != nullptr ? static_cast<float>(getPeer()->getPlatformScaleFactor()) : 1.0f;
            const auto query = makePopoutAlphaQuery(parent->child->getLocalBounds(), frameSize, cursor,
                                                     8.0f / juce::jmax(1.0f, scale));
            if (query.valid) {
                alphaHit = parent->child->alphaMaskHasAlphaNear(query.normalisedPoint, query.normalisedRadius,
                                                                popoutInteractionAlphaThreshold);
            }
        }
        if (nativeIgnoresMouseEvents && !alphaHit) {
            alphaPassThroughAnchor = screenCursor;
            hasAlphaPassThroughAnchor = true;
        }
        const bool movedBeyondPadding = !hasAlphaPassThroughAnchor
                                     || alphaPassThroughAnchor.getDistanceFrom(screenCursor) > 8.0;
        presentationState.updateAlphaHit(alphaHit, nativeIgnoresMouseEvents, movedBeyondPadding, now);
        applyNativeInteraction(!presentationState.isAlphaInteractionHeld(now));
    }

    const bool needsTimer = (transparencyEnabled && !presentationState.requestedFrameVisible)
                         || fullScreenTransitionPending
                         || reenterFullScreenAfterTransition
                         || (transparencyEnabled && presentationRefreshActive)
                         || (transparencyEnabled && allMouseEventsPassThrough)
                         || (transparencyEnabled && clickThroughHintVisible);
    if (needsTimer && !isTimerRunning()) {
        startTimerHz(60);
    } else if (!needsTimer) {
        stopTimer();
    }
}

void VisualiserWindow::applyNativeInteraction(bool ignoresMouseEvents) {
    if (nativeIgnoresMouseEvents == ignoresMouseEvents
        && osci::windowing::isMouseInteractionStateApplied(this, ignoresMouseEvents)) {
        return;
    }
    if (ignoresMouseEvents) {
        alphaPassThroughAnchor = juce::Desktop::getMousePosition();
        hasAlphaPassThroughAnchor = true;
    } else {
        hasAlphaPassThroughAnchor = false;
    }
    nativeIgnoresMouseEvents = ignoresMouseEvents;
    osci::windowing::setIgnoresMouseEvents(this, ignoresMouseEvents);
}

void VisualiserWindow::updateResizeBorderVisibility(bool visible) {
    for (auto* component : getChildren()) {
        auto* resizeBorder = dynamic_cast<juce::ResizableBorderComponent*>(component);
        if (resizeBorder != nullptr) {
            resizeBorder->setAlpha(0.0f);
            resizeBorder->setVisible(visible);
        }
    }
}
#endif

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
    setNativeTransparencySupported(parent != nullptr && osci::windowing::isTransparencySupported());

    // Sync active state with the parameter for the primary visualiser
    if (isPrimaryVisualiser()) {
        active = !audioProcessor.visualiserParameters.visualiserPaused->getBoolValue();
        audioProcessor.visualiserParameters.visualiserPaused->addListener(this);
        audioProcessor.visualiserParameters.textureOutputEnabled->addListener(this);
        audioProcessor.visualiserParameters.transparentBackground->addListener(this);
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
        popOutButton.setClickingTogglesState(false);
        popOutButton.setTooltip("Open Visualiser Popout.");
    }
#endif
    addAndMakeVisible(settingsButton);
    settingsButton.setTooltip("Opens the visualiser settings window.");

    addAndMakeVisible(textureOutputButton);
    textureOutputButton.setClickingTogglesState(false);
    textureOutputButton.setToggleState(false, juce::NotificationType::dontSendNotification);
    textureOutputButton.onClick = [this] {
#if OSCI_PREMIUM
        const bool currentlyRequestedOrRunning = this->settings.parameters.textureOutputEnabled->getBoolValue() || textureOutputPublisher.isRunning();
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
        if (popout != nullptr) {
            popout->closeButtonPressed();
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

    // Initialize timeline for standalone premium builds
    // Controller will be set by parent component
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

        if (record.getToggleState()) {
#if OSCI_PREMIUM
            if (recordingVideo) {
                // draw frame to ffmpeg
                Texture renderTexture = getRenderTexture();
                if (renderTexture.width != recordingRenderSize.width || renderTexture.height != recordingRenderSize.height) {
                    return;
                }
                if (framePixels.size() != static_cast<size_t>(renderTexture.width * renderTexture.height * 4)) {
                    framePixels.resize(renderTexture.width * renderTexture.height * 4);
                }
                getFrame(framePixels);
                if (ffmpegProcess.write(framePixels.data(), 4 * renderTexture.width * renderTexture.height, VideoEncodingConstants::frameWriteTimeoutMs) == 0) {
                    record.setToggleState(false, juce::NotificationType::dontSendNotification);

                    juce::Component::SafePointer<VisualiserComponent> safeThis(this);
                    juce::MessageManager::callAsync([safeThis] {
                        if (safeThis != nullptr) {
                            osci::showOverlayMessage(*safeThis.getComponent(),
                                                     "Recording Error",
                                                     "An error occurred while writing the video frame to the ffmpeg process. Recording has been stopped.");
                        }
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
    if (popout != nullptr) {
        popout->saveWindowState();
    }
    // Stop the background thread while VisualiserComponent's vtable is still live.
    // If deferred to ~VisualiserRenderer, the vptr has already changed and the
    // running thread's virtual run()/runTask() dispatch becomes a data race.
    setShouldBeRunning(false, [this] { renderingSemaphore.release(); });
    textureOutputPublisher.stop();
    setRecording(false);
    audioProcessor.removeAudioPlayerListener(this);
    if (isPrimaryVisualiser()) {
        audioProcessor.visualiserParameters.visualiserPaused->removeListener(this);
        audioProcessor.visualiserParameters.textureOutputEnabled->removeListener(this);
        audioProcessor.visualiserParameters.transparentBackground->removeListener(this);
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
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
        if (popout != nullptr) {
            popout->pauseStateChanged();
        }
#endif
    }

    repaint();
    if (child != nullptr) {
        child->repaint();
    }
}

bool VisualiserComponent::isPaused() const {
    return !active;
}

bool VisualiserComponent::isTransparentBackgroundEnabled() const {
    return settings.parameters.isTransparentBackgroundEnabled();
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
    juce::ignoreUnused(newValue);
    const bool transparencyChanged = parameterIndex == audioProcessor.visualiserParameters.transparentBackground->getParameterIndex();
    auto safeThis = juce::Component::SafePointer<VisualiserComponent>(this);
    juce::MessageManager::callAsync([safeThis, transparencyChanged] {
        if (safeThis == nullptr) {
            return;
        }
        safeThis->updatePausedState();
        safeThis->refreshTextureOutputButton();
        safeThis->requestTextureOutputService();
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
        if (transparencyChanged && safeThis->popout != nullptr) {
            safeThis->popout->transparencyModeChanged();
        }
#endif
    });
}

void VisualiserComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {
    // Not needed for this parameter
}

void VisualiserComponent::mouseDrag(const juce::MouseEvent& event) {
    timerId = -1;
    if (event.getDistanceFromDragStart() > 4) {
        pauseOnMouseUp = false;
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
        if (parent != nullptr && event.mods.isLeftButtonDown()) {
            auto* window = dynamic_cast<VisualiserWindow*>(getTopLevelComponent());
            if (window != nullptr && !window->getIsFullScreen()) {
                popoutDragger.dragComponent(window, event.getEventRelativeTo(window), nullptr);
            }
        }
#endif
    }
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
                            if (fullScreen) {
                                setMouseCursor(juce::MouseCursor::NoCursor);
                            }
                            resized();
                        }
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
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
            if (parent != nullptr) {
                auto* window = dynamic_cast<VisualiserWindow*>(getTopLevelComponent());
                if (window != nullptr && !window->getIsFullScreen()) {
                    popoutDragger.startDraggingComponent(window, event.getEventRelativeTo(window));
                }
            }
#endif
        }
    }
}

void VisualiserComponent::mouseUp(const juce::MouseEvent& event) {
    const bool shouldTogglePause = pauseOnMouseUp && event.getDistanceFromDragStart() <= 4;
    pauseOnMouseUp = false;
    if (!shouldTogglePause || record.getToggleState()) {
        return;
    }

    if (isMirrorMode() && parent != nullptr) {
        parent->setPaused(parent->active);
    } else {
        setPaused(active);
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
        recordingTransparency = recordingVideo && settings.parameters.isTransparentBackgroundEnabled();
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

            ffmpegEncoderManager.refreshAvailableEncoders();
            const auto codec = recordingSettings.getVideoCodec();
            if (!ffmpegEncoderManager.supportsVideoCodec(codec)) {
                record.setToggleState(false, juce::NotificationType::dontSendNotification);
                const auto errorMessage = recordingTransparency
                    ? "This FFmpeg installation does not include the ProRes 4444 encoder required for transparent video."
                    : "This FFmpeg installation does not include an encoder for the selected video codec.";
                osci::showOverlayMessage(*this,
                                         "Recording Error",
                                         errorMessage);
                return;
            }

            const auto canvasSize = recordingSettings.getCanvasSize();
            recordingRenderSize = canvasSize;
            setRenderSize(canvasSize);

            // Get the appropriate file extension based on codec
            juce::String fileExtension = recordingTransparency ? "mov" : recordingSettings.getFileExtensionForCodec();
            tempVideoFile = std::make_unique<juce::TemporaryFile>("." + fileExtension);

            juce::String cmd = ffmpegEncoderManager.buildVideoEncodingCommand(
                codec,
                recordingSettings.getCRF(),
                canvasSize.width,
                canvasSize.height,
                recordingSettings.getFrameRate(),
                recordingSettings.getCompressionPreset(),
                tempVideoFile->getFile(),
                recordingTransparency);

            if (cmd.isEmpty() || !ffmpegProcess.start(cmd)) {
                juce::Logger::writeToLog("Recording: ffmpegProcess.start() failed for command: " + cmd);
                record.setToggleState(false, juce::NotificationType::dontSendNotification);
                juce::Component::SafePointer<VisualiserComponent> safeThis(this);
                juce::MessageManager::callAsync([safeThis] {
                    if (safeThis != nullptr) {
                        osci::showOverlayMessage(*safeThis.getComponent(),
                                                 "Recording Error",
                                                 "Failed to start the FFmpeg video encoder.\n\n"
                                                 "Please check that FFmpeg is compatible with your system.");
                    }
                });
                return;
            }
            framePixels.resize(canvasSize.width * canvasSize.height * 4);
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
        bool wasRecordingTransparency = recordingTransparency;
        recordingAudio = false;
        recordingVideo = false;
        recordingTransparency = false;

        juce::String extension = wasRecordingVideo ? (wasRecordingTransparency ? "mov" : recordingSettings.getFileExtensionForCodec()) : "wav";
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
        const auto timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S");
        const auto suggestedFile = audioProcessor.getLastOpenedDirectory().getChildFile(editor.appName + "_" + timestamp + "." + extension);
        chooser = std::make_unique<juce::FileChooser>("Save recording", suggestedFile, "*." + extension);
        auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting;

#if OSCI_PREMIUM
        chooser->launchAsync(flags, [this, wasRecordingAudio, wasRecordingVideo, extension](const juce::FileChooser &chooser) {
            auto file = chooser.getResult();
            if (file != juce::File()) {
                // Ensure the file has the correct extension
                if (!file.hasFileExtension(extension)) {
                    file = file.withFileExtension(extension);
                }

                bool saved = false;
                if (wasRecordingAudio && wasRecordingVideo) {
                    juce::String muxError;
                    saved = ffmpegEncoderManager.muxAudioAndVideo(tempVideoFile->getFile(), tempAudioFile->getFile(), file, recordingSettings.getAudioCodecArgs(), muxError);
                    if (!saved && muxError.isNotEmpty()) {
                        juce::Logger::writeToLog("Recording mux failed: " + muxError.substring(0, 500));
                    }
                } else if (wasRecordingAudio) {
                    saved = tempAudioFile->getFile().copyFileTo(file);
                } else if (wasRecordingVideo) {
                    saved = tempVideoFile->getFile().copyFileTo(file);
                }
                if (!saved) {
                    osci::showOverlayMessage(*this, "Save Recording Failed", "Could not write:\n" + file.getFullPathName());
                    return;
                }
                audioProcessor.setLastOpenedDirectory(file.getParentDirectory());
                audioProcessor.recordingExportCompleted(file);
            } });
#else
        chooser->launchAsync(flags, [this, extension](const juce::FileChooser &chooser) {
            auto file = chooser.getResult();
            if (file != juce::File()) {
                // Ensure the file has the correct extension
                if (!file.hasFileExtension(extension)) {
                    file = file.withFileExtension(extension);
                }

                if (!tempAudioFile->getFile().copyFileTo(file)) {
                    osci::showOverlayMessage(*this, "Save Recording Failed", "Could not write:\n" + file.getFullPathName());
                    return;
                }
                audioProcessor.setLastOpenedDirectory(file.getParentDirectory());
                audioProcessor.recordingExportCompleted(file);
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
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    if (popoutToolbar != nullptr) {
        popoutToolbar->setBounds(getLocalBounds());
    }
#endif
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
    if (parent == nullptr) {
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
    const auto windowTitle = editor.appName + " - Software Oscilloscope";
    popout = std::make_unique<VisualiserWindow>(windowTitle, this, isPopoutAlwaysOnTop());
    popout->setContentOwned(visualiser, false);
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    popout->setUsingNativeTitleBar(!osci::windowing::isTransparencySupported());
#else
    popout->setUsingNativeTitleBar(true);
#endif
    popout->setResizable(true, false);
    // Register editor as KeyListener so undo/redo shortcuts work in the popout window
    popout->addKeyListener(&editor);
    const auto savedBounds = getSavedPopoutBounds();
    if (savedBounds.isEmpty()) {
        popout->centreWithSize(350, 350);
    } else {
        popout->setBounds(savedBounds);
    }
    popout->setVisible(true);
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    if (osci::windowing::isTransparencySupported()) {
        osci::windowing::configureTransparency(popout.get());
        visualiser->popoutToolbar = std::make_unique<PopoutToolbar>();
        visualiser->popoutToolbar->onClose = [this] {
            popout->closeButtonPressed();
        };
        visualiser->popoutToolbar->onFullScreen = [this] {
            popout->toggleFullScreen();
        };
        visualiser->popoutToolbar->onToggleFrame = [this] {
            setPopoutFrameVisible(!popout->isFrameRequestedVisible());
        };
        visualiser->popoutToolbar->onToggleAlwaysOnTop = [this] {
            setPopoutAlwaysOnTop(!popout->isPinned());
        };
        visualiser->popoutToolbar->onToggleMouseInteraction = [this] {
            setPopoutClicksPassThrough(!popout->getAllMouseEventsPassThrough());
        };
        visualiser->addAndMakeVisible(*visualiser->popoutToolbar);
        visualiser->setPopoutPresentationOverlay(true, true, popout->isPinned(), false, false, false, false);
    }
#endif
    // Hide all buttons on the popout and set up mirror mode
    visualiser->hideButtonRow = true;
    visualiser->resized();
    // Set up mirror mode AFTER the window is visible so the GL context is active
    visualiser->setMirrorSource(this);
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    if (osci::windowing::isTransparencySupported()) {
        popout->setAllMouseEventsPassThrough(doPopoutClicksPassThrough(), false);
        popout->setRequestedFrameVisible(isPopoutFrameVisible());
    }
#endif
#if OSCI_PREMIUM && JUCE_WINDOWS
    if (osci::windowing::isTransparencySupported()) {
        popout->refreshNativePresentation();
    }
#endif
    if (wasPopoutFullScreen()) {
        popout->toggleFullScreen();
    }
    // A plugin host can allow JUCE's first OpenGL component paint before the popout peer has
    // its final display scale. Repaint once after the peer, bounds, mirror context, and
    // presentation overlay are configured so JUCE rebuilds that layer at the physical scale.
    visualiser->repaint();
    resized();
#endif
}

void VisualiserComponent::childUpdated() {
#if OSCI_PREMIUM
    popOutButton.setVisible(parent == nullptr);
    popOutButton.setToggleState(child != nullptr, juce::NotificationType::dontSendNotification);
    popOutButton.setTooltip(child != nullptr ? "Close Visualiser Popout." : "Open Visualiser Popout.");
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

void VisualiserComponent::setPopoutAlwaysOnTop(bool alwaysOnTop) {
    audioProcessor.globalSettings.set("popoutAlwaysOnTop", alwaysOnTop);
    audioProcessor.globalSettings.save();
    if (popout != nullptr) {
        popout->setPinned(alwaysOnTop);
    }
}

bool VisualiserComponent::isPopoutAlwaysOnTop() const {
    return audioProcessor.globalSettings.getBool("popoutAlwaysOnTop", true);
}

void VisualiserComponent::setPopoutFrameVisible(bool visible) {
    audioProcessor.globalSettings.set("popoutFrameVisible", visible);
    audioProcessor.globalSettings.save();
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    if (popout != nullptr) {
        popout->setRequestedFrameVisible(visible);
    }
#endif
}

bool VisualiserComponent::isPopoutFrameVisible() const {
    return audioProcessor.globalSettings.getBool("popoutFrameVisible", true);
}

void VisualiserComponent::setPopoutClicksPassThrough(bool clicksPassThrough) {
    audioProcessor.globalSettings.set("popoutClicksPassThrough", clicksPassThrough);
    audioProcessor.globalSettings.save();
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    if (popout != nullptr) {
        popout->setAllMouseEventsPassThrough(clicksPassThrough);
    }
#endif
}

bool VisualiserComponent::doPopoutClicksPassThrough() const {
    return audioProcessor.globalSettings.getBool("popoutClicksPassThrough", false);
}

void VisualiserComponent::savePopoutWindowState(juce::Rectangle<int> bounds, bool fullScreen) {
    if (!bounds.isEmpty()) {
        audioProcessor.globalSettings.set("popoutWindowBounds", bounds.toString());
    }
    audioProcessor.globalSettings.set("popoutFullScreen", fullScreen);
    audioProcessor.globalSettings.save();
}

juce::Rectangle<int> VisualiserComponent::getSavedPopoutBounds() const {
    auto bounds = juce::Rectangle<int>::fromString(audioProcessor.globalSettings.getString("popoutWindowBounds"));
    if (bounds.getWidth() < 100 || bounds.getHeight() < 100) {
        return {};
    }

    const auto& displays = juce::Desktop::getInstance().getDisplays();
    auto* display = displays.getDisplayForRect(bounds);
    if (display == nullptr) {
        display = displays.getPrimaryDisplay();
    }
    if (display == nullptr) {
        return bounds;
    }

    const auto available = display->userArea;
    bounds.setSize(juce::jmin(bounds.getWidth(), available.getWidth()),
                   juce::jmin(bounds.getHeight(), available.getHeight()));
    return bounds.constrainedWithin(available);
}

bool VisualiserComponent::wasPopoutFullScreen() const {
    return audioProcessor.globalSettings.getBool("popoutFullScreen", false);
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
    const bool running = textureOutputPublisher.isRunning();

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
        osci::showOverlayMessage(*this,
                                 "Texture Output",
                                 "Texture output cannot start until the visualiser has rendered a frame.");
        refreshTextureOutputButton();
        requestTextureOutputService();
        return;
    }

    const osci::texture::BackendStatus status = osci::texture::getOpenGLBackendStatus();
    if (!status.isAvailable() || !isPrimaryVisualiser()) {
        const juce::String message = status.message.isNotEmpty()
            ? status.message
            : "Texture output is not available in this build.";
        osci::showOverlayMessage(*this, "Texture Output", message, osci::ErrorOverlay::Icon::None);
        refreshTextureOutputButton();
        requestTextureOutputService();
        return;
    }

    textureOutputPublisher.setSourceName(recordingSettings.getCustomTextureOutputName());
    settings.parameters.textureOutputEnabled->setBoolValueNotifyingHost(true);
    refreshTextureOutputButton();
    requestTextureOutputService();
}

void VisualiserComponent::requestTextureOutputService() {
    openGLContext.triggerRepaint();
}

void VisualiserComponent::serviceTextureOutputFrame() {
#if !OSCI_PREMIUM
    textureOutputPublisher.stop();

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

    const bool shouldRun = settings.parameters.textureOutputEnabled->getBoolValue();
    if (shouldRun && !textureOutputPublisher.isRunning()) {
        textureOutputPublisher.setSourceName(recordingSettings.getCustomTextureOutputName());
    }

    const Texture renderTexture = getRenderTexture();
    handleTextureOutputServiceResult(textureOutputPublisher.serviceTexture2D(shouldRun,
                                                                             static_cast<std::uint32_t>(renderTexture.id),
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
    textureOutputPublisher.stop();

    VisualiserRenderer::openGLContextClosing();
}

void VisualiserComponent::newOpenGLContextCreated() {
    VisualiserRenderer::newOpenGLContextCreated();
#if OSCI_PREMIUM && JUCE_MAC
    osci::windowing::configureOpenGLSurface(openGLContext.getRawContext());
#elif OSCI_PREMIUM && JUCE_WINDOWS
    if (parent != nullptr) {
        osci::windowing::configureOpenGLSurface(openGLContext.getRawContext());
    }
#endif
}

#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
void VisualiserComponent::setPopoutPresentationOverlay(bool frameVisible, bool requestedFrameVisible,
                                                        bool alwaysOnTop, bool fullScreen,
                                                        bool allMouseEventsPassThrough, bool paused,
                                                        bool clickThroughHintVisible,
                                                        bool transparencyEnabled) {
    if (popoutToolbar == nullptr) {
        return;
    }
    popoutToolbar->setState(frameVisible, requestedFrameVisible, alwaysOnTop, fullScreen,
                            allMouseEventsPassThrough, paused, clickThroughHintVisible,
                            transparencyEnabled);
    popoutToolbar->setVisible(frameVisible || clickThroughHintVisible);
    openGLContext.setComponentPaintingEnabled(frameVisible || clickThroughHintVisible);
}
#endif

#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
void VisualiserComponent::refreshOpenGLSurfaceTransparency() {
    openGLContext.executeOnGLThread([](juce::OpenGLContext& context) {
        osci::windowing::configureOpenGLSurface(context.getRawContext());
    }, false);
}
#endif

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
