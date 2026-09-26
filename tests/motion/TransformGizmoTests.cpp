#include "../../Source/motion/ui/TransformGizmo.h"
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
using namespace motion::editor;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool near(double a, double b) { return std::abs(a - b) < 1.0e-9; }
}

int main() {
    using namespace motion::editor;
    using namespace motion::editor::gizmo;
    Camera camera;
    const auto horizontal = axisDragDistance(camera, { 0, 0 }, { 0.5, 0 }, {}, { 3, 0, 0 });
    check(horizontal.has_value() && near(*horizontal, 0.5), "axis drag uses world units independent of axis magnitude");
    const auto reverse = axisDragDistance(camera, { 0.5, 0 }, { 0, 0 }, {}, { 1, 0, 0 });
    check(reverse.has_value() && near(*reverse, -0.5), "axis drag reverses exactly");
    const auto depth = axisDragDistance(camera, { 0, 0 }, { 0.5, 0 }, { 0, 0, 2 }, { 1, 0, 0 });
    check(depth.has_value() && near(*depth, 0.25), "axis dragging respects perspective depth");
    const auto skew = closestAxisParameter({ { 2, 3, 4 }, { 0, 0, -2 } }, {}, { 1, 0, 0 });
    check(skew.has_value() && near(*skew, 2), "closest approach handles skew nonintersecting lines");
    check(!closestAxisParameter({ { 0, 0, 4 }, { 0, 0, 1 } }, {}, { 1, 0, 0 }), "axis intersection behind picking ray rejects");
    check(!axisDragDistance(camera, {}, { 0.5, 0 }, {}, { 0, 0, 1 }), "view-parallel axis rejects");
    check(!closestAxisParameter({ { 0, 0, 4 }, { 1.0e-5, 0, -1 } }, {}, { 0, 0, 1 }), "nearly parallel axis rejects unstable division");

    const auto quarter = rotationDragAngle(camera, { 0.5, 0 }, { 0, 0.5 }, {}, { 0, 0, 2 });
    check(quarter.has_value() && near(*quarter, std::numbers::pi / 2), "ring turn is signed right-hand positive");
    const auto negative = rotationDragAngle(camera, { 0, 0.5 }, { 0.5, 0 }, {}, { 0, 0, 1 });
    check(negative.has_value() && near(*negative, -std::numbers::pi / 2), "ring reverse is negative");
    const auto flipped = rotationDragAngle(camera, { 0.5, 0 }, { 0, 0.5 }, {}, { 0, 0, -1 });
    check(flipped.has_value() && near(*flipped, -std::numbers::pi / 2), "reversing ring axis reverses sign");
    check(!rotationDragAngle(camera, {}, { 0.5, 0 }, {}, { 0, 0, 1 }), "ring center has no defined angle");
    check(!rotationDragAngle(camera, {}, { 0, 0.5 }, {}, { 1, 0, 0 }), "edge-on ring rejects");
    check(!rotationDragAngle(camera, { 0.5, 0 }, { 0, 0.5 }, { 0, 0, 5 }, { 0, 0, 1 }), "ring behind camera rejects");

    const Ray localFirst { { 0, 0, 4 }, { 0.5, 0, -4 } };
    const Ray localLast { { 0, 0, 4 }, { 0, 0.5, -4 } };
    // A parent scales X by 3 and Y by 0.2. Inverting its world picking rays
    // yields non-unit local directions, which must retain the same ring angle.
    const Vec3 worldFirst { localFirst.direction.x * 3, localFirst.direction.y * 0.2, localFirst.direction.z };
    const Vec3 worldLast { localLast.direction.x * 3, localLast.direction.y * 0.2, localLast.direction.z };
    const auto transformed = rotationDragAngle(Ray { localFirst.origin, { worldFirst.x / 3, worldFirst.y / 0.2, worldFirst.z } },
        Ray { localLast.origin, { worldLast.x / 3, worldLast.y / 0.2, worldLast.z } }, {}, { 0, 0, 1 });
    check(transformed.has_value() && near(*transformed, std::numbers::pi / 2), "inverse-transformed non-unit rays preserve local ring angle");

    camera.look(0.4, -0.3);
    const auto origin = camera.position + camera.forward() * 4;
    const auto first = camera.project(origin + camera.right() * 0.5);
    const auto last = camera.project(origin + camera.up() * 0.5);
    check(first.has_value() && last.has_value(), "rotated camera ring points project");
    const auto rotated = rotationDragAngle(camera, *first, *last, origin, camera.forward() * -1);
    check(rotated.has_value() && near(*rotated, std::numbers::pi / 2), "ring angle remains correct under rotated camera");
    const auto axis = axisDragDistance(camera, { 0, 0 }, { 0.5, 0 }, origin, camera.right());
    check(axis.has_value() && near(*axis, 0.5), "rotated camera axis displacement remains correct");

    check(near(*uniformScaleFactor(0), 1) && near(*uniformScaleFactor(100), 2), "scale is identity at zero and doubles at sensitivity distance");
    check(near(*uniformScaleFactor(-100), 0.5), "negative scale gesture shrinks without inversion");
    check(*uniformScaleFactor(1.0e300) == 1.0e4 && *uniformScaleFactor(-1.0e300) == 1.0e-4, "extreme finite scale gestures saturate");
    check(*uniformScaleFactor(1.0e300, 1.0e-300) == 1.0e4, "overflowing finite sensitivity ratio saturates safely");
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    check(!uniformScaleFactor(nan) && !uniformScaleFactor(1, 0), "invalid scale input rejects");
    check(!axisDragDistance(camera, { nan, 0 }, {}, {}, { 1, 0, 0 }), "nonfinite pointer rejects");
    check(!closestAxisParameter({ {}, {} }, {}, { 1, 0, 0 }), "zero ray direction rejects");
    check(!rotationDragAngle(camera, {}, { 0.5, 0 }, {}, {}), "zero rotation axis rejects");
    check(!closestAxisParameter({ { 0, 0, 4 }, { 0, 0, -1 } }, { 1.0e300, 0, 0 }, { 1, 0, 0 }), "unbounded world coordinates reject");
    std::cout << "Transform gizmo contracts passed\n";
}
