#include "TransparentWindow.h"

#include "../LookAndFeel.h"

#include <cstdint>

TransparentWindowToolbar::TransparentWindowToolbar() {
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

void TransparentWindowToolbar::setState(const TransparentWindowToolbarState& newState) {
    if (hasState && state == newState) {
        return;
    }
    state = newState;
    hasState = true;
    closeButton.setVisible(state.frameVisible);
    fullscreenButton.setVisible(state.frameVisible);
    frameButton.setVisible(state.frameVisible);
    alwaysOnTopButton.setVisible(state.frameVisible);
    mouseInteractionButton.setVisible(state.frameVisible && state.transparencyEnabled);
    mouseInteractionButton.setEnabled(state.passThroughAvailable);
    frameButton.setToggleState(!state.frameRequestedVisible, juce::dontSendNotification);
    frameButton.setTooltip(state.frameRequestedVisible ? "Hide Window Frame." : "Show Window Frame.");
    alwaysOnTopButton.setToggleState(state.alwaysOnTop, juce::dontSendNotification);
    alwaysOnTopButton.setTooltip(state.alwaysOnTop ? "Disable Always on Top." : "Enable Always on Top.");
    mouseInteractionButton.setToggleState(state.passThroughRequested, juce::dontSendNotification);
    if (!state.passThroughAvailable) {
        mouseInteractionButton.setTooltip("Click-through is unavailable in fullscreen on this system.");
    } else if (state.passThroughRequested) {
        mouseInteractionButton.setTooltip("Keep This Window Interactive.");
    } else {
        mouseInteractionButton.setTooltip(state.paused ? "Let Clicks Pass Through After Resuming."
                                                       : "Let Clicks Pass Through.");
    }
    fullscreenButton.setTooltip(state.fullScreen ? "Exit Fullscreen." : "Enter Fullscreen.");
    resized();
    repaint();
}

bool TransparentWindowToolbar::hitTest(int x, int y) {
    return state.frameVisible && getLocalBounds().removeFromTop(toolbarHeight).contains(x, y);
}

void TransparentWindowToolbar::paint(juce::Graphics& g) {
    if (state.frameVisible) {
        auto bounds = getLocalBounds();
        const juce::Graphics::ScopedSaveState saveState(g);
        if (!state.fullScreen) {
            juce::Path windowShape;
            windowShape.addRoundedRectangle(bounds.toFloat(), TransparentWindow::cornerRadius);
            g.reduceClipRegion(windowShape);
        }
        g.setColour(osci::Colours::veryDark());
        g.fillRect(bounds.removeFromTop(toolbarHeight));
        if (!state.fullScreen) {
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), TransparentWindow::cornerRadius - 0.5f, 1.0f);
        }
    }
    if (state.clickThroughHintVisible) {
        const auto hintWidth = juce::jmax(1, juce::jmin(620, getWidth() - 24));
        const auto hintBounds = getLocalBounds().withSizeKeepingCentre(hintWidth, 58).toFloat();
        g.setColour(osci::Colours::veryDark().withAlpha(0.94f));
        g.fillRoundedRectangle(hintBounds, 8.0f);
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawFittedText("Clicks now pass through this window. Pause the main visualiser to bring the controls back.",
                         hintBounds.reduced(14.0f).toNearestInt(), juce::Justification::centred, 2);
    }
}

