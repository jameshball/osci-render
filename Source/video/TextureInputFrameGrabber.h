#pragma once

#include <JuceHeader.h>
#include <osci_texture_interop/osci_texture_interop.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <vector>

#include "InvisibleOpenGLContextComponent.h"

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif

class TextureInputFrameGrabber final : public juce::Component, private juce::OpenGLRenderer {
public:
    explicit TextureInputFrameGrabber(osci::texture::SourceInfo source) : source(std::move(source)) {
        openGLComponent = std::make_unique<InvisibleOpenGLContextComponent>(static_cast<juce::OpenGLRenderer*>(this));
    }

    ~TextureInputFrameGrabber() override {
        wanted.store(false);
        openGLComponent = nullptr;
        receiver.disconnect();
    }

    std::function<void(juce::String, int, int)> inputStarted;
    std::function<void(const std::vector<std::uint8_t>&, int, int, bool)> frameReady;
    std::function<void()> inputStopped;
    std::function<void(juce::String)> inputFailed;

    void stop() {
        wanted.store(false);
    }

    [[nodiscard]] bool isActive() const {
        return wanted.load() || connected.load() || processorStarted.load();
    }

    [[nodiscard]] juce::String getSourceName() const {
        return source.displayName.isNotEmpty() ? source.displayName : "Texture Input";
    }

private:
    void newOpenGLContextCreated() override {
        wanted.store(true);
    }

    void renderOpenGL() override {
        serviceFrame();
    }

    void openGLContextClosing() override {
        disconnect(true);
        if (readbackFbo != 0) {
            juce::gl::glDeleteFramebuffers(1, &readbackFbo);
            readbackFbo = 0;
        }
        readbackPixels.clear();
    }

    void notifyStartedAsync(osci::texture::SourceInfo receivedSource, std::vector<std::uint8_t> initialFrame, int width, int height, bool verticallyFlipped) {
        if (inputStarted == nullptr) {
            return;
        }

        if (startNotified.exchange(true)) {
            return;
        }

        juce::Component::SafePointer<TextureInputFrameGrabber> safeThis(this);
        juce::MessageManager::callAsync([safeThis, receivedSource, initialFrame = std::move(initialFrame), width, height, verticallyFlipped] {
            if (safeThis == nullptr || safeThis->inputStarted == nullptr) {
                return;
            }

            const juce::String name = receivedSource.displayName.isNotEmpty() ? receivedSource.displayName : "Texture Input";
            safeThis->inputStarted(name, width, height);
            safeThis->processorStarted.store(true);
            if (!initialFrame.empty() && safeThis->frameReady != nullptr) {
                safeThis->frameReady(initialFrame, width, height, verticallyFlipped);
            }
        });
    }

