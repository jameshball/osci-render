#include "../../Source/motion/render/Lighting.h"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace motion;
using namespace motion::hidden;

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool nearby(double a, double b, double within = 1.0e-9) { return std::abs(a - b) <= within; }

LightView lightOf(GeneratorKind kind, Vec3 position, Vec3 direction = {0, 0, -1}) {
    LightView light;
    light.state.kind = kind;
    light.state.position = position;
    light.state.direction = direction;
    return light;
}

double red(const std::vector<LightView>& lights, Vec3 at, LineNormals normals = {}) { return illumination(lights, at, normals)[0]; }

// A square of surface drawn into a light's view, as two triangles.
void square(ShadowView& view, std::array<Vec3, 4> corners) {
    std::array<Screen, 4> at {};
    for (std::size_t index = 0; index < 4; ++index) {
        const auto point = view.projection(view.eye.see(corners[index]));
        check(point.has_value(), "the square is in view");
        at[index] = *point;
    }
    view.depth->triangle(at[0], at[1], at[2]);
    view.depth->triangle(at[0], at[2], at[3]);
}

void falloff() {
    auto point = lightOf(GeneratorKind::pointLight, {});
    point.state.range = 4;
    point.state.intensity = 2;
    const std::vector<LightView> lights {point};
    check(nearby(red(lights, {1.0e-6, 0, 0}), 2, 1.0e-6), "a point light is at its intensity where it is");
    check(red(lights, {4, 0, 0}) == 0 && red(lights, {9, 0, 0}) == 0, "and nothing at its range and past it");
    double last = 3;
    for (double distance = .5; distance < 4; distance += .5) {
        const auto value = red(lights, {0, distance, 0});
        check(value < last && value > 0, "it fades smoothly with distance");
        last = value;
    }
}

void cone() {
    auto spot = lightOf(GeneratorKind::spotLight, {}, {0, 0, -1});
    spot.state.cone = 60;
    spot.state.softness = .5;
    spot.state.range = 100;
    const std::vector<LightView> lights {spot};
    const auto at = [](double degrees) {
        const auto radians = degrees * 3.14159265358979323846 / 180;
        return Vec3 {std::sin(radians), 0, -std::cos(radians)};
    };
    const auto full = red(lights, at(0));
    check(full > .99, "a spot light shines fully down its middle");
    check(nearby(red(lights, at(14), {}), full, 1.0e-3), "and fully inside its inner cone");
    const auto edge = red(lights, at(22));
    check(edge > 0 && edge < full, "it fades across its soft edge");
    check(red(lights, at(31)) == 0 && red(lights, Vec3 {0, 0, 1}) == 0, "and nothing outside its cone or behind it");
    spot.state.softness = 0;
    const std::vector<LightView> hard {spot};
    check(red(hard, at(29)) > .99 && red(hard, at(31)) == 0, "with no softness its edge is sharp");
}

void facing() {
    const std::vector<LightView> lights {lightOf(GeneratorKind::directionalLight, {}, {0, -1, 0})};
    LineNormals up;
    up.add(Vec3 {0, 1, 0});
    LineNormals side;
    side.add(Vec3 {1, 0, 0});
    LineNormals edge;
    edge.add(Vec3 {0, 1, 0});
    edge.add(Vec3 {1, 0, 0});
    check(nearby(red(lights, {}, up), 1), "a face square on to a light takes all of it");
    check(nearby(red(lights, {}, side), 0), "one edge on to it, none");
    check(nearby(red(lights, {}, edge), .5), "an edge between them half");
    LineNormals down;
    down.add(Vec3 {0, -1, 0});
    check(nearby(red(lights, {}, down), 1), "either side of a face takes it");
    check(nearby(red(lights, {}), 1), "a line on no face takes it whichever way it comes");
    auto ambient = lightOf(GeneratorKind::ambientLight, {});
    ambient.state.intensity = .25;
    ambient.state.colour = {1, .5, 0};
    const auto fill = illumination({ambient}, {3, 4, 5}, side);
    check(nearby(fill[0], .25) && nearby(fill[1], .125) && nearby(fill[2], 0), "an ambient light is its colour everywhere, however faces face");
}

void parallelShadow() {
    // A unit square a metre above the ground, under a light shining straight down.
    DepthBuffer depth;
    ShadowView view {Eye::looking({0, 5, 0}, {0, -1, 0}, {0, 0, -1}), Projection {.5, true}, &depth};
    view.projection.farthest = 10;
    view.projection.span = 10;
    square(view, {Vec3 {-1, 1, -1}, Vec3 {1, 1, -1}, Vec3 {1, 1, 1}, Vec3 {-1, 1, 1}});
    auto light = lightOf(GeneratorKind::directionalLight, {}, {0, -1, 0});
    light.views.push_back(view);
    check(light.reaching({0, 0, 0}) == 0, "under the square is in its shadow");
    check(light.reaching({.5, 1, .5}) == 1, "the square itself is lit");
    check(light.reaching({1, 1, 0}) == 1, "and its edges");
    check(light.reaching({0, 2, 0}) == 1, "above it is lit");
    check(light.reaching({1.6, 0, 0}) == 1, "beside its shadow is lit");
    light.state.shadows = .25;
    check(nearby(light.reaching({0, 0, 0}), .75), "a weaker shadow lets some through");
}

void cubeShadow() {
    // A point light at the middle, a square on its right.
    std::array<DepthBuffer, 6> buffers {DepthBuffer({}, 512), DepthBuffer({}, 512), DepthBuffer({}, 512), DepthBuffer({}, 512), DepthBuffer({}, 512), DepthBuffer({}, 512)};
    auto light = lightOf(GeneratorKind::pointLight, {});
    const auto eyes = cubeEyes({});
    for (std::size_t face = 0; face < 6; ++face) { light.views.push_back({eyes[face], Projection {1}, &buffers[face]}); }
    square(light.views[0], {Vec3 {1, -.5, -.5}, Vec3 {1, .5, -.5}, Vec3 {1, .5, .5}, Vec3 {1, -.5, .5}});
    check(light.reaching({2, 0, 0}) == 0 && light.reaching({3, .4, -.4}) == 0, "behind the square from a point light is in its shadow");
    check(light.reaching({1, .2, .2}) == 1, "the square itself is lit");
    check(light.reaching({-2, 0, 0}) == 1 && light.reaching({0, 3, 0}) == 1 && light.reaching({2, 0, 3}) == 1, "everywhere else is lit");
}
}

int main() {
    falloff();
    cone();
    facing();
    parallelShadow();
    cubeShadow();
    std::cout << "Lighting tests passed\n";
    return 0;
}
