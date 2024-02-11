#include <JuceHeader.h>
#include "shape/Point.h"
#include "obj/Frustum.h"

class FrustumTest : public juce::UnitTest {
public:
    FrustumTest() : juce::UnitTest("Frustum Culling") {}

    void runTest() override {
        double focalLength = 1;
        double ratio = 1;
        double nearDistance = 0.1;
        double farDistance = 100;

        Frustum frustum(focalLength, ratio, nearDistance, farDistance);
        frustum.setCameraOrigin(Point(0, 0, -focalLength));

        beginTest("Focal Plane Frustum In-Bounds");

        // Focal plane is at z = 0
        Point points[][2] = {
            {Point(0, 0, 0), Point(0, 0, 0)},
            {Point(1, 1, 0), Point(1, 1, 0)},
            {Point(-1, -1, 0), Point(-1, -1, 0)},
            {Point(1, -1, 0), Point(1, -1, 0)},
            {Point(-1, 1, 0), Point(-1, 1, 0)},
            {Point(0.5, 0.5, 0), Point(0.5, 0.5, 0)},
        };

        testFrustumClippedEqualsExpected(points, frustum, 6);

        beginTest("Focal Plane Frustum Out-Of-Bounds");

        // Focal plane is at z = 0
        Point points2[][2] = {
            {Point(1.1, 1.1, 0), Point(1, 1, 0)},
            {Point(-1.1, -1.1, 0), Point(-1, -1, 0)},
            {Point(1.1, -1.1, 0), Point(1, -1, 0)},
            {Point(-1.1, 1.1, 0), Point(-1, 1, 0)},
            {Point(1.1, 0.5, 0), Point(1, 0.5, 0)},
            {Point(-1.1, 0.5, 0), Point(-1, 0.5, 0)},
            {Point(0.5, -1.1, 0), Point(0.5, -1, 0)},
            {Point(0.5, 1.1, 0), Point(0.5, 1, 0)},
            {Point(10, 10, 0), Point(1, 1, 0)},
            {Point(-10, -10, 0), Point(-1, -1, 0)},
            {Point(10, -10, 0), Point(1, -1, 0)},
            {Point(-10, 10, 0), Point(-1, 1, 0)},
        };

        testFrustumClippedEqualsExpected(points2, frustum, 12);

        beginTest("Behind Camera Out-Of-Bounds");

        double minZWorldCoords = -focalLength + nearDistance;

        Point points3[][2] = {
            {Point(0, 0, -focalLength), Point(0, 0, minZWorldCoords)},
            {Point(0, 0, -100), Point(0, 0, minZWorldCoords)},
            {Point(0.5, 0.5, -focalLength), Point(0.1, 0.1, minZWorldCoords)},
            {Point(10, -10, -focalLength), Point(0.1, -0.1, minZWorldCoords)},
            {Point(-0.5, 0.5, -100), Point(-0.1, 0.1, minZWorldCoords)},
            {Point(-10, 10, -100), Point(-0.1, 0.1, minZWorldCoords)},
        };

        testFrustumClippedEqualsExpected(points3, frustum, 6);

        beginTest("3D Point Out-Of-Bounds");

        Point points4[] = {
            Point(1, 1, -0.1),
            Point(-1, -1, -0.1),
            Point(1, -1, -0.1),
            Point(-1, 1, -0.1),
            Point(0.5, 0.5, minZWorldCoords),
        };

        testFrustumClipOccurs(points4, frustum, 5);
    }

    Point project(Point& p, double focalLength) {
        return Point(
            p.x * focalLength / p.z,
            p.y * focalLength / p.z,
            0
        );
    }

    juce::String errorMessage(Point& actual, Point& expected) {
        return "Expected: " + expected.toString() + " Got: " + actual.toString();
    }

    void testFrustumClippedEqualsExpected(Point points[][2], Frustum& frustum, int length) {
        for (int i = 0; i < length; i++) {
            Point p = points[i][0];
            frustum.clipToFrustum(p);
            expect(p == points[i][1], errorMessage(p, points[i][1]));
        }
    }

    void testFrustumClipOccurs(Point points[], Frustum& frustum, int length) {
        for (int i = 0; i < length; i++) {
            Point p = points[i];
            frustum.clipToFrustum(p);
            expect(p != points[i], errorMessage(p, points[i]));
        }
    }
};

static FrustumTest frustumTest;

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
