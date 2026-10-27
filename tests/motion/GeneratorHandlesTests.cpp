#include "../../Source/motion/model/GeneratorHandles.h"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace motion;
using namespace motion::generator;

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::vector<double> defaults(const GeneratorDefinition& definition) {
    std::vector<double> values;
    for (const auto& spec : definition.parameters) { values.push_back(spec.defaultValue); }
    return values;
}

bool same(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) { return false; }
    for (std::size_t index = 0; index < a.size(); ++index) {
        if (std::abs(a[index] - b[index]) > 1.0e-9) { return false; }
    }
    return true;
}

void everyGeneratorHasHandles() {
    for (const auto& definition : generatorDefinitions) {
        const auto values = settle(definition, defaults(definition));
        const auto list = handles(definition, values);
        const auto name = std::string(definition.name);
        check(!list.empty(), name + " has handles");
        for (std::size_t index = 0; index < list.size(); ++index) {
            const auto& handle = list[index];
            check(handle.at.finite(), name + "'s handles sit at finite points");
            check(!handle.label.empty(), name + "'s handles are named");
            if (handle.kind != Handle::Kind::point) {
                check(handle.count.has_value(), name + "'s count handles step a count");
                continue;
            }
            check(!handle.parameters.empty(), name + "'s point handles set something");
            // Dropped where it is, a handle changes nothing.
            check(same(drag(definition, index, values, handle.at, handle.at, false), values), name + "'s " + handle.label + " handle changes nothing left where it is");
            if (handle.axis.has_value()) { check(std::abs(handle.axis->length() - 1) < 1.0e-12, name + "'s axes are unit length"); }
        }
        // Every count can be stepped from the shape.
        for (std::size_t parameter = 0; parameter < definition.parameters.size(); ++parameter) {
            const auto& spec = definition.parameters[parameter];
            if (spec.decimals != 0 || spec.step < 1 || !spec.unit.empty()) { continue; }
            const auto stepped = std::any_of(list.begin(), list.end(), [&](const Handle& handle) { return handle.count == parameter; });
            check(stepped, name + "'s " + std::string(spec.label) + " can be stepped on the shape");
        }
    }
}

void dragsSetWhatTheySay() {
    const auto& star = *findGenerator("star");
    const auto values = settle(star, defaults(star));
    // The tip twice as far out doubles the outer radius, only.
    const auto tip = handles(star, values)[0];
    const auto pulled = drag(star, 0, values, tip.at, tip.at * 2, false);
    check(std::abs(pulled[1] - 2 * values[1]) < 1.0e-9 && pulled[2] == values[2] && pulled[0] == values[0], "a star's tip sets its outer radius alone");
    check(step(star, 0, values, 2)[0] == values[0] + 2, "a star steps its points");
    check(step(star, 0, values, -100)[0] == star.parameters[0].minimum, "and stops at the fewest");

    const auto& rectangle = *findGenerator("rectangle");
    const auto box = settle(rectangle, std::vector<double> {1, .5, 0});
    const auto wider = drag(rectangle, 0, box, {.5, .25, 0}, {.8, .25, 0}, false);
    check(std::abs(wider[0] - 1.6) < 1.0e-9 && std::abs(wider[1] - .5) < 1.0e-9, "a rectangle's corner sets its size");
    const auto kept = drag(rectangle, 0, box, {.5, .25, 0}, {1, .3, 0}, true);
    check(std::abs(kept[0] / kept[1] - 2) < 1.0e-9, "with Shift it keeps its proportions");
    // Half the shorter side is .25: the handle starts two fifths of that in,
    // and at the far end of its travel the corner is fully round.
    const auto sharp = handles(rectangle, box)[1].at;
    check(std::abs(sharp.x - (.5 - .1)) < 1.0e-9, "a sharp corner's roundness handle sits a little way in");
    const auto round = drag(rectangle, 1, box, sharp, {.5 - .25, .25, 0}, false);
    check(std::abs(round[2] - 100) < 1.0e-6, "dragged as far as the cut can reach, the corner is fully round");

    const auto& arc = *findGenerator("arc");
    const auto open = settle(arc, std::vector<double> {.5, 0, 90});
    const auto further = drag(arc, 1, open, {0, .5, 0}, {-.5, 0, 0}, false);
    check(std::abs(further[2] - 180) < 1.0e-9 && further[1] == 0, "an arc's end sweeps it on, its start held");
    const auto later = drag(arc, 0, open, {.5, 0, 0}, {0, .5, 0}, false);
    check(std::abs(later[1] - 90) < 1.0e-9 && std::abs(later[2]) < 1.0e-9, "its start moves toward the end, the end held");

    const auto& cube = *findGenerator("box");
    const auto sizes = settle(cube, std::vector<double> {1, 1, 1});
    check(std::abs(drag(cube, 2, sizes, {0, 0, .5}, {0, 0, .9}, false)[2] - 1.8) < 1.0e-9, "a box's face sets its depth");

    // A spot light's cone handle out from its axis widens the cone; its soft
    // edge's handle in from the rim softens it; its intensity's along its axis.
    const auto& spot = *findGenerator("spotLight");
    const auto lit = settle(spot, defaults(spot));
    const auto spotHandles = handles(spot, lit);
    const auto cone = spotHandles[0].at;
    const auto widened = drag(spot, 0, lit, cone, cone + Vec3 {0, .1, 0}, false);
    check(widened[2] > lit[2] && widened[0] == lit[0], "a spot light's cone handle dragged out widens its cone alone");
    const auto edge = spotHandles[1].at;
    const auto softer = drag(spot, 1, lit, edge, edge + Vec3 {0, .05, 0}, false);
    check(softer[3] > lit[3] && softer[2] == lit[2], "its soft edge's handle dragged toward the axis softens it");
    const auto brighter = drag(spot, 2, lit, spotHandles[2].at, spotHandles[2].at + Vec3 {0, 0, -.06}, false);
    check(std::abs(brighter[0] - (lit[0] + 1)) < 1.0e-9, "its intensity's handle along its axis brightens it, a step a unit");
    const auto& point = *findGenerator("pointLight");
    const auto bulb = settle(point, defaults(point));
    const auto ray = handles(point, bulb)[0].at;
    check(drag(point, 0, bulb, ray, ray + Vec3 {0, .06, 0}, false)[0] > bulb[0], "a point light's rays drawn out brighten it");

    const auto& hexGrid = *findGenerator("hexGrid");
    const auto cells = settle(hexGrid, defaults(hexGrid));
    const auto handle = handles(hexGrid, cells)[0];
    check(std::abs(drag(hexGrid, 0, cells, handle.at, handle.at * 1.5, false)[2] - 1.5 * cells[2]) < 1.0e-9, "a hex grid's cell handle scales its cells");
}
}

int main() {
    everyGeneratorHasHandles();
    dragsSetWhatTheySay();
    std::cout << "Generator handle tests passed\n";
    return 0;
}
