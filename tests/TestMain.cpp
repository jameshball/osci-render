#include <JuceHeader.h>
#include "../Source/audio/OutputClip.h"
#include "../Source/visualiser/VisualiserGeometry.h"

class ProducerThread : public juce::Thread {
public:
    ProducerThread(osci::BufferConsumer& consumer) : juce::Thread("Producer Thread"), consumer(consumer) {}
    
    void run() override {
        for (int i = 0; i < 1024 * 100; i++) {
            consumer.write(osci::Point(counter++));
        }
    }

private:
    osci::BufferConsumer& consumer;
    int counter = 0;
};

class ConsumerThread : public juce::Thread {
public:
    ConsumerThread(osci::BufferConsumer& consumer) : juce::Thread("Consumer Thread"), consumer(consumer) {}
    
    void run() override {
        for (int i = 0; i < 100; i++) {
            consumer.waitUntilFull();
            auto buffer = consumer.getBuffer();
            for (int i = 0; i < buffer.getNumSamples(); i++) {
                values.push_back(osci::Point::fromAudioBuffer(buffer, i).x);
            }
        }
    }
    
    bool containsGap() {
        for (int i = 0; i < values.size() - 1; i++) {
            if (values[i] + 1 != values[i + 1]) {
                return true;
            }
        }
        
        return false;
    }

private:
    osci::BufferConsumer& consumer;
    std::vector<double> values;
};

class BufferConsumerTest : public juce::UnitTest {
public:
    BufferConsumerTest() : juce::UnitTest("Buffer Consumer") {}

    void runTest() override {
        beginTest("All data received");
        
        osci::BufferConsumer consumer(1024);
        ProducerThread producer(consumer);
        ConsumerThread consumerThread(consumer);
        
        consumer.setBlockOnWrite(true);
        
        for (int i = 0; i < 100; i++) {
            producer.startThread();
            consumerThread.startThread();
            
            producer.waitForThreadToExit(-1);
            consumerThread.waitForThreadToExit(-1);
            
            expect(!consumerThread.containsGap(), "There was a gap in the data");
        }
    }
};

static BufferConsumerTest bufferConsumerTest;

class OutputClipTest : public juce::UnitTest {
public:
    OutputClipTest() : juce::UnitTest("Output Clip", "Audio") {}

    void runTest() override {
        beginTest("Top threshold bypasses clipping");
        expect(!osci::isOutputClipActive(1.0f));
        expect(!osci::isOutputClipActive(osci::kOutputClipBypassThreshold));
        expect(!osci::isOutputClipActive(osci::kOutputClipBypassThreshold + 0.5e-5f));
        expect(osci::isOutputClipActive(osci::kOutputClipBypassThreshold - 0.5e-5f));

        beginTest("Bypassed clipping applies volume only");
        expectWithinAbsoluteError(osci::applyVolumeAndOptionalClip(0.75f, 2.0f, 1.0f), 1.5f, 0.0001f);
        expectWithinAbsoluteError(osci::applyVolumeAndOptionalClip(-0.75f, 2.0f, 1.0f), -1.5f, 0.0001f);
        expectWithinAbsoluteError(osci::applyVolumeAndOptionalClip(0.75f, 2.0f, osci::kOutputClipBypassThreshold), 1.5f, 0.0001f);

        beginTest("Active clipping limits symmetrically");
        expectWithinAbsoluteError(osci::applyVolumeAndOptionalClip(0.75f, 2.0f, 0.8f), 0.8f, 0.0001f);
        expectWithinAbsoluteError(osci::applyVolumeAndOptionalClip(-0.75f, 2.0f, 0.8f), -0.8f, 0.0001f);
        expectWithinAbsoluteError(osci::applyVolumeAndOptionalClip(0.25f, 2.0f, 0.8f), 0.5f, 0.0001f);
    }
};

static OutputClipTest outputClipTest;

class VisualiserCanvasGeometryTest : public juce::UnitTest {
public:
    VisualiserCanvasGeometryTest() : juce::UnitTest("Visualiser Canvas Geometry", "Visualiser") {}

