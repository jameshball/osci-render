#pragma once

#include <JuceHeader.h>

namespace osci::windowing {

bool isTransparencySupported();

// Must be called after the window is visible and has a native peer.
void configureTransparency(juce::Component* topLevelWindow);

// Must be called on the message thread after the window has a native peer.
void setIgnoresMouseEvents(juce::Component* topLevelWindow, bool ignoresMouseEvents);

// Starts the platform's normal window move interaction, including native snap behaviour.
void beginWindowMove(juce::Component* topLevelWindow);

// Uses the platform's normal maximise/restore behaviour.
void toggleWindowMaximised(juce::Component* topLevelWindow);

// Updates the native window shape after a borderless popout is created or resized.
void setRoundedWindowRegion(juce::Component* topLevelWindow, float cornerRadius);

float getInteractiveAlphaFloor();

// Must be called on the OpenGL thread.
void configureOpenGLSurface(void* rawGLContext);

} // namespace osci::windowing
