#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

#include <osci_texture_interop/osci_texture_interop.h>

class TextureOutputPublisher {
public:
    virtual ~TextureOutputPublisher() = default;

    virtual void setSourceName(juce::String sourceName) = 0;
    [[nodiscard]] virtual bool isRunning() const = 0;
    virtual void stop() = 0;
    virtual osci::texture::ServiceResult service(bool shouldRun, osci::texture::OpenGLTextureFrame frame) = 0;
};

class OpenGLTextureOutputPublisher final : public TextureOutputPublisher {
public:
    void setSourceName(juce::String sourceName) override;
    [[nodiscard]] bool isRunning() const override;
    void stop() override;
    osci::texture::ServiceResult service(bool shouldRun, osci::texture::OpenGLTextureFrame frame) override;

private:
    osci::texture::OpenGLTexturePublisher publisher;
};

class VisualiserTextureOutputController final {
public:
    explicit VisualiserTextureOutputController(std::unique_ptr<TextureOutputPublisher> publisher = std::make_unique<OpenGLTextureOutputPublisher>());

    void setRequested(bool shouldRun) noexcept;
    [[nodiscard]] bool isRequested() const noexcept;
    [[nodiscard]] bool isRunning() const;

    void setSourceName(juce::String sourceName);
    [[nodiscard]] juce::String getSourceName() const;

    osci::texture::ServiceResult serviceFrame(osci::texture::OpenGLTextureFrame frame);
    osci::texture::ServiceResult serviceTexture2D(std::uint32_t textureId, int width, int height);

    // Stops the backend on the calling thread without changing the requested
    // state, allowing output to resume after an OpenGL context is recreated.
    osci::texture::ServiceResult stop();

private:
    void applyPendingSourceName();

    std::unique_ptr<TextureOutputPublisher> publisher;
    std::atomic<bool> requested { false };

    mutable juce::SpinLock sourceNameLock;
    juce::String sourceName;
    std::atomic<std::uint64_t> sourceNameGeneration { 1 };
    std::uint64_t appliedSourceNameGeneration = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserTextureOutputController)
};
