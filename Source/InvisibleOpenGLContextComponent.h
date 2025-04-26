#pragma once
#include <JuceHeader.h>

// An invisible component that owns a persistent OpenGL context, always attached to the desktop.
class InvisibleOpenGLContextComponent : public juce::Component {
public:
    InvisibleOpenGLContextComponent() {
        setSize(1, 1); // Minimal size
        setBounds(0, 0, 1, 1); // Minimal bounds
        setVisible(true);
        setOpaque(false);
        context.setComponentPaintingEnabled(false);
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
