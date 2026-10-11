#include "../../Source/motion/render/Reflections.h"
#include "../../Source/motion/render/SceneRays.h"
#include <cstdlib>
#include <iostream>
#include <random>
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

bool near(double a, double b, double within = 1.0e-9) { return std::abs(a - b) <= within; }

// The law of reflection at a point with a normal: the ways to the eye and to
// the point make equal angles with it, in one plane with it.
bool reflects(Vec3 seen, Vec3 normal, Vec3 eye, Vec3 point, double within = 1.0e-9) {
    const auto toEye = (eye - seen).normalized().value(), toPoint = (point - seen).normalized().value();
    const auto halfway = (toEye + toPoint).normalized().value();
    return near(normal.dot(toEye), normal.dot(toPoint), within) && halfway.cross(normal).length() < within * 10;
}

// A square of floor, 2 across, at y = 0, as a surface's corners and faces.
std::vector<std::optional<Vec3>> floorCorners() { return {Vec3 {-1, 0, -1}, Vec3 {1, 0, -1}, Vec3 {1, 0, 1}, Vec3 {-1, 0, 1}}; }
const std::vector<std::array<std::uint32_t, 3>> floorFaces {{0, 1, 2}, {0, 2, 3}};

// A ball as the sphere generator makes one: parallels and meridians.
void ball(Vec3 centre, double radius, std::vector<std::optional<Vec3>>& corners, std::vector<std::array<std::uint32_t, 3>>& faces) {
    constexpr int rings = 12, segments = 24;
    const auto pi = 3.14159265358979323846;
    for (int ring = 0; ring <= rings; ++ring) {
        for (int segment = 0; segment < segments; ++segment) {
            const auto latitude = pi * ring / rings - pi / 2, longitude = 2 * pi * segment / segments;
            corners.push_back(centre + Vec3 {std::cos(latitude) * std::cos(longitude), std::sin(latitude), std::cos(latitude) * std::sin(longitude)} * radius);
        }
    }
    for (std::uint32_t ring = 0; ring < rings; ++ring) {
        for (std::uint32_t segment = 0; segment < segments; ++segment) {
            const auto a = ring * segments + segment, b = ring * segments + (segment + 1) % segments;
            faces.push_back({a, b, b + segments});
            faces.push_back({a, b + segments, a + segments});
        }
    }
}
}

