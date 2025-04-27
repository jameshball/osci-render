#pragma once
#include <JuceHeader.h>

#include "InvisibleOpenGLContextComponent.h"

class SyphonFrameGrabber : private juce::Thread, public juce::Component {
public:
    SyphonFrameGrabber(SharedTextureManager& manager, juce::String server, juce::String app, ImageParser& parser, int pollMs = 16)
        : juce::Thread("SyphonFrameGrabber"), pollIntervalMs(pollMs), manager(manager), parser(parser) {
        // Create the invisible OpenGL context component
        glContextComponent = std::make_unique<InvisibleOpenGLContextComponent>();
        receiver = manager.addReceiver(server, app);
        if (receiver) {
            receiver->setUseCPUImage(true); // for pixel access
        }
        startThread();
    }

    ~SyphonFrameGrabber() override {
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
                if (glContextComponent) {
                    glContextComponent->getContext().makeActive();
                }
                receiver->renderGL();
                if (glContextComponent) {
                    glContextComponent->getContext().deactivateCurrentContext();
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SyphonFrameGrabber)
};
