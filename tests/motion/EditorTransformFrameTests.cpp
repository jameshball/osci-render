#include "../../Source/motion/ui/EditorTransformFrame.h"
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
using namespace motion;
using namespace motion::editor;
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
bool near(Vec3 a, Vec3 b) { return (a - b).length() < 1.0e-9; }
struct Project {
    double bpm = 120;
    motion::Tempo tempo() const { return motion::Tempo(bpm); }
    std::vector<Track> tracks;
    std::vector<Group> groups;
    std::vector<EffectInstance> effects;
};
Project fixture() {
    Project project;
    Track track;
    track.id = 1;
    Clip clip;
    clip.id = 2;
    clip.properties["position.x"] = Curve(1);
    clip.properties["position.y"] = Curve(2);
    clip.properties["position.z"] = Curve(3);
    track.clips.push_back(clip);
    project.tracks.push_back(track);
    return project;
}
}
int main() {
    auto project = fixture();
    auto frame = clipTransformFrame(project, 2, 0);
    check(frame.has_value() && near(frame->worldOrigin, {1, 2, 3}), "ungrouped origin uses clip position");
    check(near(*frame->positionDelta({4, 5, 6}), {4, 5, 6}), "ungrouped world delta is position delta");
    project.tracks[0].clips[0].properties["rotation.x"] = Curve(47);
    project.tracks[0].clips[0].properties["scale.y"] = Curve(0);
    frame = clipTransformFrame(project, 2, 0);
    check(frame.has_value() && near(frame->worldOrigin, {1, 2, 3}), "own rotation and collapsed scale do not change position frame");

    Group inner;
    inner.id = 3;
    inner.parent = 4;
    inner.properties["position.x"] = Curve(10);
    inner.properties["scale.x"] = Curve(2);
    inner.properties["scale.y"] = Curve(3);
    inner.properties["scale.z"] = Curve(4);
    inner.properties["rotation.z"] = Curve(90);
    Group outer;
    outer.id = 4;
    outer.properties["position.y"] = Curve(5);
    outer.properties["scale.y"] = Curve(2);
    outer.properties["scale.z"] = Curve(0.5);
    outer.properties["rotation.y"] = Curve(90);
    project.groups = { inner, outer };
    project.tracks[0].group = 3;
    frame = clipTransformFrame(project, 2, 0);
    check(frame.has_value() && near(frame->worldOrigin, {6, 9, -4}), "nested groups apply inner to outer scale rotate translate");
    check(near(frame->parentBasis[0], {0, 4, 0}) && near(frame->parentBasis[1], {0, 0, 3})
        && near(frame->parentBasis[2], {2, 0, 0}), "parent basis retains rotated non-uniform scale");
    check(near(*frame->positionDelta({1, 8, -3}), {2, -1, 0.5}), "world translation removes ancestors in reverse order");
    project.groups[0].properties["rotation.x"] = Curve(31);
    project.groups[0].properties["rotation.y"] = Curve(-58);
    project.groups[1].properties["rotation.z"] = Curve(-73);
    project.groups[1].properties["scale.x"] = Curve(-2);
    frame = clipTransformFrame(project, 2, 0);
    const Vec3 delta {0.37, -1.25, 2.4};
    const auto world = frame->parentBasis[0] * delta.x + frame->parentBasis[1] * delta.y + frame->parentBasis[2] * delta.z;
    check(near(*frame->positionDelta(world), delta), "all-axis nested mirrored transforms invert without changing rotation order");

    project = fixture();
    auto& clip = project.tracks[0].clips[0];
    clip.start = 1;
    clip.offset = 2;
    clip.rate = 3;
    clip.properties["position.x"].setKeyValue(0, 0);
    clip.properties["position.x"].setKeyValue(10, 10);
    Group animated;
    animated.id = 3;
    animated.properties["position.y"].setKey({0, 0, Interpolation::linear});
    animated.properties["position.y"].setKey({4, 4, Interpolation::linear});
    project.groups.push_back(animated);
    project.tracks[0].group = 3;
    frame = clipTransformFrame(project, 2, 2);
    check(frame.has_value() && near(frame->worldOrigin, {5, 4, 3}), "clip keys use offset/rate local time and ancestors use project time");

    auto effect = makeEffect(5, effectCatalog().front());
    effect.range = EffectRange {1, 2};
    project.tracks[0].clips[0].effects.push_back(effect);
    check(!clipTransformFrame(project, 2, 2)->hasPostTransformEffects, "pre-transform clip effects do not invalidate affine position mapping");
    project.tracks[0].effects.push_back(effect);
    check(clipTransformFrame(project, 2, 2)->hasPostTransformEffects, "active track effect is detected at project time");
    check(!clipTransformFrame(project, 2, 3)->hasPostTransformEffects, "effect range end is exclusive");
    project.tracks[0].effects[0].properties["strength"] = Curve(0);
    check(!clipTransformFrame(project, 2, 2)->hasPostTransformEffects, "zero strength is inactive");
    project.groups[0].effects.push_back(effect);
    check(clipTransformFrame(project, 2, 2)->hasPostTransformEffects, "ancestor effects are detected");
    project.groups[0].effects[0].enabled = false;
    project.effects.push_back(effect);
    check(clipTransformFrame(project, 2, 2)->hasPostTransformEffects, "composition effects are detected");

    check(!clipTransformFrame(project, 999, 0), "missing clip is unavailable");
    check(!clipTransformFrame(project, 2, std::numeric_limits<double>::infinity()), "nonfinite time is unavailable");
    check(!frame->positionDelta({std::numeric_limits<double>::infinity(), 0, 0}), "nonfinite drag delta is unavailable");
    project.groups[0].properties["scale.x"] = Curve(0);
    check(!clipTransformFrame(project, 2, 0), "singular ancestor scale is unavailable");
    project.groups[0].properties["scale.x"] = Curve(std::numeric_limits<double>::infinity());
    check(!clipTransformFrame(project, 2, 0), "nonfinite ancestor is unavailable");
    project.groups[0].properties["scale.x"] = Curve(1);
    project.groups[0].parent = 3;
    check(!clipTransformFrame(project, 2, 0), "cyclic ancestors are unavailable");
    project.groups[0].parent = 99;
    check(!clipTransformFrame(project, 2, 0), "missing ancestors are unavailable");
    project.tracks[0].kind = TrackKind::audio;
    check(!clipTransformFrame(project, 2, 0), "audio clips have no visual transform frame");
    project = fixture();
    project.tracks[0].group = 3;
    inner.properties["rotation.x"] = Curve(24);
    inner.properties["rotation.y"] = Curve(-37);
    outer.properties["rotation.z"] = Curve(16);
    outer.properties["scale.x"] = Curve(-1.7);
    project.groups = { inner, outer };
    auto& rotatedClip = project.tracks[0].clips[0];
    rotatedClip.properties["rotation.x"] = Curve(31);
    rotatedClip.properties["rotation.y"] = Curve(49);
    rotatedClip.properties["rotation.z"] = Curve(-23);
    rotatedClip.properties["scale.x"] = Curve(-2);
    auto gizmo = gizmoFrameForClip(project, 2, 0);
    check(gizmo.has_value() && gizmo->evaluatedScale.x == -2, "gizmo exposes evaluated negative scale for handle orientation");
    check(gizmo.has_value(), "mirrored nonuniform ancestors support Euler gizmos");
    constexpr double radians = std::numbers::pi / 180;
    check(near(gizmo->eulerRadians, {31 * radians, 49 * radians, -23 * radians}), "gizmo retains evaluated radians");
    for (int axis = 0; axis < 3; ++axis) {
        const Vec3 normal = axis == 0 ? Vec3 {1, 0, 0} : (axis == 1 ? Vec3 {0, 1, 0} : Vec3 {0, 0, 1});
        const auto worldAxis = gizmo->axisDirection(axis, true);
        check(worldAxis.has_value(), "rotation axis remains usable under mirrors");
        for (const auto angle : {0.0, 0.7, 2.9, -2.8}) {
            const auto ring = gizmo->rotationRingPoint(axis, angle, 0.8);
            check(ring.has_value(), "world ring point remains finite");
            const auto ray = gizmo->rotationRay({*ring + *worldAxis * 2, *worldAxis * -1}, axis);
            check(ray.has_value() && near(ray->direction, normal * -1), "inverse ray preserves direction and affine parameter");
            const auto hit = ray->origin + ray->direction * 2;
            const auto expected = axis == 0 ? Vec3 {0, std::cos(angle) * 0.8, std::sin(angle) * 0.8}
                : (axis == 1 ? Vec3 {std::sin(angle) * 0.8, 0, std::cos(angle) * 0.8} : Vec3 {std::cos(angle) * 0.8, std::sin(angle) * 0.8, 0});
            check(near(hit, expected), "inverse parent and post-Euler maps every ring back to canonical plane");
            const auto initial = axis == 0 ? Vec3 {0, 1, 0} : (axis == 1 ? Vec3 {0, 0, 1} : Vec3 {1, 0, 0});
            const auto signedAngle = std::atan2(normal.dot(initial.cross(hit)), initial.dot(hit));
            check(std::abs(signedAngle - angle) < 1.0e-9, "mirrored parents preserve canonical signed-angle orientation without a sign correction");
        }
    }
    const auto xScale = gizmo->axisDirection(0, false);
    const auto xRotate = gizmo->axisDirection(0, true);
    check(xScale.has_value() && near(*xScale, *xRotate), "X scale and rotation axis agree because Rx preserves X");
    const auto yScale = gizmo->axisDirection(1, false);
    const auto yRotate = gizmo->axisDirection(1, true);
    check(yScale.has_value() && yRotate.has_value() && !near(*yScale, *yRotate), "Y scale includes earlier Rx while Y parameter rotation excludes it");
    check(!gizmo->axisDirection(-1, true) && !gizmo->axisDirection(3, false), "invalid gizmo axes are unavailable");
    check(!gizmo->rotationRingPoint(0, 0, 0) && !gizmo->rotationRingPoint(0, 0, -1), "degenerate ring radii are rejected");
    check(!gizmo->rotationRay({{}, {}}, 0), "zero direction ray is rejected");
    check(!gizmo->rotationRingPoint(0, std::numeric_limits<double>::infinity(), 1), "nonfinite ring angles are rejected");
    rotatedClip.start = 1;
    rotatedClip.offset = 2;
    rotatedClip.rate = 3;
    rotatedClip.properties["rotation.x"].setKeyValue(0, 0);
    rotatedClip.properties["rotation.x"].setKeyValue(10, 90);
    gizmo = gizmoFrameForClip(project, 2, 2);
    check(gizmo.has_value() && std::abs(gizmo->eulerRadians.x - 45 * radians) < 1.0e-9, "gizmo Euler animation uses clip-local offset/rate time");
    check(!gizmoFrameForClip(project, 999, 0), "missing clip has no gizmo");
    std::cout << "Editor transform frame tests passed\n";
}
