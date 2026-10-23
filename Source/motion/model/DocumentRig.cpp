// The Document's edits of cameras and their cuts, and of modulation: its
// modulators, routes and property links.

#include "Document.h"
#include "DocumentEdits.h"
#include "CompositionGraph.h"
#include "LuaClipBake.h"
#include "ModulationGraph.h"
#include "PropertySchema.h"
#include "../import/SourceDecoding.h"
#include <numbers>
#include <set>

namespace motion {

static bool hasCamera(const Project& project, Id id) {
    return std::any_of(project.cameras.begin(), project.cameras.end(), [id](const auto& camera) { return camera.id == id; });
}

static bool isVisualClipOrGroup(const Project& project, Id id, bool groupsOnly) {
    if (id == 0) { return true; }
    if (findGroup(project, id) != nullptr) { return true; }
    // A clip aimed at is a source's, as pruning keeps (a composition's
    // instance has no one place to aim at).
    return !groupsOnly && hasVisualClip(project, id);
}

// A cut at `time` ends the cut it falls in and lasts until the next cut.
static void insertCut(Project& updated, Id id, Id camera, double time) {
    auto end = updated.duration;
    for (const auto& cut : updated.cameraCuts) {
        if (cut.start > time) { end = std::min(end, cut.start); }
    }
    std::erase_if(updated.cameraCuts, [time](const auto& cut) { return cut.start == time; });
    for (auto& cut : updated.cameraCuts) {
        if (cut.start < time && cut.end() > time) { cut.duration = spanUntil(cut.start, time); }
    }
    updated.cameraCuts.push_back({id, camera, time, spanUntil(time, end)});
    std::sort(updated.cameraCuts.begin(), updated.cameraCuts.end(), [](const auto& left, const auto& right) { return left.start < right.start; });
}

juce::Result Document::cutToCamera(Id camera, double time, Id& cutId) {
    const auto& current = project();
    if (!hasCamera(current, camera)) { return juce::Result::fail("Choose a camera to cut to."); }
    if (!std::isfinite(time) || time < 0 || time >= current.duration) { return juce::Result::fail("Cuts must start inside the composition."); }
    cutId = newId();
    const auto id = cutId;
    tryEdit("Cut to camera", [time, camera, id](Project& updated) {
        insertCut(updated, id, camera, time);
        return true;
    });
    return juce::Result::ok();
}

juce::Result Document::setCutCamera(Id cut, Id camera) {
    if (!hasCamera(project(), camera)) { return juce::Result::fail("The camera no longer exists."); }
    const auto changed = tryEdit("Change cut camera", [cut, camera](Project& updated) {
        for (auto& item : updated.cameraCuts) {
            if (item.id == cut && item.camera != camera) {
                item.camera = camera;
                return true;
            }
        }
        return false;
    });
    return changed ? juce::Result::ok() : juce::Result::fail("The cut no longer exists or already shows that camera.");
}

juce::Result Document::applyCutRange(Project& project, Id cut, double start, double end) {
    const auto frame = project.frameRate > 0 ? 1.0 / project.frameRate : 1.0 / 30;
    const auto found = std::find_if(project.cameraCuts.begin(), project.cameraCuts.end(), [cut](const auto& item) { return item.id == cut; });
    if (found == project.cameraCuts.end()) { return juce::Result::fail("The cut no longer exists."); }
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end > project.duration + 1.0e-9 || end - start < frame - 1.0e-9) {
        return juce::Result::fail("A cut must stay inside the composition and last at least one frame.");
    }
    for (const auto& other : project.cameraCuts) {
        if (other.id != cut && start < other.end() && other.start < end) { return juce::Result::fail("Cuts can't overlap; trim the neighbouring cut first."); }
    }
    found->start = start;
    found->duration = spanUntil(start, std::min(end, project.duration));
    std::sort(project.cameraCuts.begin(), project.cameraCuts.end(), [](const auto& left, const auto& right) { return left.start < right.start; });
    return juce::Result::ok();
}

juce::Result Document::setCutRange(Id cut, double start, double end, juce::String label) {
    auto candidate = project();
    const auto result = applyCutRange(candidate, cut, start, end);
    if (result.failed()) { return result; }
    const auto found = std::find_if(project().cameraCuts.begin(), project().cameraCuts.end(), [cut](const auto& item) { return item.id == cut; });
    if (found->start == start && found->end() == std::min(end, project().duration)) { return juce::Result::ok(); }
    editCoalesced(label, "cut:" + juce::String(cut), [cut, start, end](Project& updated) { applyCutRange(updated, cut, start, end); });
    return juce::Result::ok();
}

