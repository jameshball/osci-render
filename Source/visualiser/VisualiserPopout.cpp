#include "VisualiserPopout.h"

#include "../CommonPluginEditor.h"
#include "VisualiserComponent.h"

namespace {

constexpr auto alwaysOnTopKey = "popoutAlwaysOnTop";
constexpr auto frameVisibleKey = "popoutFrameVisible";
constexpr auto clicksPassThroughKey = "popoutClicksPassThrough";
constexpr auto windowBoundsKey = "popoutWindowBounds";
constexpr auto fullScreenKey = "popoutFullScreen";

}

VisualiserWindow::VisualiserWindow(juce::String name, VisualiserComponent& owner,
                                   std::unique_ptr<VisualiserComponent> content, osci::SettingsStore& settings)
    : TransparentWindow(std::move(name), loadWindowState(settings)),
      owner(owner),
      visualiser(content.get()),
      settings(settings) {
    setContent(std::move(content));
    addKeyListener(&owner.editor);
    visualiser->hideButtonRow = true;
    visualiser->resized();
    if (getBounds().isEmpty()) {
        centreWithSize(350, 350);
    }
    setTransparencyEnabled(owner.isTransparentBackgroundEnabled());
    setPresentationPaused(owner.isPaused());
}

void VisualiserWindow::showPresentation() {
    setVisible(true);
    setMinimised(false);
    visualiser->setMirrorSource(&owner);
    visualiser->setMirrorPresentationActive(true);
#if JUCE_WINDOWS
    if (isTransparencySupported()) {
        refreshPresentationSurface();
    }
#endif
    restoreSavedFullScreen();
    visualiser->repaint();
    toFront(true);
}

void VisualiserWindow::suspendPresentation() {
    visualiser->setMirrorPresentationActive(false);
    setMinimised(true);
}

bool VisualiserWindow::getAlwaysOnTopPreference(const osci::SettingsStore& settings) {
    return settings.getBool(alwaysOnTopKey, true);
}

void VisualiserWindow::setAlwaysOnTopPreference(osci::SettingsStore& settings, bool alwaysOnTop) {
    settings.set(alwaysOnTopKey, alwaysOnTop);
    settings.save();
}

void VisualiserWindow::closeRequested() {
    owner.closePopout();
}

void VisualiserWindow::stateChanged(const TransparentWindowState& state) {
    if (!state.normalBounds.isEmpty()) {
        settings.set(windowBoundsKey, state.normalBounds.toString());
    }
    settings.set(fullScreenKey, state.fullScreen);
    settings.set(frameVisibleKey, state.frameVisible);
    settings.set(alwaysOnTopKey, state.alwaysOnTop);
    settings.set(clicksPassThroughKey, state.mouseEventsPassThrough);
    settings.save();
}

juce::Component* VisualiserWindow::getAlphaHitTestComponent() {
    return visualiser;
}

juce::Point<int> VisualiserWindow::getAlphaMaskSize() const {
    const auto size = visualiser->getAlphaMaskSize();
    return { size.width, size.height };
}

std::uint64_t VisualiserWindow::getAlphaMaskGeneration() const {
    return visualiser->getAlphaMaskGeneration();
}

bool VisualiserWindow::alphaMaskHasAlphaNear(juce::Point<float> point, juce::Point<float> radius,
                                              std::uint8_t threshold) const {
    return visualiser->alphaMaskHasAlphaNear(point, radius, threshold);
}

void VisualiserWindow::setAlphaMaskCaptureEnabled(bool enabled) {
    visualiser->setAlphaMaskCaptureEnabled(enabled);
}

void VisualiserWindow::requestAlphaMaskRefresh() {
    visualiser->requestAlphaMaskRefresh();
}

void VisualiserWindow::refreshOpenGLSurfaceTransparency() {
#if OSCI_PREMIUM && (JUCE_MAC || JUCE_WINDOWS)
    visualiser->refreshOpenGLSurfaceTransparency();
#endif
}

TransparentWindowState VisualiserWindow::loadWindowState(const osci::SettingsStore& settings) {
    return {
        .normalBounds = constrainSavedBounds(
            juce::Rectangle<int>::fromString(settings.getString(windowBoundsKey))),
        .fullScreen = settings.getBool(fullScreenKey, false),
        .frameVisible = settings.getBool(frameVisibleKey, true),
        .alwaysOnTop = settings.getBool(alwaysOnTopKey, true),
        .mouseEventsPassThrough = settings.getBool(clicksPassThroughKey, false),
    };
}

juce::Rectangle<int> VisualiserWindow::constrainSavedBounds(juce::Rectangle<int> bounds) {
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

    const auto available = display->userBounds.toNearestInt();
    bounds.setSize(juce::jmin(bounds.getWidth(), available.getWidth()),
                   juce::jmin(bounds.getHeight(), available.getHeight()));
    return bounds.constrainedWithin(available);
}
