#pragma once

#include <JuceHeader.h>

namespace osci::windowing {

bool isTransparencySupported();

// Must be called after the window is visible and has a native peer.
void configureTransparency(juce::Component* topLevelWindow);

// Must be called on the message thread after the window has a native peer.
void setIgnoresMouseEvents(juce::Component* topLevelWindow, bool ignoresMouseEvents);

bool isRecoveryModifierDown();
juce::String getRecoveryModifierName();
float getInteractiveAlphaFloor();

// Must be called on the OpenGL thread.
void configureOpenGLSurface(void* rawGLContext);

} // namespace osci::windowing