juce::Result Document::removeCut(Id cut) {
    const auto removed = tryEdit("Delete cut", [cut](Project& updated) {
        const auto before = updated.cameraCuts.size();
        std::erase_if(updated.cameraCuts, [cut](const auto& item) { return item.id == cut; });
        return updated.cameraCuts.size() != before;
    });
    return removed ? juce::Result::ok() : juce::Result::fail("The cut no longer exists.");
}

juce::Result Document::addCamera(double time, Id& cameraId) {
    const auto& current = project();
    if (!std::isfinite(time) || time < 0 || time > current.duration) { return juce::Result::fail("Add cameras inside the composition."); }
    Camera camera;
    camera.id = cameraId = newId();
    // The camera on the output at `time` is the first camera unless a cut shows another.
    const Camera* showing = current.cameras.empty() ? nullptr : &current.cameras.front();
    for (const auto& cut : current.cameraCuts) {
        if (cut.contains(time)) {
            for (const auto& item : current.cameras) {
                if (item.id == cut.camera) { showing = &item; }
            }
        }
    }
    if (showing != nullptr) {
        for (const auto& [name, curve] : showing->properties) { camera.properties[name] = Curve(curve.evaluateBase(time)); }
        camera.target = showing->target;
        camera.parent = showing->parent;
    }
    for (int number = static_cast<int>(current.cameras.size()) + 1;; ++number) {
        camera.name = "Camera " + std::to_string(number);
        const auto taken = std::any_of(current.cameras.begin(), current.cameras.end(), [&camera](const auto& item) { return item.name == camera.name; });
        if (!taken) { break; }
    }
    const auto cutId = current.cameras.empty() || time >= current.duration ? Id(0) : newId();
    tryEdit("Add camera", [camera, cutId, time](Project& updated) {
        updated.cameras.push_back(camera);
        if (cutId != 0) { insertCut(updated, cutId, camera.id, time); }
        return true;
    });
    return juce::Result::ok();
}

juce::Result Document::removeCamera(Id camera) {
    const auto removed = tryEdit("Delete camera", [camera](Project& updated) {
        const auto before = updated.cameras.size();
        std::erase_if(updated.cameras, [camera](const auto& item) { return item.id == camera; });
        std::erase_if(updated.cameraCuts, [camera](const auto& cut) { return cut.camera == camera; });
        return updated.cameras.size() != before;
    });
    return removed ? juce::Result::ok() : juce::Result::fail("The camera no longer exists.");
}

juce::Result Document::setCameraRig(Id camera, Id target, Id parent) {
    const auto& current = project();
    if (!hasCamera(current, camera)) { return juce::Result::fail("The camera no longer exists."); }
    if (!isVisualClipOrGroup(current, target, false) || !isVisualClipOrGroup(current, parent, true)) { return juce::Result::fail("Aim at an object or group, and parent to a group."); }
    tryEdit("Change camera rig", [camera, target, parent](Project& updated) {
        for (auto& item : updated.cameras) {
            if (item.id == camera && (item.target != target || item.parent != parent)) {
                item.target = target;
                item.parent = parent;
                return true;
            }
        }
        return false;
    });
    return juce::Result::ok();
}

juce::Result Document::addModulator(Modulator modulator, Id& id) {
    modulator.id = newId();
    if (!modulator.valid()) { return juce::Result::fail("Invalid modulator settings."); }
    id = modulator.id;
    edit("Add modulator", [modulator](Project& project) { project.modulators.push_back(modulator); });
    return juce::Result::ok();
}

juce::Result Document::setModulator(Modulator modulator) {
    const auto& list = project().modulators;
    const auto found = std::find_if(list.begin(), list.end(), [&](const auto& item) { return item.id == modulator.id; });
    if (found == list.end()) { return juce::Result::fail("The modulator no longer exists."); }
    if (!modulator.valid()) { return juce::Result::fail("Invalid modulator settings."); }
    if (*found == modulator) { return juce::Result::ok(); }
    editCoalesced("Change modulator", "modulator:" + juce::String(modulator.id), [modulator](Project& project) {
        for (auto& item : project.modulators) {
            if (item.id == modulator.id) { item = modulator; }
        }
    });
    return juce::Result::ok();
}

