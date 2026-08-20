#pragma once

#include <JuceHeader.h>

inline constexpr bool isNativeWindowTransparencySupported() {
#if JUCE_MAC
    return true;
#else
    return false;
#endif
}

// Must be called after the window is visible and has a native peer.
void configureNativeWindowTransparency(juce::Component* topLevelWindow);

// Must be called on the message thread after the window has a native peer.
void setNativeWindowIgnoresMouseEvents(juce::Component* topLevelWindow, bool ignoresMouseEvents);

// Must be called on the OpenGL thread.
void configureOpenGLSurfaceTransparency(void* rawGLContext);
