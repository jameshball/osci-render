#include "VisualiserTextureOutputController.h"

namespace {

class OpenGLTextureOutputPublisher final : public TextureOutputPublisher {
public:
    void setSourceName(juce::String sourceName) override {
        publisher.setSourceName(std::move(sourceName));
    }

    bool isRunning() const override {
        return publisher.isRunning();
    }

    void stop() override {
        publisher.stop();
    }

    osci::texture::ServiceResult service(bool shouldRun, osci::texture::OpenGLTextureFrame frame) override {
        return publisher.service(shouldRun, frame);
    }

private:
    osci::texture::OpenGLTexturePublisher publisher;
};

}

VisualiserTextureOutputController::VisualiserTextureOutputController()
    : VisualiserTextureOutputController(std::make_unique<OpenGLTextureOutputPublisher>()) {}

VisualiserTextureOutputController::VisualiserTextureOutputController(std::unique_ptr<TextureOutputPublisher> newPublisher)
    : publisher(std::move(newPublisher)) {
    jassert(publisher != nullptr);
}

void VisualiserTextureOutputController::setRequested(bool shouldRun) noexcept {
    requested.store(shouldRun, std::memory_order_release);
}

bool VisualiserTextureOutputController::isRequested() const noexcept {
    return requested.load(std::memory_order_acquire);
}

bool VisualiserTextureOutputController::isRunning() const {
    return publisher->isRunning();
}

void VisualiserTextureOutputController::setSourceName(juce::String newSourceName) {
    {
        juce::SpinLock::ScopedLockType lock(sourceNameLock);
        sourceName = std::move(newSourceName);
    }
    sourceNameGeneration.fetch_add(1, std::memory_order_release);
}

osci::texture::ServiceResult VisualiserTextureOutputController::serviceFrame(osci::texture::OpenGLTextureFrame frame) {
    applyPendingSourceName();
    auto result = publisher->service(isRequested(), frame);
    if (result.failed()) {
        requested.store(false, std::memory_order_release);
    }
    return result;
}

osci::texture::ServiceResult VisualiserTextureOutputController::serviceTexture2D(std::uint32_t textureId, int width, int height) {
    osci::texture::OpenGLTextureFrame frame;
    frame.textureId = textureId;
    frame.textureTarget = osci::texture::openGLTexture2D;
    frame.width = width;
    frame.height = height;
    return serviceFrame(frame);
}

osci::texture::ServiceResult VisualiserTextureOutputController::stop() {
    if (!publisher->isRunning()) {
        return {};
    }

    publisher->stop();
    return { osci::texture::ServiceEvent::stopped };
}

void VisualiserTextureOutputController::applyPendingSourceName() {
    const auto pendingGeneration = sourceNameGeneration.load(std::memory_order_acquire);
    if (pendingGeneration == appliedSourceNameGeneration) {
        return;
    }

    juce::String pendingSourceName;
    {
        juce::SpinLock::ScopedLockType lock(sourceNameLock);
        pendingSourceName = sourceName;
    }
    publisher->setSourceName(std::move(pendingSourceName));
    appliedSourceNameGeneration = pendingGeneration;
}
