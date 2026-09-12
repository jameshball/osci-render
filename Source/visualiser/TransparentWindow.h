#pragma once

#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>

#include "TransparentWindowInteraction.h"

struct TransparentWindowState {
    juce::Rectangle<int> normalBounds;
    bool fullScreen = false;
    bool frameVisible = true;
    bool alwaysOnTop = true;
    bool mouseEventsPassThrough = false;
};

struct TransparentWindowToolbarState {
    bool frameVisible = true;
    bool frameRequestedVisible = true;
    bool alwaysOnTop = true;
    bool fullScreen = false;
    bool transparencyEnabled = false;
    bool passThroughRequested = false;
    bool passThroughAvailable = false;
    bool paused = false;
    bool clickThroughHintVisible = false;

    bool operator==(const TransparentWindowToolbarState&) const = default;
};

class TransparentWindowToolbar final : public juce::Component {
public:
    TransparentWindowToolbar();

    std::function<void()> onClose;
    std::function<void()> onFullScreen;
    std::function<void()> onToggleFrame;
    std::function<void()> onToggleAlwaysOnTop;
    std::function<void()> onToggleMouseInteraction;

    void setState(const TransparentWindowToolbarState& state);
    bool hitTest(int x, int y) override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

private:
    static constexpr int toolbarHeight = 24;

    osci::CloseButton closeButton;
    osci::SvgButton fullscreenButton { "fullscreen", BinaryData::fullscreen_svg, juce::Colours::white };
    osci::SvgButton frameButton { "windowFrame", BinaryData::eye_svg, juce::Colours::white, juce::Colours::white,
                                  nullptr, BinaryData::eyeoff_svg };
    osci::SvgButton alwaysOnTopButton { "alwaysOnTop", BinaryData::pushpin_svg, juce::Colours::white, juce::Colours::green };
    osci::SvgButton mouseInteractionButton { "mouseInteraction", BinaryData::mouse_svg, juce::Colours::white, juce::Colours::red };
    juce::ComponentDragger dragger;
    TransparentWindowToolbarState state;
    bool hasState = false;
};

class TransparentWindow : public juce::DocumentWindow,
                          private juce::Timer {
public:
    static constexpr float cornerRadius = 11.0f;

    TransparentWindow(juce::String name, TransparentWindowState initialState);
    ~TransparentWindow() override;

    static bool isTransparencySupported();
    static void configureOpenGLSurface(void* rawGLContext);

    void setContent(std::unique_ptr<juce::Component> content);
    void setTransparencyEnabled(bool enabled);
    void setPresentationPaused(bool paused);
    void setFrameVisible(bool visible);
    void setMouseEventsPassThrough(bool shouldPassThrough, bool showHint = true);
    void setPinned(bool shouldBePinned);
    void toggleFullScreen();
    void restoreSavedFullScreen();
    void refreshPresentationSurface();
    void saveWindowState();
#if JUCE_MAC
    bool deferCloseUntilFullScreenExit();
    void cancelDeferredClose() { closeAfterFullScreenExit = false; }
#endif

    bool isFullScreenRequested() const { return fullScreenRequested; }
    bool isFrameVisibleRequested() const { return frameRequestedVisible; }
    bool areMouseEventsPassedThrough() const { return allMouseEventsPassThrough; }
    bool isPinned() const { return pinned; }
    TransparentWindowState getWindowState() const;

    bool keyPressed(const juce::KeyPress& key) override;
    void closeButtonPressed() override;
    void resized() override;
    void visibilityChanged() override;

protected:
    virtual void closeRequested();
    virtual void stateChanged(const TransparentWindowState& state);
    virtual juce::Component* getAlphaHitTestComponent();
    virtual juce::Point<int> getAlphaMaskSize() const;
    virtual std::uint64_t getAlphaMaskGeneration() const;
    virtual bool alphaMaskHasAlphaNear(juce::Point<float> point, juce::Point<float> radius,
                                       std::uint8_t threshold) const;
    virtual void setAlphaMaskCaptureEnabled(bool enabled);
    virtual void requestAlphaMaskRefresh();
    virtual void refreshOpenGLSurfaceTransparency();

private:
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void updatePresentation();
    void applyNativeInteraction(bool ignoresMouseEvents);
    void updateResizeBorderVisibility(bool visible);
    void leaveTransparentFullScreenAfterResize();
    void enterTransparentFullScreen();
#if JUCE_WINDOWS
    void scheduleNativeFullScreenBoundsSync();
    void synchroniseNativeFullScreenBounds();
#endif

    static bool supportsClickThroughInTransparentFullScreen();
    static juce::Rectangle<int> getTransparentFullScreenBounds(juce::Rectangle<int> displayBounds);
    void configureNativeTransparency();
    void applyAlwaysOnTop();
    void setNativeIgnoresMouseEvents(bool ignoresMouseEvents);
    bool isNativeMouseInteractionStateApplied(bool ignoresMouseEvents) const;
    void setMovesToActiveSpace(bool shouldMove);
    void setNativeRoundedWindowRegion(float cornerRadius);
#if JUCE_LINUX
    bool isNativeFullScreenStateActive() const;
    void setNativeBounds(juce::Rectangle<int> bounds);
#elif JUCE_MAC
    void observeNativeFullScreenExit();
    void removeFullScreenExitObserver();
#endif

    std::unique_ptr<TransparentWindowToolbar> toolbar;
    juce::Component* dragSurface = nullptr;
    juce::ComponentDragger contentDragger;
    bool transparencyEnabled = false;
    bool presentationPaused = false;
    bool restoreFullScreen = false;
    bool fullScreenRequested = false;
    bool fullScreenTransitionPending = false;
    juce::Rectangle<int> boundsBeforeFullScreen;
    juce::Rectangle<int> transparentFullScreenBounds;
    bool transparentFullScreen = false;
    bool settingTransparentFullScreenBounds = false;
    bool reenterFullScreenAfterTransition = false;
#if JUCE_LINUX
    std::uint32_t transparentFullScreenTransitionTime = 0;
#elif JUCE_MAC
    void* fullScreenExitObserver = nullptr;
    bool closeAfterFullScreenExit = false;
#endif
    bool pinned = true;
    bool frameRequestedVisible = true;
    AlphaInteractionHold alphaInteractionHold;
    bool nativeIgnoresMouseEvents = false;
    bool allMouseEventsPassThrough = false;
    bool clickThroughHintVisible = false;
    bool presentationRefreshActive = false;
    bool windowCornersConfigured = false;
    bool windowCornersRounded = false;
    bool hasAlphaPassThroughAnchor = false;
    juce::Point<int> alphaPassThroughAnchor;
    std::uint64_t presentationRefreshGeneration = 0;
    juce::uint32 presentationRefreshDeadline = 0;
    juce::uint32 clickThroughHintEndTime = 0;
    bool resizeBorderVisible = false;
    bool resizeBorderVisibilityInitialised = false;
    TransparentWindowInteractionPolicy appliedInteractionPolicy;
    bool hasAppliedInteractionPolicy = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransparentWindow)
};
