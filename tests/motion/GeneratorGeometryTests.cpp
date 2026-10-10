#include "../../Source/motion/model/GeneratorGeometry.h"
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

namespace {
using namespace motion;
using namespace motion::generator;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool near(Vec3 a, Vec3 b, double tolerance = 1.0e-9) { return (a - b).length() <= tolerance; }

std::vector<double> defaults(const GeneratorDefinition& definition) {
    std::vector<double> values;
    for (const auto& spec : definition.parameters) { values.push_back(spec.defaultValue); }
    return values;
}

Geometry built(std::string_view id, std::vector<double> values = {}) {
    const auto* definition = findGenerator(id);
    check(definition != nullptr, std::string("a ") + std::string(id) + " generator exists");
    return build(*definition, values.empty() ? defaults(*definition) : values);
}

void everyGeneratorDraws() {
    std::set<std::string_view> ids;
    for (const auto& definition : generatorDefinitions) {
        check(ids.insert(definition.id).second, "generator ids are unique");
        const auto geometry = build(definition, defaults(definition));
        check(!geometry.paths.empty(), std::string(definition.name) + " draws something at its defaults");
        for (const auto& path : geometry.paths) {
            check(!path.empty(), std::string(definition.name) + " draws no empty strokes");
            for (std::size_t index = 0; index < path.size(); ++index) {
                for (const auto& point : path[index].points) { check(point.finite(), std::string(definition.name) + " draws only finite points"); }
                if (index > 0) { check(near(path[index - 1].points[3], path[index].points[0], 1.0e-6), std::string(definition.name) + "'s strokes run end to end"); }
            }
            if (geometry.filled) { check(near(path.front().points[0], path.back().points[3], 1.0e-6), std::string(definition.name) + "'s filled strokes close"); }
        }
        for (const auto& triangle : geometry.triangles) {
            for (const auto corner : triangle) { check(corner < geometry.vertices.size(), std::string(definition.name) + "'s faces use its vertices"); }
        }
        const auto hides = definition.category == GeneratorCategory::solid || definition.category == GeneratorCategory::grid;
        check(geometry.triangles.empty() != hides, std::string(definition.name) + " has faces exactly when it is a solid or a grid");
        for (const auto& spec : definition.parameters) {
            const auto light = definition.category == GeneratorCategory::light;
            check(spec.id.starts_with(light ? "light." : "generator."), "parameters are named generator.*, a light's light.*");
            check(spec.contains(spec.defaultValue), std::string(spec.label) + " starts in range");
        }
    }
}

void settlesItsNumbers() {
    const auto* star = findGenerator("star");
    const auto settled = settle(*star, std::vector<double> {5.6, 9.0, -1.0});
    check(settled.size() == star->parameters.size(), "every parameter gets a value");
    check(settled[0] == 6, "counts are whole");
    check(settled[1] == 2, "values stay in range");
    check(settled[2] == 0, "values stay in range below too");
    check(settled[3] == 0, "a missing value takes its default");
}

void shapesAreWhatTheySay() {
    // A five-pointed star: ten corners, its first point straight up.
    const auto star = built("star");
    check(star.paths.size() == 1 && star.paths.front().size() == 10, "a star is one stroke of ten edges");
    check(near(star.paths.front().front().points[0], {0, .5, 0}), "a star points up");
    // Rounding turns each corner into a curve.
    const auto rounded = built("star", {5, .5, .2, 50});
    check(rounded.paths.front().size() == 20, "a rounded star has a curve at each corner and an edge between");
    // A ring's inner edge turns the other way, which makes the hole.
    const auto ring = built("ring");
    check(ring.paths.size() == 2, "a ring has two edges");
    const auto turn = [](const Path& path) {
        double area = 0;
        for (const auto& segment : path) { area += segment.points[0].x * segment.points[3].y - segment.points[3].x * segment.points[0].y; }
        return area;
    };
    check(turn(ring.paths[0]) * turn(ring.paths[1]) < 0, "a ring's edges turn opposite ways");
    // An 8 x 8 grid has nine lines each way.
    const auto grid = built("grid");
    check(grid.paths.size() == 18, "a grid has its rows and columns plus one each");
    for (std::size_t index = 1; index < grid.paths.size(); ++index) {
        check((grid.paths[index].front().points[0] - grid.paths[index - 1].back().points[3]).length() < .25, "each grid line starts near where the last ended");
    }
    // Hex cells share their edges: none is drawn twice.
    const auto hex = built("hexGrid", {2, 1, .5});
    std::size_t edges = 0;
    for (const auto& path : hex.paths) { edges += path.size(); }
    check(edges == 11, "two hexagons side by side share one edge");
    // A box: eight corners, twelve faces' worth of triangles, twelve edges.
    const auto box = built("box");
    check(box.vertices.size() == 8 && box.triangles.size() == 12, "a box has eight corners and twelve triangles");
    std::size_t boxEdges = 0;
    for (const auto& path : box.paths) { boxEdges += path.size(); }
    check(boxEdges == 12, "a box has twelve edges, each once");
    // A spirograph closes: it ends where it began.
    const auto spirograph = built("spirograph");
    check(near(spirograph.paths.front().front().points[0], spirograph.paths.front().back().points[3], 1.0e-6), "a spirograph closes");
    // The Lissajous figure fits its size.
    const auto lissajous = built("lissajous");
    for (const auto& segment : lissajous.paths.front()) { check(std::abs(segment.points[0].x) <= .5 + 1.0e-9 && std::abs(segment.points[0].y) <= .5 + 1.0e-9, "a Lissajous figure stays within its size"); }
}
}

// The area a geometry's faces cover, flat as a grid's are.
double faceArea(const Geometry& geometry) {
    double area = 0;
    for (const auto& triangle : geometry.triangles) {
        const auto& a = geometry.vertices[triangle[0]];
        const auto& b = geometry.vertices[triangle[1]];
        const auto& c = geometry.vertices[triangle[2]];
        area += std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) / 2;
    }
    return area;
}

void gridsAreSolid() {
    // Each grid's face covers just what its lines span.
    check(std::abs(faceArea(built("grid", {4, 3, 1.2, .9})) - 1.2 * .9) < 1.0e-9, "a grid is solid across its width and height");
    const auto disc = faceArea(built("polarGrid", {3, 8, .8}));
    check(std::abs(disc - 3.14159265358979323846 * .64) < .002, "a polar grid is a solid disc of its radius");
    const auto cell = 3 * std::sqrt(3.0) / 2 * .1 * .1;
    check(std::abs(faceArea(built("hexGrid", {3, 2, .1})) - 6 * cell) < 1.0e-9, "a hex grid is solid in each of its cells");
}

int main() {
    everyGeneratorDraws();
    settlesItsNumbers();
    shapesAreWhatTheySay();
    gridsAreSolid();
    std::cout << "Generator geometry tests passed\n";
    return 0;
}
