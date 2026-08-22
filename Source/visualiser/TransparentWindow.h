#pragma once

#include <JuceHeader.h>

namespace osci::windowing {

bool isTransparencySupported();

// Must be called after the window is visible and has a native peer.
void configureTransparency(juce::Component* topLevelWindow);

// Must be called on the message thread after the window has a native peer.
void setIgnoresMouseEvents(juce::Component* topLevelWindow, bool ignoresMouseEvents);

// Checks the native window rather than relying on cached presentation state.
bool isMouseInteractionStateApplied(juce::Component* topLevelWindow, bool ignoresMouseEvents);

// Uses the platform's normal maximise/restore behaviour.
void toggleWindowMaximised(juce::Component* topLevelWindow);

// Controls whether activating the window moves it to the currently active desktop space.
void setMovesToActiveSpace(juce::Component* topLevelWindow, bool shouldMove);

// Updates the native window shape after a borderless popout is created or resized.
void setRoundedWindowRegion(juce::Component* topLevelWindow, float cornerRadius);

float getInteractiveAlphaFloor();

// Must be called on the OpenGL thread.
void configureOpenGLSurface(void* rawGLContext);

} // namespace osci::windowing
