#pragma once

#include <JuceHeader.h>
#include <memory>

// Presents a completed GL texture beneath native JUCE painting. A null presenter
// leaves presentation to the renderer's existing OpenGL surface.
class FramePresenter {
public:
#if JUCE_MAC
    static constexpr bool usesNativeSurface() { return true; }
#else
    static constexpr bool usesNativeSurface() { return false; }
#endif
    static std::unique_ptr<FramePresenter> create(juce::Component&, juce::OpenGLContext&);
    virtual ~FramePresenter() = default;

    // Message thread. Destroy only after detaching the OpenGL context.
    virtual void resized(juce::Rectangle<int> viewport, juce::Colour background) = 0;
    virtual void paint(juce::Graphics&, juce::Rectangle<int> viewport) = 0;

    // GL thread, with the producer's context current. The source is GL_TEXTURE_2D.
    virtual void present(unsigned texture, int width, int height) = 0;
    virtual void releaseResources() = 0;
};