void TransparentWindowToolbar::resized() {
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

void TransparentWindowToolbar::mouseDown(const juce::MouseEvent& event) {
    if (!event.mods.isLeftButtonDown() || state.fullScreen) {
        return;
    }
    dragger.startDraggingComponent(getTopLevelComponent(), event.getEventRelativeTo(getTopLevelComponent()));
}

void TransparentWindowToolbar::mouseDoubleClick(const juce::MouseEvent& event) {
#if JUCE_WINDOWS
    if (event.mods.isLeftButtonDown() && onFullScreen != nullptr) {
        onFullScreen();
    }
#else
    juce::ignoreUnused(event);
#endif
}

void TransparentWindowToolbar::mouseDrag(const juce::MouseEvent& event) {
    if (!state.fullScreen) {
        dragger.dragComponent(getTopLevelComponent(), event.getEventRelativeTo(getTopLevelComponent()), nullptr);
    }
}

TransparentWindow::TransparentWindow(juce::String name, TransparentWindowState initialState)
    : juce::DocumentWindow(name,
                           isTransparencySupported() ? juce::Colours::transparentBlack : juce::Colours::black,
                           isTransparencySupported() ? 0 : juce::DocumentWindow::TitleBarButtons::allButtons),
      restoreFullScreen(initialState.fullScreen),
      pinned(initialState.alwaysOnTop),
      frameRequestedVisible(initialState.frameVisible),
      allMouseEventsPassThrough(initialState.mouseEventsPassThrough) {
    setUsingNativeTitleBar(!isTransparencySupported());
    setResizable(true, false);
    applyAlwaysOnTop();
    if (!initialState.normalBounds.isEmpty()) {
        setBounds(initialState.normalBounds);
    }
    if (!isTransparencySupported()) {
        return;
    }

    setTitleBarHeight(0);
    setOpaque(false);
#if JUCE_WINDOWS
    // JUCE's non-native shadow is made from separate windows, which cannot follow click-through.
    setDropShadowEnabled(false);
#endif
    toolbar = std::make_unique<TransparentWindowToolbar>();
    toolbar->onClose = [this] { closeButtonPressed(); };
    toolbar->onFullScreen = [this] { toggleFullScreen(); };
    toolbar->onToggleFrame = [this] { setFrameVisible(!frameRequestedVisible); };
    toolbar->onToggleAlwaysOnTop = [this] { setPinned(!pinned); };
    toolbar->onToggleMouseInteraction = [this] {
        setMouseEventsPassThrough(!allMouseEventsPassThrough);
    };
    addAndMakeVisible(toolbar.get());
}

TransparentWindow::~TransparentWindow() {
    stopTimer();
    if (dragSurface != nullptr) {
        dragSurface->removeMouseListener(this);
    }
    if (isTransparencySupported()) {
        setNativeIgnoresMouseEvents(false);
    }
}

void TransparentWindow::setContent(std::unique_ptr<juce::Component> content) {
    if (dragSurface != nullptr) {
        dragSurface->removeMouseListener(this);
    }
    dragSurface = content.get();
    if (toolbar != nullptr) {
        // Composite controls with the content, above any native OpenGL surface.
        // Reparent before replacing the old content so the window retains ownership.
        auto* overlayParent = dragSurface != nullptr ? dragSurface : this;
        overlayParent->addChildComponent(*toolbar);
    }
    setContentOwned(content.release(), false);
    if (dragSurface != nullptr) {
        dragSurface->addMouseListener(this, false);
    }
    if (toolbar != nullptr) {
        toolbar->toFront(false);
    }
    resized();
}

void TransparentWindow::setTransparencyEnabled(bool enabled) {
    enabled = enabled && isTransparencySupported();
    if (transparencyEnabled == enabled) {
        return;
    }
    transparencyEnabled = enabled;
    if (fullScreenRequested) {
        if (transparencyEnabled && !transparentFullScreen) {
            fullScreenTransitionPending = true;
            reenterFullScreenAfterTransition = true;
            transparentFullScreenTransitionTime = juce::Time::getMillisecondCounter() + 250;
            juce::ResizableWindow::setFullScreen(false);
#if JUCE_LINUX
            configureNativeTransparency();
#endif
        } else if (!transparencyEnabled) {
            reenterFullScreenAfterTransition = false;
            if (transparentFullScreen) {
                transparentFullScreen = false;
                transparentFullScreenBounds = {};
                if (!boundsBeforeFullScreen.isEmpty()) {
                    const juce::ScopedValueSetter<bool> settingBounds(settingTransparentFullScreenBounds, true);
                    setBounds(boundsBeforeFullScreen);
                }
                setMovesToActiveSpace(false);
            }
            fullScreenTransitionPending = true;
            juce::ResizableWindow::setFullScreen(true);
#if JUCE_WINDOWS
            scheduleNativeFullScreenBoundsSync();
#endif
        }
        updatePresentation();
        return;
    }
    if (reenterFullScreenAfterTransition) {
        return;
    }
    if (transparencyEnabled) {
        refreshPresentationSurface();
    } else {
        presentationRefreshActive = false;
        updatePresentation();
    }
}

void TransparentWindow::setPresentationPaused(bool paused) {
    if (presentationPaused == paused) {
        return;
    }
    presentationPaused = paused;
    updatePresentation();
}

void TransparentWindow::setFrameVisible(bool visible) {
    if (frameRequestedVisible == visible) {
        return;
    }
    frameRequestedVisible = visible;
    stateChanged(getWindowState());
    updatePresentation();
}

void TransparentWindow::setMouseEventsPassThrough(bool shouldPassThrough, bool showHint) {
    if (allMouseEventsPassThrough == shouldPassThrough && !showHint) {
        updatePresentation();
        return;
    }
    allMouseEventsPassThrough = shouldPassThrough;
    if (shouldPassThrough) {
        clickThroughHintVisible = showHint && transparencyEnabled && !presentationPaused;
        if (clickThroughHintVisible) {
            clickThroughHintEndTime = juce::Time::getMillisecondCounter() + 5000;
        }
    } else {
        clickThroughHintVisible = false;
    }
    stateChanged(getWindowState());
    updatePresentation();
}

void TransparentWindow::setPinned(bool shouldBePinned) {
    if (pinned == shouldBePinned) {
        return;
    }
    pinned = shouldBePinned;
    applyAlwaysOnTop();
#if JUCE_WINDOWS
    if (isTransparencySupported()) {
        refreshPresentationSurface();
    } else {
        updatePresentation();
    }
#else
    updatePresentation();
#endif
    stateChanged(getWindowState());
}

void TransparentWindow::toggleFullScreen() {
    fullScreenRequested = !fullScreenRequested;
    fullScreenTransitionPending = true;
    reenterFullScreenAfterTransition = false;
    applyAlwaysOnTop();
    if (fullScreenRequested) {
        boundsBeforeFullScreen = getBounds();
        transparentFullScreen = transparencyEnabled;
        if (transparentFullScreen) {
            enterTransparentFullScreen();
        } else {
            setMovesToActiveSpace(false);
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
            setMovesToActiveSpace(false);
        } else {
            juce::ResizableWindow::setFullScreen(false);
        }
        transparentFullScreenBounds = {};
    }
    stateChanged(getWindowState());
    updatePresentation();
}

void TransparentWindow::restoreSavedFullScreen() {
    if (!restoreFullScreen) {
        return;
    }
    restoreFullScreen = false;
    toggleFullScreen();
}

void TransparentWindow::refreshPresentationSurface() {
    if (!isTransparencySupported()) {
        return;
    }
#if JUCE_LINUX
    configureNativeTransparency();
#endif
    presentationRefreshGeneration = getAlphaMaskGeneration();
    presentationRefreshDeadline = juce::Time::getMillisecondCounter() + 1000;
    presentationRefreshActive = true;
    requestAlphaMaskRefresh();
    updatePresentation();
}

TransparentWindowState TransparentWindow::getWindowState() const {
    auto normalBounds = getBounds();
    if (fullScreenRequested && !boundsBeforeFullScreen.isEmpty()) {
        normalBounds = boundsBeforeFullScreen;
    }
    return {
        .normalBounds = normalBounds,
        .fullScreen = fullScreenRequested,
        .frameVisible = frameRequestedVisible,
        .alwaysOnTop = pinned,
        .mouseEventsPassThrough = allMouseEventsPassThrough,
    };
}

void TransparentWindow::saveWindowState() {
    stateChanged(getWindowState());
}

bool TransparentWindow::keyPressed(const juce::KeyPress& key) {
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

void TransparentWindow::closeButtonPressed() {
    closeRequested();
}

void TransparentWindow::visibilityChanged() {
    juce::DocumentWindow::visibilityChanged();
    if (isVisible()) {
        applyAlwaysOnTop();
    }
    if (isVisible() && isTransparencySupported()) {
        windowCornersConfigured = false;
        configureNativeTransparency();
        updatePresentation();
    }
}

void TransparentWindow::resized() {
    juce::DocumentWindow::resized();
    if (!isTransparencySupported()) {
        return;
    }
    if (reenterFullScreenAfterTransition) {
        transparentFullScreenTransitionTime = juce::Time::getMillisecondCounter() + 250;
    }
    if (getContentComponent() != nullptr) {
        getContentComponent()->setBounds(getLocalBounds());
    }
    if (toolbar != nullptr) {
        toolbar->setBounds(getLocalBounds());
        toolbar->toFront(false);
    }
#if JUCE_LINUX
    if (windowCornersRounded) {
        setNativeRoundedWindowRegion(cornerRadius);
    }
#endif
    if (fullScreenRequested && transparentFullScreen && !settingTransparentFullScreenBounds
        && !transparentFullScreenBounds.isEmpty() && getBounds() != transparentFullScreenBounds) {
        if (static_cast<std::int32_t>(juce::Time::getMillisecondCounter() - transparentFullScreenTransitionTime) < 0) {
            transparentFullScreen = false;
            reenterFullScreenAfterTransition = true;
            transparentFullScreenTransitionTime = juce::Time::getMillisecondCounter() + 250;
            updatePresentation();
            return;
        }
        leaveTransparentFullScreenAfterResize();
    }
    const bool nativeFullScreen = juce::ResizableWindow::isFullScreen();
    const bool frameVisible = hasAppliedInteractionPolicy ? appliedInteractionPolicy.frameVisible : frameRequestedVisible;
    updateResizeBorderVisibility(frameVisible && !fullScreenRequested && !nativeFullScreen);
}

void TransparentWindow::closeRequested() {
    setVisible(false);
}

void TransparentWindow::stateChanged(const TransparentWindowState&) {}
juce::Component* TransparentWindow::getAlphaHitTestComponent() { return nullptr; }
juce::Point<int> TransparentWindow::getAlphaMaskSize() const { return {}; }
std::uint64_t TransparentWindow::getAlphaMaskGeneration() const { return 0; }
bool TransparentWindow::alphaMaskHasAlphaNear(juce::Point<float>, juce::Point<float>, std::uint8_t) const { return false; }
void TransparentWindow::setAlphaMaskCaptureEnabled(bool) {}
void TransparentWindow::requestAlphaMaskRefresh() {}
void TransparentWindow::refreshOpenGLSurfaceTransparency() {}

void TransparentWindow::mouseDown(const juce::MouseEvent& event) {
    if (event.originalComponent != dragSurface || !event.mods.isLeftButtonDown() || fullScreenRequested) {
        return;
    }
    contentDragger.startDraggingComponent(this, event.getEventRelativeTo(this));
}

void TransparentWindow::mouseDrag(const juce::MouseEvent& event) {
    if (event.originalComponent == dragSurface && event.mods.isLeftButtonDown()
        && event.getDistanceFromDragStart() > 4 && !fullScreenRequested) {
        contentDragger.dragComponent(this, event.getEventRelativeTo(this), nullptr);
    }
}

void TransparentWindow::leaveTransparentFullScreenAfterResize() {
    fullScreenRequested = false;
    fullScreenTransitionPending = false;
    transparentFullScreen = false;
    transparentFullScreenBounds = {};
    reenterFullScreenAfterTransition = false;
    setMovesToActiveSpace(false);
    applyAlwaysOnTop();
    stateChanged(getWindowState());
    updatePresentation();
}

void TransparentWindow::enterTransparentFullScreen() {
    transparentFullScreen = true;
    setMovesToActiveSpace(true);
    const auto displayBounds = boundsBeforeFullScreen.isEmpty() ? getBounds() : boundsBeforeFullScreen;
    auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(displayBounds);
    if (display != nullptr) {
#if JUCE_LINUX
        // A normal X11 window is constrained to the desktop work area. Matching
        // it avoids mistaking the window manager's adjustment for a user resize.
        transparentFullScreenBounds = getTransparentFullScreenBounds(display->userBounds.toNearestInt());
#else
        transparentFullScreenBounds = getTransparentFullScreenBounds(display->logicalBounds.toNearestInt());
#endif
        const juce::ScopedValueSetter<bool> settingBounds(settingTransparentFullScreenBounds, true);
        transparentFullScreenTransitionTime = juce::Time::getMillisecondCounter() + 250;
        setBounds(transparentFullScreenBounds);
#if JUCE_LINUX
        // Stale ConfigureNotify events can leave JUCE's component bounds ahead
        // of the native X11 peer after leaving native fullscreen.
        setNativeBounds(transparentFullScreenBounds);
#endif
    }
    fullScreenTransitionPending = true;
}

#if JUCE_WINDOWS
void TransparentWindow::scheduleNativeFullScreenBoundsSync() {
    const juce::Component::SafePointer<TransparentWindow> safeWindow(this);
    juce::Timer::callAfterDelay(50, [safeWindow] {
        if (safeWindow != nullptr && safeWindow->fullScreenRequested && safeWindow->isFullScreen()) {
            safeWindow->synchroniseNativeFullScreenBounds();
        }
    });
}

void TransparentWindow::synchroniseNativeFullScreenBounds() {
    const auto referenceBounds = boundsBeforeFullScreen.isEmpty() ? getBounds() : boundsBeforeFullScreen;
    auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(referenceBounds);
    if (display != nullptr) {
        setBounds(display->logicalBounds.toNearestInt());
    }
}
#endif

void TransparentWindow::timerCallback() {
    updatePresentation();
}

void TransparentWindow::updatePresentation() {
    if (!isTransparencySupported()) {
        fullScreenTransitionPending = false;
        stopTimer();
        return;
    }
    const auto now = juce::Time::getMillisecondCounter();
    if (presentationPaused) {
        clickThroughHintVisible = false;
    }
    if (clickThroughHintVisible && static_cast<std::int32_t>(now - clickThroughHintEndTime) >= 0) {
        clickThroughHintVisible = false;
    }
    if (!transparencyEnabled) {
        presentationRefreshActive = false;
        clickThroughHintVisible = false;
        alphaInteractionHold.reset();
        hasAlphaPassThroughAnchor = false;
    }

    const bool nativeFullScreen = juce::ResizableWindow::isFullScreen();
    // Native fullscreen flags can clear before the window manager finishes resizing.
    bool canReenterTransparentFullScreen = reenterFullScreenAfterTransition && !nativeFullScreen
                                       && static_cast<std::int32_t>(now - transparentFullScreenTransitionTime) >= 0;
#if JUCE_LINUX
    if (canReenterTransparentFullScreen) {
        canReenterTransparentFullScreen = !isNativeFullScreenStateActive();
    }
#endif
    if (canReenterTransparentFullScreen) {
        reenterFullScreenAfterTransition = false;
        enterTransparentFullScreen();
    }
    const bool fullScreenTransitionComplete = !reenterFullScreenAfterTransition
                                           && (transparentFullScreen || nativeFullScreen == fullScreenRequested);
    if (fullScreenTransitionPending && fullScreenTransitionComplete) {
        fullScreenTransitionPending = false;
        configureNativeTransparency();
        applyAlwaysOnTop();
        refreshOpenGLSurfaceTransparency();
#if JUCE_WINDOWS
        if (getAlphaHitTestComponent() != nullptr) {
            presentationRefreshGeneration = getAlphaMaskGeneration();
            presentationRefreshDeadline = now + 1000;
            presentationRefreshActive = true;
            requestAlphaMaskRefresh();
        }
#endif
        if (!fullScreenRequested) {
            transparentFullScreen = false;
        }
    }

    if (presentationRefreshActive && (getAlphaMaskGeneration() != presentationRefreshGeneration
                                     || static_cast<std::int32_t>(now - presentationRefreshDeadline) >= 0)) {
        presentationRefreshActive = false;
        configureNativeTransparency();
        applyAlwaysOnTop();
    }

    const bool fullPassThroughActive = transparencyEnabled && allMouseEventsPassThrough && !presentationPaused;
    if (fullPassThroughActive) {
        alphaInteractionHold.reset();
        hasAlphaPassThroughAnchor = false;
    }
    const bool fullScreenActive = fullScreenRequested || nativeFullScreen;
    const bool alphaClickThroughAllowed = !fullScreenActive || supportsClickThroughInTransparentFullScreen();
    const TransparentWindowInteractionContext interactionContext {
        .transparencyEnabled = transparencyEnabled,
        .frameRequestedVisible = frameRequestedVisible,
        .paused = presentationPaused,
        .passThroughRequested = allMouseEventsPassThrough,
        .waitingForSurface = presentationRefreshActive,
        .alphaClickThroughAllowed = alphaClickThroughAllowed,
    };
    const auto interactionPolicy = deriveTransparentWindowInteractionPolicy(interactionContext);
    const bool framedWindow = interactionPolicy.frameVisible && !fullScreenActive;
    updateResizeBorderVisibility(framedWindow);
    if (!windowCornersConfigured || windowCornersRounded != framedWindow) {
        setNativeRoundedWindowRegion(framedWindow ? cornerRadius : 0.0f);
        windowCornersConfigured = true;
        windowCornersRounded = framedWindow;
    }
    if (!hasAppliedInteractionPolicy || appliedInteractionPolicy != interactionPolicy) {
        setAlphaMaskCaptureEnabled(interactionPolicy.alphaCaptureRequired);
        appliedInteractionPolicy = interactionPolicy;
        hasAppliedInteractionPolicy = true;
    }
    if (toolbar != nullptr) {
        const TransparentWindowToolbarState toolbarState {
            .frameVisible = interactionPolicy.frameVisible,
            .frameRequestedVisible = frameRequestedVisible,
            .alwaysOnTop = pinned,
            .fullScreen = fullScreenActive,
            .transparencyEnabled = transparencyEnabled,
            .passThroughRequested = transparencyEnabled && allMouseEventsPassThrough,
            .passThroughAvailable = transparencyEnabled && alphaClickThroughAllowed,
            .paused = presentationPaused,
            .clickThroughHintVisible = clickThroughHintVisible,
        };
        toolbar->setState(toolbarState);
        toolbar->setVisible(toolbarState.frameVisible || toolbarState.clickThroughHintVisible);
    }

    if (interactionPolicy.mode == TransparentWindowInteractionMode::interactive) {
        applyNativeInteraction(false);
    } else if (interactionPolicy.mode == TransparentWindowInteractionMode::passAll) {
        applyNativeInteraction(true);
    } else {
        bool alphaHit = false;
        const auto screenCursor = juce::Desktop::getMousePosition();
        auto* alphaComponent = getAlphaHitTestComponent();
        if (alphaComponent != nullptr) {
            const auto frameSize = getAlphaMaskSize();
            const auto cursor = alphaComponent->getLocalPoint(nullptr, screenCursor);
            const float scale = getPeer() != nullptr ? static_cast<float>(getPeer()->getPlatformScaleFactor()) : 1.0f;
            const auto query = makeTransparentWindowAlphaQuery(alphaComponent->getLocalBounds(), frameSize, cursor,
                                                                8.0f / juce::jmax(1.0f, scale));
            if (query.valid) {
                alphaHit = alphaMaskHasAlphaNear(query.normalisedPoint, query.normalisedRadius,
                                                 transparentWindowAlphaThreshold);
            }
        }
        if (nativeIgnoresMouseEvents && !alphaHit) {
            alphaPassThroughAnchor = screenCursor;
            hasAlphaPassThroughAnchor = true;
        }
        const bool movedBeyondPadding = !hasAlphaPassThroughAnchor
                                     || alphaPassThroughAnchor.getDistanceFrom(screenCursor) > 8.0;
        alphaInteractionHold.update(alphaHit, nativeIgnoresMouseEvents, movedBeyondPadding, now);
        applyNativeInteraction(!alphaInteractionHold.isActive(now));
    }

    const bool needsTimer = interactionPolicy.mode == TransparentWindowInteractionMode::alphaAware
                         || fullScreenTransitionPending
                         || reenterFullScreenAfterTransition
                         || (transparencyEnabled && presentationRefreshActive)
                         || (transparencyEnabled && clickThroughHintVisible);
    if (needsTimer && !isTimerRunning()) {
        startTimerHz(60);
    } else if (!needsTimer) {
        stopTimer();
    }
}

void TransparentWindow::applyNativeInteraction(bool ignoresMouseEvents) {
    if (nativeIgnoresMouseEvents == ignoresMouseEvents
        && isNativeMouseInteractionStateApplied(ignoresMouseEvents)) {
        return;
    }
    if (ignoresMouseEvents) {
        alphaPassThroughAnchor = juce::Desktop::getMousePosition();
        hasAlphaPassThroughAnchor = true;
    } else {
        hasAlphaPassThroughAnchor = false;
    }
    nativeIgnoresMouseEvents = ignoresMouseEvents;
    setNativeIgnoresMouseEvents(ignoresMouseEvents);
}

#if !JUCE_LINUX
void TransparentWindow::applyAlwaysOnTop() {
    setAlwaysOnTop(pinned);
}
#endif

void TransparentWindow::updateResizeBorderVisibility(bool visible) {
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
