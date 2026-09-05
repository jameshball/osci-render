#include "TransparentWindow.h"

#if !JUCE_MAC && !JUCE_WINDOWS && !JUCE_LINUX
bool TransparentWindow::isTransparencySupported() {
    return false;
}

bool TransparentWindow::supportsClickThroughInTransparentFullScreen() { return false; }
juce::Rectangle<int> TransparentWindow::getTransparentFullScreenBounds(juce::Rectangle<int> displayBounds) { return displayBounds; }

void TransparentWindow::configureNativeTransparency() {}
void TransparentWindow::setNativeIgnoresMouseEvents(bool) {}
bool TransparentWindow::isNativeMouseInteractionStateApplied(bool) const { return true; }

void TransparentWindow::setMovesToActiveSpace(bool) {}
void TransparentWindow::setNativeRoundedWindowRegion(float) {}
void TransparentWindow::configureOpenGLSurface(void*) {}

#endif