    void notifyStoppedAsync() {
        if (!startNotified.exchange(false)) {
            return;
        }

        juce::Component::SafePointer<TextureInputFrameGrabber> safeThis(this);
        processorStarted.store(false);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr && safeThis->inputStopped != nullptr) {
                safeThis->inputStopped();
            }
        });
    }

    void disconnect(bool notifyProcessor) {
        receiver.disconnect();
        connected.store(false);
        lastFrameIndex = std::numeric_limits<std::uint64_t>::max();
        lastConnectError.store(osci::texture::ErrorCode::none);

        if (notifyProcessor) {
            notifyStoppedAsync();
        } else {
            startNotified.store(false);
            processorStarted.store(false);
        }
    }

    bool readFrame(const osci::texture::ReceivedOpenGLTexture& received) {
        using namespace juce::gl;

        const auto& texture = received.texture;
        if (texture.textureId == 0 || texture.width <= 0 || texture.height <= 0) {
            return false;
        }

        if (readbackFbo == 0) {
            glGenFramebuffers(1, &readbackFbo);
        }

        if (readbackFbo == 0) {
            return false;
        }

        GLint previousReadFramebuffer = 0;
        GLint previousDrawFramebuffer = 0;
        GLint previousReadBuffer = 0;
        GLint previousDrawBuffer = 0;
        GLint previousPackAlignment = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
        glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
        glGetIntegerv(GL_DRAW_BUFFER, &previousDrawBuffer);
        glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);

        glBindFramebuffer(GL_FRAMEBUFFER, readbackFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0,
                               static_cast<GLenum>(texture.textureTarget),
                               static_cast<GLuint>(texture.textureId),
                               0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        const GLenum framebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        bool success = framebufferStatus == GL_FRAMEBUFFER_COMPLETE;
        if (success) {
            readbackPixels.resize((size_t)texture.width * (size_t)texture.height * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(texture.originX,
                         texture.originY,
                         texture.width,
                         texture.height,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         readbackPixels.data());
            const GLenum error = glGetError();
            success = error == GL_NO_ERROR;
        }

        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0,
                               static_cast<GLenum>(texture.textureTarget),
                               0,
                               0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)previousReadFramebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)previousDrawFramebuffer);
        glReadBuffer(static_cast<GLenum>(previousReadBuffer));
        glDrawBuffer(static_cast<GLenum>(previousDrawBuffer));
        glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);

        return success;
    }

    void fail(osci::texture::ErrorCode error, juce::String message = {}) {
        wanted.store(false);

        if (message.isEmpty()) {
            const osci::texture::BackendStatus status = osci::texture::getOpenGLBackendStatus();
            message = status.isAvailable() || status.message.isEmpty()
                ? osci::texture::toString(error)
                : status.message;
        }

        auto callback = inputFailed;
        juce::MessageManager::callAsync([callback, message] {
            if (callback) {
                callback(message);
            }
        });

        disconnect(true);
    }

    void serviceFrame() {
        if (!wanted.load()) {
            if (connected.load() || startNotified.load() || processorStarted.load()) {
                disconnect(true);
            }
            return;
        }

        if (!connected.load()) {
            const osci::texture::ErrorCode error = receiver.connect(source);
            if (error != osci::texture::ErrorCode::none) {
                const osci::texture::ErrorCode previousError = lastConnectError.load();
                if (previousError != error) {
                    lastConnectError.store(error);
                }
                if (error == osci::texture::ErrorCode::sourceNotFound || error == osci::texture::ErrorCode::connectionLost) {
                    connected.store(false);
                    return;
                }

                fail(error);
                return;
            }

            connected.store(true);
            lastConnectError.store(osci::texture::ErrorCode::none);
        }

        osci::texture::ReceivedOpenGLTexture received;
        const osci::texture::ErrorCode error = receiver.receive(received);
        if (error == osci::texture::ErrorCode::receiveFailed) {
            return;
        }

        if (error != osci::texture::ErrorCode::none) {
            if (error == osci::texture::ErrorCode::sourceNotFound || error == osci::texture::ErrorCode::connectionLost) {
                juce::String message = "The selected texture input source was removed.";
                const juce::String sourceName = getSourceName();
                if (sourceName.isNotEmpty() && sourceName != "Texture Input") {
                    message = "The texture input source \"" + sourceName + "\" was removed.";
                }

                fail(error, message);
                return;
            }

            fail(error);
            return;
        }

        if (!received.newFrame && received.texture.frameIndex == lastFrameIndex) {
            return;
        }

        if (!readFrame(received)) {
            fail(osci::texture::ErrorCode::receiveFailed, "The selected texture could not be read by this OpenGL context.");
            return;
        }

        lastFrameIndex = received.texture.frameIndex;
        if (!startNotified.load()) {
            osci::texture::SourceInfo receivedSource = received.source;
            receivedSource.width = received.texture.width;
            receivedSource.height = received.texture.height;
            notifyStartedAsync(receivedSource, readbackPixels, received.texture.width, received.texture.height, true);
            return;
        }

        if (!processorStarted.load()) {
            return;
        }

        if (frameReady != nullptr) {
            frameReady(readbackPixels, received.texture.width, received.texture.height, true);
        }
    }

    osci::texture::SourceInfo source;
    osci::texture::OpenGLReceiver receiver;
    std::unique_ptr<InvisibleOpenGLContextComponent> openGLComponent;
    std::atomic<bool> wanted = false;
    std::atomic<bool> connected = false;
    std::atomic<bool> startNotified = false;
    std::atomic<bool> processorStarted = false;
    std::atomic<osci::texture::ErrorCode> lastConnectError = osci::texture::ErrorCode::none;
    GLuint readbackFbo = 0;
    std::vector<std::uint8_t> readbackPixels;
    std::uint64_t lastFrameIndex = std::numeric_limits<std::uint64_t>::max();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TextureInputFrameGrabber)
    JUCE_DECLARE_WEAK_REFERENCEABLE(TextureInputFrameGrabber)
};
