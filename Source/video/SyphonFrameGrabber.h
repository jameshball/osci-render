#pragma once
#include <JuceHeader.h>

#include "InvisibleOpenGLContextComponent.h"

class SyphonFrameGrabber : private juce::Thread, public juce::Component {
public:
    SyphonFrameGrabber(SharedTextureManager& manager, juce::String server, juce::String app, ImageParser& parser, int pollMs = 8)
        : juce::Thread("SyphonFrameGrabber"), pollIntervalMs(pollMs), manager(manager), parser(parser) {
        // Create the invisible OpenGL context component
        glContextComponent = std::make_unique<InvisibleOpenGLContextComponent>();
        
        // Make sure the context is properly initialized before creating the receiver
        glContextComponent->getContext().makeActive();
        
        // Create the receiver after the context is active
        receiver = manager.addReceiver(server, app);
        if (receiver) {
            receiver->setUseCPUImage(true); // for pixel access
            
            // Initialize the receiver with the active GL context
            receiver->initGL();
        }
        
        // Release the context
        glContextComponent->getContext().deactivateCurrentContext();
        
        // Start the thread after everything is set up
        startThread();
    }

    ~SyphonFrameGrabber() override {
        juce::CriticalSection::ScopedLockType lock(openGLLock);

        stopThread(500);
        if (receiver) {
            manager.removeReceiver(receiver);
            receiver = nullptr;
        }
        glContextComponent.reset();
    }

    void run() override {
        while (!threadShouldExit()) {
            {
                juce::CriticalSection::ScopedLockType lock(openGLLock);

                bool activated = false;
                if (glContextComponent) {
                    activated = glContextComponent->getContext().makeActive();
                }
                if (juce::OpenGLContext::getCurrentContext() != nullptr) {
                    receiver->renderGL();
                }
                if (activated && glContextComponent) {
                    juce::OpenGLContext::deactivateCurrentContext();
                }
                if (isActive() && receiver->isConnected) {
                    juce::Image image = receiver->getImage();
                    parser.updateLiveFrame(image);
                }
            }
            wait(pollIntervalMs);
        }
    }

    bool isActive() const {
        return receiver != nullptr && receiver->isInit && receiver->enabled;
    }

    juce::String getSourceName() const {
        if (receiver) {
            return receiver->sharingName + " (" + receiver->sharingAppName + ")";
        }
        return "";
    }

private:
    int pollIntervalMs;
    SharedTextureManager& manager;
    SharedTextureReceiver* receiver = nullptr;
    ImageParser& parser;
    std::unique_ptr<InvisibleOpenGLContextComponent> glContextComponent;
    juce::CriticalSection openGLLock; // To protect OpenGL context operations

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SyphonFrameGrabber)
};