juce::Result Document::removeModulator(Id id) {
    const auto removed = tryEdit("Delete modulator", [id](Project& project) {
        const auto before = project.modulators.size();
        std::erase_if(project.modulators, [id](const auto& item) { return item.id == id; });
        std::erase_if(project.routes, [id](const auto& route) { return route.modulator == id; });
        return project.modulators.size() != before;
    });
    return removed ? juce::Result::ok() : juce::Result::fail("The modulator no longer exists.");
}

static bool hasModulator(const Project& project, Id modulator) {
    return std::any_of(project.modulators.begin(), project.modulators.end(), [modulator](const auto& item) { return item.id == modulator; });
}

juce::Result Document::addRoute(ModulationRoute route, Id& id) {
    route.id = newId();
    const auto& current = project();
    if (!route.valid() || !hasModulator(current, route.modulator) || !drivableProperty(current, route.target, route.property)) { return juce::Result::fail("A route needs an existing modulator and a visual property."); }
    if (beamOutsideMain(route.target)) { return juce::Result::fail("Go back to the main composition to drive the Scope."); }
    id = route.id;
    edit("Route modulator", [route](Project& project) { project.routes.push_back(route); });
    return juce::Result::ok();
}

static double defaultRouteAmount(const std::string& property) {
    // A quarter unit keeps a moved or scaled object on the canvas; rotations
    // swing 45 degrees; colours move halfway.
    if (property.starts_with("rotation.")) { return 45.0; }
    if (property == "red" || property == "green" || property == "blue") { return 0.5; }
    return 0.25;
}

// A quarter of a property's range when it has one (a percentage moves by 25),
// rotations and colours as defaultRouteAmount says, and a quarter unit for
// the unbounded (a position or a scale).
double Document::routeAmount(const Project& project, Id target, const std::string& property) {
    const auto found = findPropertyTarget(project, target);
    const auto* beam = found.has_value() && found->beam ? findPropertySpec(beamPropertySpecs, property) : nullptr;
    if (beam != nullptr) { return 0.25 * (beam->maximum - beam->minimum); }
    const auto spec = specFor(project, target, property);
    const auto bounded = spec.has_value() && spec->minimum > -unbounded && spec->maximum < unbounded && spec->maximum > spec->minimum;
    if (bounded && !property.starts_with("rotation.") && property != "red" && property != "green" && property != "blue") { return 0.25 * (spec->maximum - spec->minimum); }
    return defaultRouteAmount(property);
}

// Routes from `modulator` to each of `properties` of `target` that it does
// not drive yet and that can be driven, each once, numbered by `nextId`.
template <typename NextId>
static std::vector<ModulationRoute> newRoutes(const Project& project, Id modulator, Id target, const std::vector<std::string>& properties, NextId&& nextId) {
    std::vector<ModulationRoute> added;
    const auto drives = [&](const std::vector<ModulationRoute>& routes, const std::string& property) {
        return std::any_of(routes.begin(), routes.end(), [&](const auto& route) { return route.modulator == modulator && route.target == target && route.property == property; });
    };
    for (const auto& property : properties) {
        if (drives(project.routes, property) || drives(added, property) || !drivableProperty(project, target, property)) { continue; }
        added.push_back({nextId(), modulator, target, property, Document::routeAmount(project, target, property), ModulationMode::add});
    }
    return added;
}

juce::Result Document::routeModulator(Id modulator, Id target, const std::vector<std::string>& properties) {
    const auto& current = project();
    if (!hasModulator(current, modulator)) { return juce::Result::fail("The modulator no longer exists."); }
    if (beamOutsideMain(target)) { return juce::Result::fail("Go back to the main composition to drive the Scope."); }
    const auto added = newRoutes(current, modulator, target, properties, [this] { return newId(); });
    if (added.empty()) { return juce::Result::fail("That modulator already drives it, or it can't be modulated."); }
    edit("Route modulator", [added](Project& project) { project.routes.insert(project.routes.end(), added.begin(), added.end()); });
    return juce::Result::ok();
}

juce::Result Document::moveEffect(Id effect, Id owner, bool copy) {
    if (findEffect(project(), effect) == nullptr) { return juce::Result::fail("The effect no longer exists."); }
    const auto copyId = copy ? newId() : Id(0);
    const auto moved = tryEdit(copy ? "Copy effect" : "Move effect", [&](Project& updated) { return motion::moveEffect(updated, effect, owner, copy, copyId); });
    return moved ? juce::Result::ok() : juce::Result::fail("The effect is already there, or that list has no room for more.");
}

