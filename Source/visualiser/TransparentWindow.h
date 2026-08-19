#pragma once

#include <JuceHeader.h>

// Must be called after the window is visible and has a native peer.
void configureNativeWindowTransparency(juce::Component* topLevelWindow);

// Must be called on the OpenGL thread.
void configureOpenGLSurfaceTransparency(void* rawGLContext);
