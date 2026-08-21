#include "TransparentWindow.h"

#if !JUCE_MAC && !JUCE_WINDOWS
namespace osci::windowing {

bool isTransparencySupported() {
    return false;
}

void configureTransparency(juce::Component*) {}
void setIgnoresMouseEvents(juce::Component*, bool) {}

void toggleWindowMaximised(juce::Component*) {}
void setRoundedWindowRegion(juce::Component*, float) {}
float getInteractiveAlphaFloor() { return 0.0f; }
void configureOpenGLSurface(void*) {}

} // namespace osci::windowing
#endif
