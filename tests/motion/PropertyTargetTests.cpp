#include "../../Source/motion/model/PropertyTarget.h"
#include <cstdlib>
#include <iostream>

namespace {
struct Project {
    double duration = 120.0;
    std::vector<motion::Track> tracks;
    std::vector<motion::Camera> cameras;
};
void check(bool passed, const char* message) {
    if (!passed) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    Project project;
    motion::Clip clip;
    clip.id = 10;
    clip.start = 8;
    clip.duration = 6;
    clip.offset = 3;
    clip.rate = 2;
    clip.properties["position.x"] = motion::Curve(4);
    project.tracks.push_back({ 1, "Objects", { clip } });
    motion::Camera camera;
    camera.id = 20;
    camera.name = "Wide";
    project.cameras.push_back(camera);

    const auto& readOnly = project;
    const auto object = motion::findPropertyTarget(readOnly, 10);
    const auto view = motion::findPropertyTarget(readOnly, 20);
    static_assert(std::is_same_v<decltype(object->curve("position.x")), const motion::Curve*>);
    check(object.has_value() && !object->camera && object->localTime(10) == 7, "object target preserves trimmed/stretched content time");
    check(view.has_value() && view->camera && view->start == 0 && view->duration == 120 && view->localTime(10) == 10, "camera target uses full project time");
    check(view->name == "Wide" && view->properties == &project.cameras[0].properties, "camera target borrows its name and map");
    check(!motion::findPropertyTarget(readOnly, 0) && !motion::findPropertyTarget(readOnly, 999), "missing identities are rejected");
    check(motion::findPropertyCurve(readOnly, 20, "unknown") == nullptr, "missing properties do not grow maps");

    auto* curve = motion::findPropertyCurve(project, 20, "position.x");
    static_assert(std::is_same_v<decltype(curve), motion::Curve*>);
    curve->setKey({ 5, 2, motion::Interpolation::cubic, 3, 4 });
    curve->setKeyValue(5, 7);
    check(project.cameras[0].properties.at("position.x").evaluate(5) == 7, "camera mutation reaches original project storage");
    check(curve->keyframes()[0].outgoingSlope == 4 && curve->keyframes()[0].interpolation == motion::Interpolation::cubic, "camera value edits preserve authored tangents");
    check(project.tracks[0].clips[0].properties.at("position.x").base == 4, "camera mutation leaves object curves untouched");
    project.cameras.clear();
    check(motion::findPropertyCurve(project, 20, "position.x") == nullptr, "lookup after deletion does not retain a stale map");
    std::cout << "Property target contracts passed\n";
}
