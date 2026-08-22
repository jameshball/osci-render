#include "TransparentWindow.h"

#if !JUCE_MAC && !JUCE_WINDOWS
namespace osci::windowing {

bool isTransparencySupported() {
    return false;
}

bool supportsClickThroughInTransparentFullScreen() { return false; }
juce::Rectangle<int> getTransparentFullScreenBounds(juce::Rectangle<int> displayBounds) { return displayBounds; }

void configureTransparency(juce::Component*) {}
void setIgnoresMouseEvents(juce::Component*, bool) {}
bool isMouseInteractionStateApplied(juce::Component*, bool) { return true; }

void setMovesToActiveSpace(juce::Component*, bool) {}
void setRoundedWindowRegion(juce::Component*, float) {}
void configureOpenGLSurface(void*) {}

} // namespace osci::windowing
#endif
