#include "VisualiserPopout.h"

#include "VisualiserComponent.h"
#include "../LookAndFeel.h"
#include "PopoutInteractionGeometry.h"
#include "TransparentWindow.h"

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
    setState({});
    setInterceptsMouseClicks(true, true);
}

void PopoutToolbar::setState(const PopoutPresentation& state) {
    if (hasPresentation && presentation == state) {
        return;
    }
    presentation = state;
    hasPresentation = true;
    frameVisible = state.frameVisible;
    fullScreen = state.fullScreen;
    closeButton.setVisible(state.frameVisible);
    fullscreenButton.setVisible(state.frameVisible);
    frameButton.setVisible(state.frameVisible);
    alwaysOnTopButton.setVisible(state.frameVisible);
    mouseInteractionButton.setVisible(state.frameVisible && state.transparencyEnabled);
    mouseInteractionButton.setEnabled(state.clickThroughAvailable);
    frameButton.setToggleState(!state.requestedFrameVisible, juce::NotificationType::dontSendNotification);
    frameButton.setTooltip(state.requestedFrameVisible ? "Hide Window Frame." : "Show Window Frame.");
    alwaysOnTopButton.setToggleState(state.alwaysOnTop, juce::NotificationType::dontSendNotification);
    alwaysOnTopButton.setTooltip(state.alwaysOnTop ? "Disable Always on Top." : "Enable Always on Top.");
    mouseInteractionButton.setToggleState(state.allMouseEventsPassThrough, juce::NotificationType::dontSendNotification);
    if (!state.clickThroughAvailable) {
        mouseInteractionButton.setTooltip("Click-through is unavailable in fullscreen on this system.");
    } else if (state.allMouseEventsPassThrough) {
        mouseInteractionButton.setTooltip("Keep This Window Interactive.");
    } else {
        mouseInteractionButton.setTooltip(state.paused ? "Let Clicks Pass Through After Resuming."
                                                       : "Let Clicks Pass Through.");
    }
    fullscreenButton.setTooltip(state.fullScreen ? "Exit Fullscreen." : "Enter Fullscreen.");
    clickThroughHintVisible = state.clickThroughHintVisible;
    resized();
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
    const auto placeFromLeft = [&bar](juce::Component& component, int inset) {
        component.setBounds(component.isVisible() ? bar.removeFromLeft(toolbarHeight).reduced(inset)
                                                  : juce::Rectangle<int>());
    };
    const auto placeFromRight = [&bar](juce::Component& component, int inset) {
        component.setBounds(component.isVisible() ? bar.removeFromRight(toolbarHeight).reduced(inset)
                                                  : juce::Rectangle<int>());
    };
#if JUCE_WINDOWS || JUCE_LINUX
    placeFromRight(closeButton, 3);
    placeFromLeft(fullscreenButton, 4);
    placeFromLeft(frameButton, 4);
    placeFromLeft(mouseInteractionButton, 4);
    placeFromLeft(alwaysOnTopButton, 4);
#else
    placeFromLeft(closeButton, 3);
    placeFromRight(fullscreenButton, 4);
    placeFromRight(frameButton, 4);
    placeFromRight(mouseInteractionButton, 4);
    placeFromRight(alwaysOnTopButton, 4);
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
#if JUCE_LINUX
    // JUCE's Linux OpenGL backend calls eglTerminate when any OpenGLContext is
    // detached. The EGL display is process-wide, so destroying this mirror
    // context also invalidates the primary editor's live GPU surfaces. JUCE
    // deliberately keeps OpenGL attached to minimised peers, allowing this
    // window to be restored without rebuilding either GPU context.
    auto* visualiser = dynamic_cast<VisualiserComponent*>(getContentComponent());
    if (visualiser != nullptr) {
        visualiser->setMirrorPresentationActive(false);
    }
    setMinimised(true);
#else
    parent->popout.reset();
#endif
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
#if JUCE_WINDOWS
            scheduleNativeFullScreenBoundsSync();
#endif
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
    presentationRefreshDeadline = juce::Time::getMillisecondCounter() + 1000;
    presentationRefreshActive = true;
    parent->child->requestAlphaMaskRefresh();
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
#if JUCE_WINDOWS
            scheduleNativeFullScreenBoundsSync();
#endif
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
    juce::DocumentWindow::resized();
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
        transparentFullScreenBounds = osci::windowing::getTransparentFullScreenBounds(display->logicalBounds.toNearestInt());
        const juce::ScopedValueSetter<bool> settingBounds(settingTransparentFullScreenBounds, true);
        setBounds(transparentFullScreenBounds);
    }
    fullScreenTransitionPending = true;
}

