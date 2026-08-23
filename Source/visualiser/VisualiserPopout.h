#pragma once

#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>

#include "PopoutPresentationState.h"

class VisualiserComponent;

#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
class PopoutToolbar : public juce::Component {
public:
    PopoutToolbar();

    std::function<void()> onClose;
    std::function<void()> onFullScreen;
    std::function<void()> onToggleFrame;
    std::function<void()> onToggleAlwaysOnTop;
    std::function<void()> onToggleMouseInteraction;

    void setState(const PopoutPresentation& state);
    bool hitTest(int x, int y) override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    static constexpr int toolbarHeight = 24;

    osci::CloseButton closeButton;
    osci::SvgButton fullscreenButton { "fullscreen", BinaryData::fullscreen_svg, juce::Colours::white };
    osci::SvgButton frameButton { "popoutFrame", BinaryData::eye_svg, juce::Colours::white, juce::Colours::white,
                                  nullptr, BinaryData::eyeoff_svg };
    osci::SvgButton alwaysOnTopButton { "alwaysOnTop", BinaryData::pushpin_svg, juce::Colours::white, juce::Colours::green };
    osci::SvgButton mouseInteractionButton { "mouseInteraction", BinaryData::mouse_svg, juce::Colours::white, juce::Colours::red };
    juce::ComponentDragger dragger;
    PopoutPresentation presentation;
    bool hasPresentation = false;
    bool frameVisible = true;
    bool fullScreen = false;
    bool clickThroughHintVisible = false;
};
#endif

class VisualiserWindow : public juce::DocumentWindow
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    , private juce::Timer
#endif
{
public:
    VisualiserWindow(juce::String name, VisualiserComponent* parent, bool pinned);
    ~VisualiserWindow() override;

    bool keyPressed(const juce::KeyPress& key) override;
    void closeButtonPressed() override;
    void toggleFullScreen();

    bool getIsFullScreen() const { return fullScreenRequested; }
    bool isPinned() const { return pinned; }
    void setPinned(bool shouldBePinned);
    void saveWindowState();

#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    bool getAllMouseEventsPassThrough() const { return allMouseEventsPassThrough; }
    void setRequestedFrameVisible(bool visible);
    void setAllMouseEventsPassThrough(bool shouldPassThrough, bool showHint = true);
    bool isFrameRequestedVisible() const { return presentationState.requestedFrameVisible; }
    void refreshNativePresentation();
    void transparencyModeChanged();
    void pauseStateChanged();
    void resized() override;
#endif

private:
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    void timerCallback() override;
    void updatePresentationState();
    void applyNativeInteraction(bool ignoresMouseEvents);
    void updateResizeBorderVisibility(bool visible);
    void leaveTransparentFullScreenAfterResize();
    void enterTransparentFullScreen();
#if JUCE_WINDOWS
    void scheduleNativeFullScreenBoundsSync();
    void synchroniseNativeFullScreenBounds();
#endif
#endif

    VisualiserComponent* parent;
    bool fullScreenRequested = false;
    bool fullScreenTransitionPending = false;
#if JUCE_WINDOWS || JUCE_MAC
    juce::Rectangle<int> boundsBeforeFullScreen;
    juce::Rectangle<int> transparentFullScreenBounds;
    bool transparentFullScreen = false;
    bool settingTransparentFullScreenBounds = false;
    bool reenterFullScreenAfterTransition = false;
#endif
    bool pinned = true;
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    PopoutPresentationState presentationState;
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
    PopoutPresentation appliedPresentation;
    bool hasAppliedPresentation = false;
#endif
};
