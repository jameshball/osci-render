#include "TransparentWindow.h"

#if !JUCE_MAC && !JUCE_WINDOWS
namespace osci::windowing {

bool isTransparencySupported() {
    return false;
}

void configureTransparency(juce::Component*) {}
void setIgnoresMouseEvents(juce::Component*, bool) {}
void setRoundedWindowRegion(juce::Component*, float) {}
bool isRecoveryModifierDown() { return false; }
juce::String getRecoveryModifierName() { return {}; }
float getInteractiveAlphaFloor() { return 0.0f; }
void configureOpenGLSurface(void*) {}

} // namespace osci::windowing
#endif