#if JUCE_WINDOWS
void VisualiserWindow::scheduleNativeFullScreenBoundsSync() {
    const juce::Component::SafePointer<VisualiserWindow> safeWindow(this);
    juce::Timer::callAfterDelay(50, [safeWindow] {
        if (safeWindow != nullptr && safeWindow->fullScreenRequested && safeWindow->isFullScreen()) {
            safeWindow->synchroniseNativeFullScreenBounds();
        }
    });
}

void VisualiserWindow::synchroniseNativeFullScreenBounds() {
    const auto referenceBounds = boundsBeforeFullScreen.isEmpty() ? getBounds() : boundsBeforeFullScreen;
    auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(referenceBounds);
    if (display != nullptr) {
        // Windows may maximise the native peer without updating JUCE's Component bounds for a
        // transparent-capable window with a custom title bar. Keep the component tree and hit
        // testing in sync with the peer; the peer ignores this bounds update while maximised.
        setBounds(display->logicalBounds.toNearestInt());
    }
}
#endif

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
            presentationRefreshDeadline = now + 1000;
            presentationRefreshActive = true;
            parent->child->requestAlphaMaskRefresh();
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
    if (presentationRefreshActive
        && static_cast<std::int32_t>(now - presentationRefreshDeadline) >= 0) {
        presentationRefreshActive = false;
        osci::windowing::configureTransparency(this);
        setAlwaysOnTop(pinned);
    }

    const bool fullPassThroughActive = transparencyEnabled && allMouseEventsPassThrough && !presentationState.paused;
    if (fullPassThroughActive) {
        presentationState.resetAlphaInteraction();
        hasAlphaPassThroughAnchor = false;
    }
    const bool fullScreenActive = fullScreenRequested || nativeFullScreen;
    const auto presentation = presentationState.derive(
        transparencyEnabled,
        pinned,
        fullScreenActive,
        allMouseEventsPassThrough,
        clickThroughHintVisible,
        presentationRefreshActive,
        osci::windowing::supportsClickThroughInTransparentFullScreen());
    const bool framedWindow = presentation.frameVisible && !fullScreenActive;
    updateResizeBorderVisibility(framedWindow);
    if (!windowCornersConfigured || windowCornersRounded != framedWindow) {
        osci::windowing::setRoundedWindowRegion(this, framedWindow ? 11.0f : 0.0f);
        windowCornersConfigured = true;
        windowCornersRounded = framedWindow;
    }
    if (parent != nullptr && parent->child != nullptr) {
        if (!hasAppliedPresentation || appliedPresentation != presentation) {
            parent->child->setAlphaMaskCaptureEnabled(presentation.alphaCaptureRequired);
            parent->child->setPopoutPresentationOverlay(presentation);
            appliedPresentation = presentation;
            hasAppliedPresentation = true;
        }
    }
    if (presentation.interactionMode == PopoutInteractionMode::interactive) {
        applyNativeInteraction(false);
    } else if (presentation.interactionMode == PopoutInteractionMode::passAll) {
        applyNativeInteraction(true);
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
    if (resizeBorderVisibilityInitialised && resizeBorderVisible == visible) {
        return;
    }
    resizeBorderVisibilityInitialised = true;
    resizeBorderVisible = visible;
    for (auto* component : getChildren()) {
        auto* resizeBorder = dynamic_cast<juce::ResizableBorderComponent*>(component);
        if (resizeBorder != nullptr) {
            resizeBorder->setAlpha(0.0f);
            resizeBorder->setVisible(visible);
        }
    }
}
#endif
