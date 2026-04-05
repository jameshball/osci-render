#pragma once

#include <JuceHeader.h>

// Configure a native window for true transparency on macOS.
// Sets NSWindow.opaque=NO, clears the background, removes shadow.
// Must be called AFTER the window is visible (peer exists).
void configureNativeWindowTransparency(juce::Component* topLevelWindow);

// Configure the OpenGL surface for transparency (must be called on GL thread).
// On macOS sets NSOpenGLCPSurfaceOpacity=0.
void configureOpenGLSurfaceTransparency(void* rawGLContext);
