#pragma once
#include <JuceHeader.h>

// An invisible component that owns a persistent OpenGL context, always attached to the desktop.
class InvisibleOpenGLContextComponent : public juce::Component {
public:
    InvisibleOpenGLContextComponent(juce::OpenGLRenderer* renderer) {
        setSize(1, 1); // Minimal size
        setBounds(0, 0, 1, 1); // Minimal bounds
        setVisible(true);
        setOpaque(false);
        context.setComponentPaintingEnabled(false);
        context.setRenderer(renderer);
        context.setContinuousRepainting(true);
        context.attachTo(*this);
        addToDesktop(
            juce::ComponentPeer::windowIsTemporary |
            juce::ComponentPeer::windowIgnoresKeyPresses |
            juce::ComponentPeer::windowIgnoresMouseClicks
        );
    }
    ~InvisibleOpenGLContextComponent() override {
        context.detach();
    }

    juce::OpenGLContext& getContext() { return context; }

private:
    juce::OpenGLContext context;
};