int main() {
    // A flat mirror: a floor seen from above and in front.
    {
        auto mirror = Mirror::of(floorCorners(), floorFaces);
        check(!mirror.sphere.has_value() && mirror.flats.size() == 1, "a floor is one flat mirror");
        const Vec3 eye {0, 2, 3};
        mirror.face(eye);
        const auto& flat = mirror.flats.front();
        check(near(flat.normal.y, 1) && flat.outline.size() == 4, "facing the eye, its outline its four sides");
        const Vec3 point {0, 1, -1};
        const auto seen = flat.reflectionOf(eye, point);
        check(seen.has_value() && near(seen->y, 0) && near(seen->z, 1.0 / 3), "a point above it is seen where the line to its image crosses it");
        check(reflects(*seen, flat.normal, eye, point), "with equal angles to the eye and the point");
        check(!flat.reflectionOf(eye, {0, -1, 0}).has_value(), "a point below it is not seen in it");
        check(flat.contains(*seen) && !flat.contains({1.5, 0, 0}), "the outline holds what it holds");
        // A line over it and beyond its far edge.
        const auto spans = flat.spans(eye, {0, .5, -3}, {0, .5, 0});
        check(spans.size() == 1, "a line partly over the mirror is seen in it once");
        const auto [from, to] = spans.front();
        const auto enters = flat.reflectionOf(eye, Vec3 {0, .5, -3} + Vec3 {0, 0, 3} * from);
        check(enters.has_value() && near(enters->z, -1, 1.0e-9) && near(to, 1), "from where its reflection crosses the far edge to its end");
        check(flat.spans(eye, {3, 1, 0}, {4, 1, 0}).empty(), "a line whose reflection misses the mirror is not seen in it");
        check(flat.spans(eye, {0, -1, 0}, {0, -2, 0}).empty(), "nor a line under it");
    }

    // A box is six flat mirrors; a ball, one round one.
    {
        std::vector<std::optional<Vec3>> corners {Vec3 {-1, -1, -1}, Vec3 {1, -1, -1}, Vec3 {1, 1, -1}, Vec3 {-1, 1, -1}, Vec3 {-1, -1, 1}, Vec3 {1, -1, 1}, Vec3 {1, 1, 1}, Vec3 {-1, 1, 1}};
        std::vector<std::array<std::uint32_t, 3>> faces;
        for (const auto& face : std::array<std::array<std::uint32_t, 4>, 6> {{{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}}}) {
            faces.push_back({face[0], face[1], face[2]});
            faces.push_back({face[0], face[2], face[3]});
        }
        const auto box = Mirror::of(corners, faces);
        check(!box.sphere.has_value() && box.flats.size() == 6, "a box is six flat mirrors");
        std::vector<std::optional<Vec3>> round;
        std::vector<std::array<std::uint32_t, 3>> roundFaces;
        ball({.3, -.2, .5}, .4, round, roundFaces);
        const auto sphere = Mirror::of(round, roundFaces);
        check(sphere.sphere.has_value() && near(sphere.sphere->radius, .4, 1.0e-9) && (sphere.sphere->centre - Vec3 {.3, -.2, .5}).length() < 1.0e-9, "a ball is the sphere through its corners");
        // A flat disc's rim lies on many spheres, and is none of them.
        std::vector<std::optional<Vec3>> disc {Vec3 {}};
        std::vector<std::array<std::uint32_t, 3>> discFaces;
        for (std::uint32_t step = 0; step < 32; ++step) {
            disc.push_back(Vec3 {std::cos(step * .19634954084936207), 0, std::sin(step * .19634954084936207)});
            discFaces.push_back({0, 1 + step, 1 + (step + 1) % 32});
        }
        check(!Mirror::of(disc, discFaces).sphere.has_value(), "a flat disc is flat");
    }

    // A round mirror obeys the law of reflection wherever it shows a point.
    {
        const SphereMirror sphere {{.2, .1, -.3}, .5};
        std::mt19937 random(7);
        std::uniform_real_distribution<double> spread(-3, 3);
        int seen = 0;
        for (int trial = 0; trial < 2000; ++trial) {
            const Vec3 eye {spread(random), spread(random), spread(random)}, point {spread(random), spread(random), spread(random)};
            const auto at = sphere.reflectionOf(eye, point);
            if (!at.has_value()) { continue; }
            ++seen;
            const auto normal = (*at - sphere.centre) / sphere.radius;
            check(near((*at - sphere.centre).length(), sphere.radius, 1.0e-12), "the reflection is on the sphere");
            check(reflects(*at, normal, eye, point, 1.0e-7), "with equal angles to the eye and the point");
        }
        check(seen > 1000, "most points round a ball are seen in it");
        check(!sphere.reflectionOf({.2, .1, -.3}, {2, 0, 0}).has_value(), "an eye inside it sees nothing in it");
        check(!sphere.reflectionOf({0, 0, 3}, {.2, .1, -.3}).has_value(), "a point inside it is not seen");
        // Straight back along the line through its middle.
        const auto back = sphere.reflectionOf({.2, .1, 3}, {.2, .1, 1});
        check(back.has_value() && near(back->z, .2, 1.0e-12), "a point between it and the eye is seen straight back");
        // Its rim: on it, and where the line from the eye only grazes it.
        const Vec3 eye {.4, 1.5, 2};
        const auto rim = sphere.rimFrom(eye);
        check(rim.has_value(), "a ball seen from outside has a rim");
        for (int step = 0; step < 16; ++step) {
            const auto at = rim->at(step * .4);
            check(near((at - sphere.centre).length(), sphere.radius, 1.0e-12) && near((at - eye).dot(at - sphere.centre), 0, 1.0e-12), "its rim is where the eye's lines graze it");
        }
        // Behind it and off to one side (straight behind is hidden by it).
        const auto edge = sphere.reflectionOf({.2, .1, 3}, {1.6, .1, -4});
        check(edge.has_value() && (*edge - sphere.centre).dot(Vec3 {0, 0, 1}) < .2 * sphere.radius, "a point behind it and aside is seen near its rim");
    }

    // What blocks a straight line between two points.
    {
        SceneRays rays;
        rays.build({{{-1, -1, 0}, {1, -1, 0}, {0, 1, 0}, 3}, {{5, 5, 5}, {6, 5, 5}, {5, 6, 5}, 4}});
        check(rays.blocked({0, 0, 1}, {0, 0, -1}, 0, 0), "a face between two points blocks them");
        check(!rays.blocked({0, 0, 1}, {0, 0, -1}, 0, 0, 3), "but not when it is a face of the surface left out");
        check(!rays.blocked({3, 0, 1}, {3, 0, -1}, 0, 0), "a line passing beside it is clear");
        check(!rays.blocked({0, 0, 1}, {0, 0, 0}, 0, 1.0e-6), "a point on the face is not hidden by it");
        check(!rays.blocked({0, 0, 0}, {0, 0, 1}, 1.0e-6, 0), "nor is a line leaving it");
        // Many faces in a tree, against testing each in turn.
        std::vector<SceneRays::Triangle> faces;
        std::mt19937 random(3);
        std::uniform_real_distribution<double> spread(-2, 2), small(-.3, .3);
        for (std::uint32_t index = 0; index < 500; ++index) {
            const Vec3 at {spread(random), spread(random), spread(random)};
            faces.push_back({at, at + Vec3 {small(random), small(random), small(random)}, at + Vec3 {small(random), small(random), small(random)}, index});
        }
        SceneRays many, one;
        many.build(faces);
        int agree = 0;
        for (int trial = 0; trial < 3000; ++trial) {
            const Vec3 from {spread(random), spread(random), spread(random)}, to {spread(random), spread(random), spread(random)};
            bool any = false;
            for (const auto& face : faces) {
                one.build({face});
                any = any || one.blocked(from, to, 0, 0);
            }
            agree += any == many.blocked(from, to, 0, 0) ? 1 : 0;
        }
        check(agree == 3000, "the tree finds just what testing each face finds");
    }

    std::cout << "ReflectionTests passed\n";
    return 0;
}
