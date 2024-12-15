#include <JuceHeader.h>
#include "obj/Camera.h"
#include "mathter/Common/Approx.hpp"
#include "concurrency/BufferConsumer.h"

class FrustumTest : public juce::UnitTest {
public:
    FrustumTest() : juce::UnitTest("Frustum Culling") {}

    void runTest() override {
        double focalLength = 1;

        Camera camera;
        camera.setFocalLength(focalLength);
        Vec3 position = Vec3(0, 0, -focalLength);
        camera.setPosition(position);
        Frustum frustum = camera.getFrustum();

        beginTest("Focal Plane Frustum In-Bounds");

        // Focal plane is at z = 0
        Vec3 vecs[] = {
            Vec3(0, 0, 0), Vec3(0, 0, 0),
            Vec3(1, 1, 0), Vec3(1, 1, 0),
            Vec3(-1, -1, 0), Vec3(-1, -1, 0),
            Vec3(1, -1, 0), Vec3(1, -1, 0),
            Vec3(-1, 1, 0), Vec3(-1, 1, 0),
            Vec3(0.5, 0.5, 0), Vec3(0.5, 0.5, 0),
        };

        testFrustumClippedEqualsExpected(vecs, camera, 6);

        beginTest("Focal Plane Frustum Out-Of-Bounds");

        // Focal plane is at z = 0
        Vec3 vecs2[] = {
            Vec3(1.1, 1.1, 0), Vec3(1, 1, 0),
            Vec3(-1.1, -1.1, 0), Vec3(-1, -1, 0),
            Vec3(1.1, -1.1, 0), Vec3(1, -1, 0),
            Vec3(-1.1, 1.1, 0), Vec3(-1, 1, 0),
            Vec3(1.1, 0.5, 0), Vec3(1, 0.5, 0),
            Vec3(-1.1, 0.5, 0), Vec3(-1, 0.5, 0),
            Vec3(0.5, -1.1, 0), Vec3(0.5, -1, 0),
            Vec3(0.5, 1.1, 0), Vec3(0.5, 1, 0),
            Vec3(10, 10, 0), Vec3(1, 1, 0),
            Vec3(-10, -10, 0), Vec3(-1, -1, 0),
            Vec3(10, -10, 0), Vec3(1, -1, 0),
            Vec3(-10, 10, 0), Vec3(-1, 1, 0),
        };

        testFrustumClippedEqualsExpected(vecs2, camera, 12);

        beginTest("Behind Camera Out-Of-Bounds");

        double minZWorldCoords = -focalLength + camera.getFrustum().nearDistance;

        Vec3 vecs3[] = {
            Vec3(0, 0, -focalLength), Vec3(0, 0, minZWorldCoords),
            Vec3(0, 0, -100), Vec3(0, 0, minZWorldCoords),
            Vec3(0.5, 0.5, -focalLength), Vec3(0.1, 0.1, minZWorldCoords),
            Vec3(10, -10, -focalLength), Vec3(0.1, -0.1, minZWorldCoords),
            Vec3(-0.5, 0.5, -100), Vec3(-0.1, 0.1, minZWorldCoords),
            Vec3(-10, 10, -100), Vec3(-0.1, 0.1, minZWorldCoords),
        };

        testFrustumClippedEqualsExpected(vecs3, camera, 6);

        beginTest("3D Point Out-Of-Bounds");

        Vec3 vecs4[] = {
            Vec3(1, 1, -0.1),
            Vec3(-1, -1, -0.1),
            Vec3(1, -1, -0.1),
            Vec3(-1, 1, -0.1),
            Vec3(0.5, 0.5, minZWorldCoords),
        };

        testFrustumClipOccurs(vecs4, camera, 5);
    }

    Vec3 project(Vec3& p, double focalLength) {
        return Vec3(
            p.x * focalLength / p.z,
            p.y * focalLength / p.z,
            0
        );
    }

    juce::String vec3ToString(Vec3& p) {
        return "(" + juce::String(p.x) + ", " + juce::String(p.y) + ", " + juce::String(p.z) + ")";
    }

    juce::String errorMessage(Vec3& actual, Vec3& expected) {
        return "Expected: " + vec3ToString(expected) + ", Actual: " + vec3ToString(actual);
    }

    void testFrustumClippedEqualsExpected(Vec3 vecs[], Camera& camera, int length) {
        for (int i = 0; i < length; i++) {
            Vec3 p = vecs[2 * i];
            p = camera.toCameraSpace(p);
            camera.getFrustum().clipToFrustum(p);
            p = camera.toWorldSpace(p);
            expect(mathter::AlmostEqual(p, vecs[2 * i + 1]), errorMessage(p, vecs[2 * i + 1]));
        }
    }

    void testFrustumClipOccurs(Vec3 vecs[], Camera& camera, int length) {
        for (int i = 0; i < length; i++) {
            Vec3 p = vecs[i];
            p = camera.toCameraSpace(p);
            camera.getFrustum().clipToFrustum(p);
            p = camera.toWorldSpace(p);
            expect(!mathter::AlmostEqual(p, vecs[i]), errorMessage(p, vecs[i]));
        }
    }
};

class ProducerThread : public juce::Thread {
public:
    ProducerThread(BufferConsumer& consumer) : juce::Thread("Producer Thread"), consumer(consumer) {}
    
    void run() override {
        for (int i = 0; i < 1024 * 100; i++) {
            consumer.write(OsciPoint(counter++));
        }
    }

private:
    BufferConsumer& consumer;
    int counter = 0;
};

class ConsumerThread : public juce::Thread {
public:
    ConsumerThread(BufferConsumer& consumer) : juce::Thread("Consumer Thread"), consumer(consumer) {}
    
    void run() override {
        for (int i = 0; i < 100; i++) {
            consumer.waitUntilFull();
            auto buffer = consumer.getBuffer();
            for (auto& point : buffer) {
                values.push_back(point.x);
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
    BufferConsumer& consumer;
    std::vector<double> values;
};

class BufferConsumerTest : public juce::UnitTest {
public:
    BufferConsumerTest() : juce::UnitTest("Buffer Consumer") {}

    void runTest() override {
        beginTest("All data received");
        
        BufferConsumer consumer(1024);
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

static FrustumTest frustumTest;
static BufferConsumerTest bufferConsumerTest;

int main(int argc, char* argv[]) {
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    
    runner.runAllTests();
    
	for (int i = 0; i < runner.getNumResults(); ++i) {
		const juce::UnitTestRunner::TestResult* result = runner.getResult(i);
		if (result->failures > 0) {
			return 1;
		}
	}
    
	return 0;
}