std::optional<Project> Document::previewRoute(Id modulator, Id target, const std::vector<std::string>& properties) const {
    auto preview = project();
    if (!hasModulator(preview, modulator) || beamOutsideMain(target)) { return std::nullopt; }
    // Identities past every real one; the preview is never recorded.
    auto next = highestId();
    const auto added = newRoutes(preview, modulator, target, properties, [&next] { return ++next; });
    if (added.empty()) { return std::nullopt; }
    preview.routes.insert(preview.routes.end(), added.begin(), added.end());
    return preview;
}

juce::Result Document::addRoutedModulator(Modulator modulator, ModulationRoute route, Id& modulatorId) {
    modulator.id = newId();
    route.id = newId();
    route.modulator = modulator.id;
    if (!modulator.valid() || !route.valid() || !drivableProperty(project(), route.target, route.property)) {
        return juce::Result::fail("A route needs an existing visual property.");
    }
    if (beamOutsideMain(route.target)) { return juce::Result::fail("Go back to the main composition to drive the Scope."); }
    modulatorId = modulator.id;
    edit("Route new modulator", [modulator, route](Project& project) {
        project.modulators.push_back(modulator);
        project.routes.push_back(route);
    });
    return juce::Result::ok();
}

juce::Result Document::setRoute(const ModulationRoute& route) {
    const auto& list = project().routes;
    const auto found = std::find_if(list.begin(), list.end(), [&](const auto& item) { return item.id == route.id; });
    if (found == list.end()) { return juce::Result::fail("The route no longer exists."); }
    if (!route.valid() || found->modulator != route.modulator || found->target != route.target || found->property != route.property) {
        return juce::Result::fail("Only a route's amount and mode can change.");
    }
    if (*found == route) { return juce::Result::ok(); }
    editCoalesced("Change route", "route:" + juce::String(route.id), [route](Project& project) {
        for (auto& item : project.routes) {
            if (item.id == route.id) { item = route; }
        }
    });
    return juce::Result::ok();
}

juce::Result Document::removeRoute(Id id) {
    const auto removed = tryEdit("Remove route", [id](Project& project) {
        const auto before = project.routes.size();
        std::erase_if(project.routes, [id](const auto& route) { return route.id == id; });
        return project.routes.size() != before;
    });
    return removed ? juce::Result::ok() : juce::Result::fail("The route no longer exists.");
}

juce::Result Document::removeKeyframes(Id target, const std::vector<std::string>& properties, double projectTime, const juce::String& what) {
    const auto found = findPropertyTarget(project(), target);
    if (!found.has_value() || found->locked) { return juce::Result::fail("Those keyframes can't be removed."); }
    const auto local = found->localTime(project().frameTime(projectTime));
    const auto removed = tryEdit("Remove " + what + " keyframes", [&](Project& updated) {
        bool any = false;
        for (const auto& property : properties) {
            auto* curve = findPropertyCurve(updated, target, property);
            if (curve == nullptr || !curve->animated()) { continue; }
            curve->clearKeys(local);
            any = true;
        }
        return any;
    });
    return removed ? juce::Result::ok() : juce::Result::fail("There are no keyframes to remove.");
}

juce::Result Document::setLink(Id target, const std::string& property, std::optional<PropertyLink> link) {
    const auto& current = project();
    const auto* curve = findPropertyCurve(current, target, property);
    if (curve == nullptr) { return juce::Result::fail("The property no longer exists."); }
    if (link.has_value() && !drivableProperty(current, target, property)) { return juce::Result::fail("Audio clip gain and pan can't be linked."); }
    if (link.has_value()) {
        if (!link->valid() || !hasPropertyCurve(current, link->source, link->property)) { return juce::Result::fail("Choose an existing property to link to."); }
        // The Scope follows the main composition; nothing follows the Scope.
        if (link->source == current.beam.id) { return juce::Result::fail("Scope properties can't be linked to."); }
        if (beamOutsideMain(target)) { return juce::Result::fail("The Scope is linked from the main composition."); }
        if (linkCreatesCycle(current, target, property, *link)) { return juce::Result::fail("That link would make the property depend on itself."); }
    }
    if (curve->link == link) { return juce::Result::ok(); }
    const auto label = link.has_value() ? (curve->link.has_value() ? "Change property link" : "Link property") : "Unlink property";
    editCoalesced(label, "link:" + juce::String(target) + ":" + juce::String(property), [target, property, link](Project& project) {
        auto* edited = findPropertyCurve(project, target, property);
        if (edited != nullptr) { edited->link = link; }
    });
    return juce::Result::ok();
}
}
