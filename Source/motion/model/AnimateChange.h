#pragma once

#include "Document.h"
#include "PropertySchema.h"
#include <string>
#include <vector>

namespace motion {

// A value an edit changed on a property that wasn't animated: what it was
// and what it became.
struct StaticChange {
    Id target = 0;
    std::string property;
    double before = 0, after = 0;
};

// Every target with properties: effects, groups, tracks' effects, clips and
// theirs, cameras and the Scope.
template <typename Visit>
void forEachPropertyTarget(const Project& project, Visit&& visit) {
    for (const auto& effect : project.effects) { visit(effect.id); }
    for (const auto& group : project.groups) {
        visit(group.id);
        for (const auto& effect : group.effects) { visit(effect.id); }
    }
    for (const auto& track : project.tracks) {
        for (const auto& effect : track.effects) { visit(effect.id); }
        for (const auto& clip : track.clips) {
            visit(clip.id);
            for (const auto& effect : clip.effects) { visit(effect.id); }
        }
    }
    for (const auto& camera : project.cameras) { visit(camera.id); }
    visit(project.beam.id);
}

// What an edit changed without animating it, on targets it neither moved
// nor retimed (a clip moved changes no value of its own).
inline std::vector<StaticChange> staticChanges(const Project& before, const Project& after) {
    std::vector<StaticChange> changes;
    forEachPropertyTarget(after, [&](Id id) {
        const auto now = findPropertyTarget(after, id);
        const auto was = findPropertyTarget(before, id);
        if (!now.has_value() || !was.has_value() || now->properties == nullptr || was->properties == nullptr) { return; }
        if (now->start != was->start || now->duration != was->duration || now->offset != was->offset || now->rate != was->rate) { return; }
        for (const auto& [property, curve] : *now->properties) {
            if (curve.animated()) { continue; }
            // A value never stored before was its default.
            const auto* old = was->curve(property);
            const auto spec = old == nullptr ? specFor(before, id, property) : std::nullopt;
            if ((old == nullptr && !spec.has_value()) || (old != nullptr && old->animated())) { continue; }
            const auto previous = old != nullptr ? old->base : spec->defaultValue;
            if (previous != curve.base) { changes.push_back({id, property, previous, curve.base}); }
        }
    });
    return changes;
}

// Where a change can become an animation at `time` (project time): inside
// its target's span, past its start.
inline bool canAnimateAt(const Project& project, const StaticChange& change, double time) {
    const auto target = findPropertyTarget(project, change.target);
    if (!target.has_value() || target->locked || target->properties == nullptr) { return false; }
    return time > target->start + 1.0e-6 && time <= target->end() + 1.0e-6;
}

// The changes as animations: the value before keyed at the target's start,
// the new one at `time`, eased as keys are by default. A few whole-number
// choices (a mode, a switch) hold until `time`. The rest of a changed axis's
// group (Position's other axes) is keyed where it stands at both times, as
// its key diamond keys them together. False when none could be.
inline bool animateChanges(Project& project, const std::vector<StaticChange>& changes, double time) {
    std::vector<std::pair<Id, PropertySpec>> grouped;
    bool any = false;
    double from = 0, to = 0;
    const auto span = [&](Id id) {
        const auto target = findPropertyTarget(project, id);
        from = target->localTime(target->start);
        to = target->localTime(time);
        return to > from;
    };
    for (const auto& change : changes) {
        if (!canAnimateAt(project, change, time) || !span(change.target)) { continue; }
        auto* curve = findPropertyCurve(project, change.target, change.property);
        if (curve == nullptr || curve->animated() || curve->base != change.after) { continue; }
        const auto spec = specFor(project, change.target, change.property);
        const auto stepped = spec.has_value() && spec->decimals == 0 && spec->step >= 1 && spec->maximum - spec->minimum <= 16;
        Keyframe first {from, change.before};
        if (stepped) { first.interpolation = Interpolation::hold; }
        curve->setKey(first);
        curve->setKeyValue(to, change.after);
        any = true;
        if (spec.has_value() && !spec->axis.empty()) { grouped.emplace_back(change.target, *spec); }
    }
    for (const auto& [id, spec] : grouped) {
        if (!span(id)) { continue; }
        for (const auto& sibling : propertySpecs(*findPropertyTarget(std::as_const(project), id))) {
            if (sibling.group != spec.group || sibling.axis.empty() || sibling.id == spec.id) { continue; }
            auto* other = ensurePropertyCurve(project, id, sibling.id);
            if (other == nullptr || other->animated()) { continue; }
            const auto value = other->base;
            other->setKey({from, value});
            other->setKeyValue(to, value);
        }
    }
    return any;
}

}
