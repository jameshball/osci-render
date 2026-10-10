#pragma once

#include "CompositionRenderer.h"
#include <vector>

namespace motion::hidden {
// Everything a frame's hidden lines and lighting depend on at a time, as
// numbers: the active camera and its view, each light, then for each clip
// whether it plays and shows, which drawing of its source it shows, and every
// pose and effect setting along its chain (its colour aside: that is applied
// as the beam draws, after what is baked). Two times with equal
// fingerprints cull to the same frame, so a bake can reuse one for the other:
// a still stretch of a project costs one frame.
class Fingerprint {
public:
    void take(const PreparedComposition& composition, double time) {
        values.clear();
        const auto* camera = composition.activeCamera(time);
        add(camera != nullptr ? static_cast<double>(camera - composition.cameras.data()) : -1.0);
        if (camera != nullptr) {
            add(camera->hidesHiddenLines ? 1.0 : 0.0);
            const auto view = camera->frame(time);
            if (view.has_value()) {
                for (const auto& axis : {view->position, view->right, view->up, view->forward}) { add(axis.x, axis.y, axis.z); }
                add(view->focalLength);
            }
        }
        addEffects(composition.compositionEffects(), time);
        // Each light that shines, as it shines: its colour lights what is baked.
        for (const auto& light : composition.lights) {
            add(light.clip.active(time) ? 1.0 : 0.0);
            const auto state = composition.lightAt(light, time);
            if (!state.has_value()) { continue; }
            add(static_cast<double>(state->kind), state->position.x, state->position.y, state->position.z, state->direction.x, state->direction.y, state->direction.z);
            add(state->colour[0], state->colour[1], state->colour[2], state->intensity, state->range, state->cone, state->softness, state->shadows);
        }
        for (const auto& clip : composition.clips) {
            const auto active = clip.active(time);
            add(active ? 1.0 : 0.0);
            if (!active) { continue; }
            add(clip.visible(time) ? 1.0 : 0.0, clip.liveIdentity != nullptr ? 1.0 : 0.0);
            // The drawing it shows (frames that repeat one are the same), or
            // for a point source the frame.
            const auto frame = clip.source != nullptr ? clip.source->frameIndex(clip.localTime(time)) : 0;
            const auto* drawing = clip.source != nullptr ? clip.source->drawingAt(frame) : nullptr;
            add(drawing != nullptr ? static_cast<double>(reinterpret_cast<std::uintptr_t>(drawing)) : -2.0 - static_cast<double>(frame));
            addStage(clip, time);
            for (const auto& ancestor : clip.ancestors) { addStage(ancestor, time); }
        }
    }
    bool operator==(const Fingerprint& other) const { return values == other.values; }

private:
    template <typename... Values>
    void add(Values... more) { (values.push_back(static_cast<double>(more)), ...); }
    void addPose(const TransformPose& pose) {
        for (std::size_t axis = 0; axis < 3; ++axis) { add(pose.scale[axis], pose.translation[axis], pose.anchor[axis]); }
        for (const auto& row : pose.rotation) { add(row[0], row[1], row[2]); }
    }
    void addEffects(const std::vector<PreparedEffect>& effects, double time) {
        for (const auto& effect : effects) {
            const auto parameters = effect.parametersAt(time);
            add(parameters.active ? 1.0 : 0.0);
            if (!parameters.active) { continue; }
            add(static_cast<double>(effect.kind), parameters.wobble);
            for (std::size_t index = 0; index < effect.count; ++index) { add(parameters.values[index]); }
        }
    }
    void addStage(const PreparedClipStage& stage, double time) {
        const auto local = stage.localTime(time), scope = stage.scopeTime(time);
        addPose(TransformPose::at(stage.curves, local, stage.spatial.get()));
        addEffects(stage.effects, local);
        addEffects(stage.trackEffects, scope);
        for (const auto& group : stage.groups) {
            addPose(TransformPose::at(group.curves, scope, group.spatial.get()));
            addEffects(group.effects, scope);
        }
        addEffects(stage.compositionEffects, scope);
    }

    std::vector<double> values;
};
}
