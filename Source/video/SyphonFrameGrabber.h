#pragma once
#include <JuceHeader.h>

#include "../parser/img/ImageParser.h"
#include "InvisibleOpenGLContextComponent.h"

class SyphonFrameGrabber : public juce::Component, public juce::OpenGLRenderer {
public:
    SyphonFrameGrabber(SharedTextureManager& manager, juce::String server, juce::String app, ImageParser& parser)
        : manager(manager), parser(parser) {
        // Store server/app info BEFORE creating the GL context, because the GL
        // thread can fire newOpenGLContextCreated() before the constructor body
        // finishes (observed on Windows/Parallels).
        serverName = server;
        appName = app;

        // Create the invisible component that hosts our OpenGL context
        openGLComponent = std::make_unique<InvisibleOpenGLContextComponent>(this);
    }

    ~SyphonFrameGrabber() override {
        // The InvisibleOpenGLContextComponent will detach the context in its destructor
        openGLComponent = nullptr;

        // Clean up the receiver if needed
        if (receiver) {
            manager.removeReceiver(receiver);
            receiver = nullptr;
        }
    }

    // OpenGLRenderer interface implementation
    void newOpenGLContextCreated() override {
        // Create the receiver in the GL context creation callback
        receiver = manager.addReceiver(serverName, appName);
        if (receiver) {
            receiver->setUseCPUImage(true); // for pixel access
            receiver->initGL();
        }
    }

    void renderOpenGL() override {
        // This is called on the OpenGL thread at the frequency set by the context
        if (receiver) {
            receiver->renderGL();

            if (isActive() && receiver->isConnected) {
                juce::Image image = receiver->getImage();
                parser.updateLiveFrame(image);
            }
        }
    }

    void openGLContextClosing() override {
        // Clean up in the context closing callback
        if (receiver) {
            manager.removeReceiver(receiver);
            receiver = nullptr;
        }
    }

    bool isActive() const {
        return receiver != nullptr && receiver->isInit && receiver->enabled;
    }

    juce::String getSourceName() const {
        if (receiver) {
            auto name = receiver->sharingName;
            if (receiver->sharingAppName.isNotEmpty()) {
                name += " (" + receiver->sharingAppName + ")";
            }
            return name;
        }
        return "";
    }

private:
    SharedTextureManager& manager;
    SharedTextureReceiver* receiver = nullptr;
    ImageParser& parser;
    std::unique_ptr<InvisibleOpenGLContextComponent> openGLComponent;
    juce::String serverName;
    juce::String appName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SyphonFrameGrabber)
};
