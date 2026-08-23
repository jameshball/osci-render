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

VisualiserPresentationView::VisualiserPresentationView(VisualiserComponent& owner)
    : OpenGLTextureView(owner.getFrameMirror()), owner(owner) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setCheckerColours(osci::Colours::surface(), osci::Colours::darker());
    const auto themeBase = osci::Colours::surfaceSunken();
    setOpaqueBackground(themeBase.interpolatedWith(osci::Colours::shadow(), osci::Theme::isDark() ? 0.86f : 0.38f));
    setContextCreatedCallback([](void* context) { TransparentWindow::configureOpenGLSurface(context); });
}

void VisualiserPresentationView::setPaused(bool shouldBePaused) {
    paused = shouldBePaused;
    repaint();
}

void VisualiserPresentationView::paint(juce::Graphics& g) {
    if (!paused) {
        return;
    }
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRect(getLocalBounds());
    g.setColour(juce::Colours::white);
    g.setFont(30.0f);
    g.drawText("Paused", getLocalBounds(), juce::Justification::centred);
}

void VisualiserPresentationView::mouseDown(const juce::MouseEvent& event) {
    pauseOnMouseUp = event.mods.isLeftButtonDown();
}

void VisualiserPresentationView::mouseDrag(const juce::MouseEvent& event) {
    if (event.getDistanceFromDragStart() > 4) {
        pauseOnMouseUp = false;
    }
}

void VisualiserPresentationView::mouseUp(const juce::MouseEvent& event) {
    const bool shouldTogglePause = pauseOnMouseUp && event.getDistanceFromDragStart() <= 4;
    pauseOnMouseUp = false;
    if (shouldTogglePause) {
        owner.setPaused(!owner.isPaused());
    }
}

VisualiserWindow::VisualiserWindow(juce::String name, VisualiserComponent& owner, osci::SettingsStore& settings)
    : TransparentWindow(std::move(name), loadWindowState(settings)),
      owner(owner),
      settings(settings) {
    auto content = std::make_unique<VisualiserPresentationView>(owner);
    visualiser = content.get();
    setContent(std::move(content));
    addKeyListener(&owner.editor);
    if (getBounds().isEmpty()) {
        centreWithSize(350, 350);
    }
    setTransparencyEnabled(owner.isTransparentBackgroundEnabled());
    setPresentationPaused(owner.isPaused());
}

void VisualiserWindow::showPresentation() {
    setVisible(true);
    setMinimised(false);
    visualiser->setActive(true);
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
    visualiser->setActive(false);
    setMinimised(true);
}

void VisualiserWindow::setPresentationPaused(bool paused) {
    TransparentWindow::setPresentationPaused(paused);
    visualiser->setPaused(paused);
}

void VisualiserWindow::setTransparencyEnabled(bool enabled) {
    TransparentWindow::setTransparencyEnabled(enabled);
    visualiser->setTransparent(enabled);
    visualiser->setNativeTransparencySupported(isTransparencySupported());
}

void VisualiserWindow::setPresentationFadeAlpha(float alpha) {
    visualiser->setFadeAlpha(alpha);
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
    return visualiser->getAlphaMaskSize();
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
    visualiser->executeOnGLThread([](juce::OpenGLContext& context) {
        TransparentWindow::configureOpenGLSurface(context.getRawContext());
    });
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
