#pragma once

#include "Document.h"
#include "PropertySchema.h"
#include "Vec3.h"
#include <array>
#include <cmath>
#include <string>

// Editing an object's anchor: the point (in its own space) that rotation and
// scale turn about and that lands on its position.
namespace motion::anchor {
// Moves a curve's value by `delta`: its base when still, every key when
// `everyKey`, otherwise a key at `time`.
inline void shiftCurve(Curve& curve, double delta, double time, bool everyKey) {
    if (delta == 0) { return; }
    if (!curve.animated()) {
        curve.base += delta;
    } else if (everyKey) {
        const auto keys = curve.keyframes();
        for (const auto& key : keys) { curve.setKeyValue(key.time, key.value + delta); }
    } else {
        curve.setKeyValue(time, curve.evaluateBase(time) + delta);
    }
}

// Moves the anchor by `local` (the object's own space) and the position by
// `parent` (its parent's space) together, so the object stays where it is,
// as After Effects' Pan Behind does. A still anchor carries every position
// key with it; an animated one is keyed here, with the position.
inline bool shift(Project& project, Id id, double time, Vec3 parent, Vec3 local) {
    if (!parent.finite() || !local.finite()) { return false; }
    std::array<Curve*, 3> anchors {}, positions {};
    bool anchorAnimated = false;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto name = std::string(1, "xyz"[axis]);
        anchors[axis] = ensurePropertyCurve(project, id, "anchor." + name);
        positions[axis] = ensurePropertyCurve(project, id, "position." + name);
        if (anchors[axis] == nullptr || positions[axis] == nullptr) { return false; }
        anchorAnimated = anchorAnimated || anchors[axis]->animated();
    }
    const std::array<double, 3> byParent {parent.x, parent.y, parent.z}, byLocal {local.x, local.y, local.z};
    bool changed = false;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        changed = changed || std::abs(byParent[axis]) > 1.0e-12 || std::abs(byLocal[axis]) > 1.0e-12;
        shiftCurve(*anchors[axis], byLocal[axis], time, false);
        shiftCurve(*positions[axis], byParent[axis], time, !anchorAnimated);
    }
    return changed;
}
}
