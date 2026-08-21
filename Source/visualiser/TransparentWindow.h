#pragma once

#include <JuceHeader.h>

namespace osci::windowing {

bool isTransparencySupported();

// Must be called after the window is visible and has a native peer.
void configureTransparency(juce::Component* topLevelWindow);

// Must be called on the message thread after the window has a native peer.
void setIgnoresMouseEvents(juce::Component* topLevelWindow, bool ignoresMouseEvents);

// Updates the native window shape after a borderless popout is created or resized.
void setRoundedWindowRegion(juce::Component* topLevelWindow, float cornerRadius);

bool isRecoveryModifierDown();
juce::String getRecoveryModifierName();
float getInteractiveAlphaFloor();

// Must be called on the OpenGL thread.
void configureOpenGLSurface(void* rawGLContext);

} // namespace osci::windowing