    void runTest() override {
        beginTest("Canvas dimensions are clamped and even");
        auto size = VisualiserGeometry::sanitiseRenderSize(127, 5001);
        expect(size.width == 128, "Width should clamp to the minimum even dimension");
        expect(size.height == 4096, "Height should clamp to the maximum even dimension");

        size = VisualiserGeometry::sanitiseRenderSize(1919, 1079);
        expect(size.width == 1918, "Odd width should round down to an even value");
        expect(size.height == 1078, "Odd height should round down to an even value");

        beginTest("Canvas preset lookup");
        expect(VisualiserGeometry::getPresetForRenderSize({1024, 1024}) == VisualiserCanvasPreset::Square);
        expect(VisualiserGeometry::getPresetForRenderSize({1920, 1080}) == VisualiserCanvasPreset::HDLandscape);
        expect(VisualiserGeometry::getPresetForRenderSize({1080, 1920}) == VisualiserCanvasPreset::HDPortrait);
        expect(VisualiserGeometry::getPresetForRenderSize({1280, 720}) == VisualiserCanvasPreset::Custom);
        expect(VisualiserGeometry::sanitiseCanvasPreset(999, VisualiserCanvasPreset::HDPortrait) == VisualiserCanvasPreset::HDPortrait);

        beginTest("Aspect-scaled render targets preserve output ratio");
        size = VisualiserGeometry::getAspectScaledRenderSize({1920, 1080}, 512);
        expect(size.width == 512);
        expect(size.height == 288);
        size = VisualiserGeometry::getAspectScaledRenderSize({1080, 1920}, 128);
        expect(size.width == 72);
        expect(size.height == 128);

        beginTest("Legacy resolution loads as square canvas");
        auto xml = std::make_unique<juce::XmlElement>("state");
        auto* settingsXml = xml->createNewChildElement("recordingSettings");
        auto* resolutionXml = settingsXml->createNewChildElement("resolution");
        auto* parameterXml = resolutionXml->createNewChildElement("parameter");
        parameterXml->setAttribute("id", "resolution");
        parameterXml->setAttribute("value", 1920);
        expect(VisualiserGeometry::getLegacyRenderSizeFromRecordingSettingsXml(settingsXml) == VisualiserRenderSize{1920, 1920}, "Legacy resolution should become width and height");

        beginTest("World extents preserve XY geometry");
        auto scale = VisualiserGeometry::getWorldToClipScale({1024, 1024});
        expectWithinAbsoluteError(scale.x, 1.0f, 0.0001f);
        expectWithinAbsoluteError(scale.y, 1.0f, 0.0001f);

        scale = VisualiserGeometry::getWorldToClipScale({1920, 1080});
        expectWithinAbsoluteError(scale.x, 0.5625f, 0.0001f);
        expectWithinAbsoluteError(scale.y, 1.0f, 0.0001f);

        scale = VisualiserGeometry::getWorldToClipScale({1080, 1920});
        expectWithinAbsoluteError(scale.x, 1.0f, 0.0001f);
        expectWithinAbsoluteError(scale.y, 0.5625f, 0.0001f);

        beginTest("Graticule layout keeps square cells");
        auto graticule = VisualiserGeometry::getGraticuleLayout({1024, 1024});
        expect(graticule.xDivisions == 10);
        expect(graticule.yDivisions == 10);
        expectWithinAbsoluteError(graticule.cellSizePixels, 90.0f, 0.0001f);
        expectWithinAbsoluteError(graticule.xOriginPixels, 62.0f, 0.0001f);
        expectWithinAbsoluteError(graticule.yOriginPixels, 62.0f, 0.0001f);
        expectWithinAbsoluteError(graticule.lineWidthPixels, 2.0f, 0.0001f);

        graticule = VisualiserGeometry::getGraticuleLayout({1920, 1080});
        expect(graticule.xDivisions == 18);
        expect(graticule.yDivisions == 10);
        expectWithinAbsoluteError(graticule.cellSizePixels, 94.9219f, 0.0001f);

        graticule = VisualiserGeometry::getGraticuleLayout({1080, 1920});
        expect(graticule.xDivisions == 10);
        expect(graticule.yDivisions == 18);
        expectWithinAbsoluteError(graticule.cellSizePixels, 94.9219f, 0.0001f);

        graticule = VisualiserGeometry::getGraticuleLayout({1030, 1024});
        expect(graticule.xDivisions == 10, "Near-square landscape grids should not add a clipped extra pair of columns");
        expect(graticule.xOriginPixels >= 0.0f, "Graticule should stay inside the canvas horizontally");
        expect(graticule.xOriginPixels + graticule.cellSizePixels * static_cast<float>(graticule.xDivisions) <= 1030.0f);

        graticule = VisualiserGeometry::getGraticuleLayout({1024, 1030});
        expect(graticule.yDivisions == 10, "Near-square portrait grids should not add a clipped extra pair of rows");
        expect(graticule.yOriginPixels >= 0.0f, "Graticule should stay inside the canvas vertically");
        expect(graticule.yOriginPixels + graticule.cellSizePixels * static_cast<float>(graticule.yDivisions) <= 1030.0f);

        beginTest("Aspect-fit viewport preserves canvas ratio");
        auto fitted = VisualiserGeometry::getAspectFitBounds({0, 0, 1000, 1000}, {1920, 1080});
        expect(fitted.getWidth() == 1000);
        expect(fitted.getHeight() == 562 || fitted.getHeight() == 563);
        expect(fitted.getY() == 218 || fitted.getY() == 219);

        fitted = VisualiserGeometry::getAspectFitBounds({0, 0, 1000, 1000}, {1080, 1920});
        expect(fitted.getWidth() == 562 || fitted.getWidth() == 563);
        expect(fitted.getHeight() == 1000);
        expect(fitted.getX() == 218 || fitted.getX() == 219);
    }
};

static VisualiserCanvasGeometryTest visualiserCanvasGeometryTest;

int main(int argc, char* argv[]) {
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    
    if (argc > 1) {
        // Run only tests matching the given category
        juce::String category(argv[1]);
        runner.runTestsInCategory(category);
    } else {
        runner.runAllTests();
    }
    
	for (int i = 0; i < runner.getNumResults(); ++i) {
		const juce::UnitTestRunner::TestResult* result = runner.getResult(i);
		if (result->failures > 0) {
			return 1;
		}
	}

	// Clean up MessageManager if any test created one (e.g. MidiCC tests use
	// Timer/ChangeBroadcaster which require it).
	juce::DeletedAtShutdown::deleteAll();
	if (juce::MessageManager::getInstanceWithoutCreating() != nullptr)
		juce::MessageManager::deleteInstance();
    
	return 0;
}
