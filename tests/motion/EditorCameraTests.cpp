#include "../../Source/motion/ui/EditorCamera.h"
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
bool near(double left, double right, double tolerance = 1.0e-9) { return std::abs(left - right) < tolerance; }
bool near(motion::Vec3 left, motion::Vec3 right, double tolerance = 1.0e-9) { return (left - right).length() < tolerance; }
}

int main() {
    using namespace motion::editor;
    using motion::Vec3;
    Camera camera;
    const auto projected = camera.project({ 0.5, -0.25, 0 });
    check(projected.has_value() && near(projected->x, 0.5) && near(projected->y, -0.25), "default editor framing matches fixed output camera");
    const auto closer = camera.project({ 0.5, -0.25, 2 });
    check(closer.has_value() && near(closer->x, 1) && near(closer->y, -0.5), "perspective scales with depth");
    const auto wide = camera.project({ 0.5, -0.25, 0 }, 2);
    check(wide.has_value() && near(wide->x, 0.25), "viewport aspect affects only horizontal framing");
    check(!camera.project({ 0, 0, 5 }) && !camera.project(camera.position), "behind-camera and camera-origin points are rejected");
    check(!camera.project(camera.position + camera.forward() * (Camera::nearPlane * 0.5)), "near-plane rejection protects perspective division");

    camera.position = { 2, 3, 5 };
    camera.yaw = 0.4;
    camera.pitch = -0.3;
    check(near(camera.forward().length(), 1) && near(camera.right().dot(camera.up()), 0), "camera basis remains orthonormal under rotation");
    for (const double aspect : { 0.5, 1.0, 2.0 }) {
        for (const Vec2 screen : { Vec2 { -0.8, -0.6 }, Vec2 { 0, 0 }, Vec2 { 0.7, 0.9 } }) {
            const auto ray = camera.ray(screen, aspect);
            check(ray.has_value(), "valid rotated camera produces a picking ray");
            const auto world = ray->origin + ray->direction * 7;
            const auto roundTrip = camera.project(world, aspect);
            check(roundTrip.has_value() && near(roundTrip->x, screen.x) && near(roundTrip->y, screen.y), "rotated projection and rays are inverse");
            const auto intersection = Camera::intersectPlane(*ray, world, camera.forward());
            check(intersection.has_value() && near(*intersection, world), "picking ray reaches its camera-facing world plane");
        }
    }
    const auto delta = camera.translationOnFacingPlane({ 0, 0 }, { 0.25, -0.1 }, camera.position + camera.forward() * 4);
    check(delta.has_value() && near(*delta, camera.right() * 0.25 + camera.up() * -0.1), "translation drag maps NDC delta onto facing world plane");
    check(!Camera::intersectPlane({ { 0, 0, 1 }, { 1, 0, 0 } }, { 0, 0, 0 }, { 0, 0, 1 }), "parallel rays do not produce intersections");
    check(!Camera::intersectPlane({ { 0, 0, 1 }, { 0, 0, 1 } }, { 0, 0, 0 }, { 0, 0, 1 }), "intersections behind a ray are rejected");
    check(!camera.translationOnFacingPlane({0, 0}, {1, 1}, camera.position - camera.forward()), "translation cannot use a plane behind the camera");

    camera = Camera();
    check(camera.orbit(std::numbers::pi / 2, 0), "orbit accepts a quarter turn");
    check(near(camera.position, { -4, 0, 0 }) && near(camera.distance(), 4), "orbit retains pivot radius");
    const auto pivotScreen = camera.project(camera.pivot);
    check(pivotScreen.has_value() && near(pivotScreen->x, 0) && near(pivotScreen->y, 0), "orbit keeps pivot centered");
    check(camera.orbit(1.0e300, 1.0e300) && camera.pitch == Camera::pitchLimit, "extreme finite orbit increments wrap yaw and clamp pitch");
    check(near(camera.distance(), 4), "pole clamp retains orbit radius");
    check(camera.dolly(1.0e300) && near(camera.distance(), Camera::maximumDistance, 0.00001), "zoom-out saturates at maximum distance");
    check(camera.dolly(-1.0e300) && near(camera.distance(), Camera::minimumDistance), "zoom-in saturates before reaching pivot");

    camera = Camera();
    const auto eyeBeforeLook = camera.position;
    check(camera.look(std::numbers::pi / 2, 0), "look rotates without translating the eye");
    check(near(camera.position, eyeBeforeLook) && near(camera.pivot, { 4, 0, 4 }), "look moves pivot onto new forward ray at preserved distance");
    const auto lookedPivot = camera.project(camera.pivot);
    check(lookedPivot.has_value() && near(lookedPivot->x, 0) && near(lookedPivot->y, 0), "look maintains centered orbit pivot");
    check(camera.look(1.0e300, -1.0e300) && camera.pitch == -Camera::pitchLimit, "look bounds extreme finite increments");
    camera = Camera();
    camera.position.x = Camera::maximumCoordinate;
    camera.pivot.x = Camera::maximumCoordinate;
    const auto beforeRejectedLook = camera;
    check(!camera.look(std::numbers::pi / 2, 0), "look rejects out-of-bounds resulting pivot");
    check(near(camera.position, beforeRejectedLook.position) && near(camera.pivot, beforeRejectedLook.pivot)
        && camera.yaw == beforeRejectedLook.yaw && camera.pitch == beforeRejectedLook.pitch, "rejected look preserves every camera field");
    check(!camera.look(std::numeric_limits<double>::quiet_NaN(), 0), "look rejects nonfinite input");

    camera = Camera();
    check(camera.pan(100, 50, 1000), "pixel pan accepts viewport dimensions");
    check(near(camera.position, { -0.2, 0.1, 4 }) && near(camera.pivot, { -0.2, 0.1, 0 }), "pixel pan moves camera and pivot by world-per-pixel scale");
    auto once = Camera();
    auto continuous = Camera();
    check(once.fly({ 0, 0, 1 }, 3, 1), "continuous navigation accepts delta time");
    for (int frame = 0; frame < 100; ++frame) {
        check(continuous.fly({ 0, 0, 1 }, 3, 0.01), "small navigation timestep remains valid");
    }
    check(near(once.position, continuous.position) && near(once.pivot, continuous.pivot), "fly movement is independent of timer frequency");
    auto diagonal = Camera();
    check(diagonal.fly({ 1, 1, 1 }, 2, 1), "diagonal movement is accepted");
    check(near((diagonal.position - Vec3 { 0, 0, 4 }).length(), 2), "diagonal input does not increase movement speed");
    camera = Camera();
    check(camera.frame({ 10, -2, 3 }, 2), "frame fits a finite bounding sphere");
    check(near(camera.pivot, { 10, -2, 3 }), "frame updates orbit pivot");
    const auto sphereEdge = camera.project(camera.pivot + camera.right() * 2);
    check(sphereEdge.has_value() && std::abs(sphereEdge->x) < 1, "framed sphere fits inside viewport with margin");
    check(camera.frame({ 0, 0, 0 }, 0) && near(camera.distance(), Camera::minimumDistance), "zero-radius selection gets a safe minimum distance");

    const auto before = camera;
    const auto invalid = std::numeric_limits<double>::quiet_NaN();
    check(!camera.orbit(invalid, 0) && !camera.dolly(invalid), "invalid angle and zoom requests reject");
    check(!camera.pan(1, 1, 0) && !camera.fly({1, 0, 0}, 1, -1), "invalid viewport and negative delta time reject");
    check(!camera.frame({0, 0, 0}, -1) && !camera.frame({0, 0, 0}, std::numeric_limits<double>::max()), "negative and unframeable radii reject");
    check(!camera.fly({1, 0, 0}, std::numeric_limits<double>::max(), 2), "overflowing travel rejects");
    check(near(camera.position, before.position) && near(camera.pivot, before.pivot), "invalid navigation leaves camera unchanged");
    check(!camera.project({invalid, 0, 0}) && !camera.ray({invalid, 0}) && !camera.ray({0, 0}, 0), "invalid projection inputs reject");
    camera.fovDegrees = 0;
    check(!camera.project({0, 0, 0}) && !camera.ray({0, 0}), "invalid field of view rejects projection and rays");
    camera = Camera();
    camera.position.x = std::numeric_limits<double>::max();
    check(!camera.valid() && !camera.pan(1, 1, 100), "extreme camera coordinates cannot poison navigation");
    std::cout << "Editor camera contracts passed\n";
}
