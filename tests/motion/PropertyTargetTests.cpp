#include "../../Source/motion/model/PropertyTarget.h"
#include <cstdlib>
#include <iostream>

namespace {
struct Project {
    double duration = 120.0;
    double bpm = 120.0;
    motion::Tempo tempo() const { return motion::Tempo(bpm); }
    std::vector<motion::Track> tracks;
    std::vector<motion::Camera> cameras;
    std::vector<motion::EffectInstance> effects;
    std::vector<motion::Group> groups;
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
    auto localEffect = motion::makeEffect(30, *motion::effectDefinition("ripple"));
    check(localEffect.valid(), "factory populates every bounded parameter");
    project.tracks[0].clips[0].effects.push_back(localEffect);
    auto trackEffect = motion::makeEffect(31, *motion::effectDefinition("bulge"));
    project.tracks[0].effects.push_back(trackEffect);
    project.effects.push_back(motion::makeEffect(32, *motion::effectDefinition("swirl")));
    const auto localTarget = motion::findPropertyTarget(readOnly, 30);
    const auto trackTarget = motion::findPropertyTarget(readOnly, 31);
    const auto globalTarget = motion::findPropertyTarget(readOnly, 32);
    check(localTarget->isEffect && !localTarget->camera && localTarget->localTime(10) == 7, "clip effects inherit slip/stretch clocks");
    check(trackTarget->isEffect && globalTarget->isEffect && trackTarget->localTime(10) == 10 && globalTarget->localTime(10) == 10, "track and composition effects use project time");
    check(motion::findEffectOwner(project, 10) == &project.tracks[0].clips[0].effects, "clip effect ownership lookup");
    check(motion::findEffectOwner(project, 1) == &project.tracks[0].effects, "track effect ownership lookup");
    check(motion::findEffectOwner(project, 0) == &project.effects, "composition effect ownership lookup");
    check(motion::findEffectOwner(project, 999) == nullptr, "unknown owners cannot receive effects");
    motion::findPropertyCurve(project, 30, "strength")->setKey({ 7, 0.25 });
    check(motion::findEffect(readOnly, 30)->properties.at("strength").evaluate(7) == 0.25, "effect graph edits update owner storage");
    auto invalidEffect = localEffect;
    invalidEffect.range = motion::EffectRange { 1, -1 };
    check(!invalidEffect.valid(), "negative effect duration is rejected");
    invalidEffect.range = motion::EffectRange { -1, 2 };
    check(invalidEffect.valid(), "negative clip-local range is supported");
    invalidEffect.properties["rippleDepth"] = motion::Curve(2);
    check(!invalidEffect.valid(), "out-of-range effect values are rejected");
    const auto split = project.tracks[0].clips[0].split(10, 40, motion::Tempo(120));
    check(split.has_value() && split->second.effects.size() == 1, "split retains an independent effect stack value");
    auto right = split->second;
    right.effects[0].id = 41;
    right.effects[0].properties["strength"] = motion::Curve(0.75);
    check(project.tracks[0].clips[0].effects[0].properties.at("strength").evaluate(7) == 0.25, "split effect curves remain independent");
    motion::Group outer;
    outer.id = 50;
    motion::Group inner;
    inner.id = 51;
    inner.parent = 50;
    inner.effects.push_back(motion::makeEffect(52, *motion::effectDefinition("scale")));
    project.groups = { outer, inner };
    project.tracks[0].group = 51;
    check(motion::validGroupHierarchy(project), "nested group references are valid");
    const auto groupTarget = motion::findPropertyTarget(readOnly, 51);
    check(groupTarget->isGroup && !groupTarget->isEffect && groupTarget->localTime(10) == 10, "group curves use project time");
    check(motion::findPropertyTarget(readOnly, 52)->isEffect, "group effects are graph targets");
    check(motion::findEffectOwner(project, 51) == &project.groups[1].effects, "group effect owner lookup");
    project.groups[0].solo = true;
    check(motion::trackIsAudible(project, project.tracks[0]), "ancestor solo includes descendants");
    project.groups[0].muted = true;
    project.tracks[0].solo = true;
    check(!motion::trackIsAudible(project, project.tracks[0]), "ancestor mute wins over track solo");
    project.groups[0].parent = 51;
    check(!motion::validGroupHierarchy(project), "group cycles are rejected");
    std::cout << "Property target contracts passed\n";
}
