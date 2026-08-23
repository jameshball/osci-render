#include <JuceHeader.h>

#include <utility>

#include "../Source/visualiser/VisualiserTextureOutputController.h"

namespace {

class FakeTextureOutputPublisher final : public TextureOutputPublisher {
public:
    void setSourceName(juce::String newSourceName) override {
        sourceName = std::move(newSourceName);
        ++sourceNameUpdates;
    }

    bool isRunning() const override {
        return running;
    }

    void stop() override {
        running = false;
        ++stopCalls;
    }

    osci::texture::ServiceResult service(bool shouldRun, osci::texture::OpenGLTextureFrame newFrame) override {
        ++serviceCalls;
        lastShouldRun = shouldRun;
        lastFrame = newFrame;

        if (nextResult.failed()) {
            running = false;
            auto result = nextResult;
            nextResult = {};
            return result;
        }

        if (shouldRun && !running) {
            running = true;
            return { osci::texture::ServiceEvent::started };
        }
        if (!shouldRun && running) {
            running = false;
            return { osci::texture::ServiceEvent::stopped };
        }
        return {};
    }

    bool running = false;
    bool lastShouldRun = false;
    int serviceCalls = 0;
    int stopCalls = 0;
    int sourceNameUpdates = 0;
    juce::String sourceName;
    osci::texture::OpenGLTextureFrame lastFrame;
    osci::texture::ServiceResult nextResult;
};

class VisualiserTextureOutputControllerTest final : public juce::UnitTest {
public:
    VisualiserTextureOutputControllerTest()
        : juce::UnitTest("Visualiser texture output controller", "Visualiser") {}

    void runTest() override {
        beginTest("Requests and names are applied when a frame is serviced");
        auto publisher = std::make_unique<FakeTextureOutputPublisher>();
        auto* fake = publisher.get();
        VisualiserTextureOutputController controller(std::move(publisher));

        controller.setSourceName("Test output");
        controller.setRequested(true);
        expect(controller.isRequested());
        expectEquals(fake->sourceNameUpdates, 0);
        expectEquals(fake->serviceCalls, 0);

        auto result = controller.serviceTexture2D(42, 640, 480);
        expect(result.event == osci::texture::ServiceEvent::started);
        expect(fake->running);
        expectEquals(fake->sourceName, juce::String("Test output"));
        expectEquals(fake->sourceNameUpdates, 1);
        expect(fake->lastShouldRun);
        expectEquals(static_cast<int>(fake->lastFrame.textureId), 42);
        expectEquals(fake->lastFrame.width, 640);
        expectEquals(fake->lastFrame.height, 480);

        controller.serviceTexture2D(43, 640, 480);
        expectEquals(fake->sourceNameUpdates, 1);

        beginTest("Disabling is serviced on the caller thread");
        controller.setRequested(false);
        expect(fake->running);
        result = controller.serviceTexture2D(43, 640, 480);
        expect(result.event == osci::texture::ServiceEvent::stopped);
        expect(!fake->running);

        beginTest("Failures clear requested state");
        controller.setRequested(true);
        fake->nextResult = { osci::texture::ServiceEvent::failed,
                             osci::texture::ErrorCode::publishFailed,
                             "Publish failed" };
        result = controller.serviceTexture2D(44, 640, 480);
        expect(result.failed());
        expect(!controller.isRequested());

        beginTest("Context shutdown preserves the requested state");
        controller.setRequested(true);
        controller.serviceTexture2D(45, 640, 480);
        expect(fake->running);
        result = controller.stop();
        expect(result.event == osci::texture::ServiceEvent::stopped);
        expect(controller.isRequested());
        expect(!fake->running);
        expectEquals(fake->stopCalls, 1);

        result = controller.serviceTexture2D(46, 640, 480);
        expect(result.event == osci::texture::ServiceEvent::started);
        expect(fake->running);

        beginTest("Source-name changes are applied once on the next service");
        controller.setSourceName("Renamed output");
        controller.serviceTexture2D(47, 640, 480);
        expectEquals(fake->sourceName, juce::String("Renamed output"));
        expectEquals(fake->sourceNameUpdates, 2);
    }
};

static VisualiserTextureOutputControllerTest visualiserTextureOutputControllerTest;

}
