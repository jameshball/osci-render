#pragma once

#include "../model/Document.h"
#include "PreparedEffects.h"
#include <array>
#include <numbers>

namespace motion {
inline osci::Point applyTransform(osci::Point point, const std::array<Curve, 13>& curves, double time) {
    point.scale(curves[6].evaluate(time), curves[7].evaluate(time), curves[8].evaluate(time));
    constexpr auto radians = std::numbers::pi / 180.0;
    point.rotate(curves[3].evaluate(time) * radians, curves[4].evaluate(time) * radians, curves[5].evaluate(time) * radians);
    point.translate(curves[0].evaluate(time), curves[1].evaluate(time), curves[2].evaluate(time));
    const auto sourceRed = point.r < 0 ? 1.0f : point.r;
    const auto sourceGreen = point.r < 0 ? 1.0f : point.g;
    const auto sourceBlue = point.r < 0 ? 1.0f : point.b;
    point.r = std::clamp(static_cast<float>(sourceRed * curves[9].evaluate(time)), 0.0f, 1.0f);
    point.g = std::clamp(static_cast<float>(sourceGreen * curves[10].evaluate(time)), 0.0f, 1.0f);
    point.b = std::clamp(static_cast<float>(sourceBlue * curves[11].evaluate(time)), 0.0f, 1.0f);
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)
        || !std::isfinite(point.r) || !std::isfinite(point.g) || !std::isfinite(point.b)) {
        return { 0, 0, 0, 0, 0, 0 };
    }
    return point;
}

struct PreparedGroup {
    Id id;
    std::array<Curve, 13> curves;
    std::vector<PreparedEffect> effects;

    explicit PreparedGroup(const Group& group) : id(group.id), effects(prepareEffects(group.effects)) {
        for (std::size_t index = 0; index < propertyNames.size(); ++index) {
            const auto found = group.properties.find(propertyNames[index]);
            curves[index] = found == group.properties.end() ? Curve(index >= 6 ? 1 : 0) : found->second;
        }
    }
    double weight(double time) const {
        const auto value = curves[12].evaluate(time);
        return std::isfinite(value) ? std::clamp(value, 0.0, 1000000.0) : 0.0;
    }
    osci::Point apply(osci::Point point, double time) const {
        return applyEffects(effects, applyTransform(point, curves, time), time);
    }
};

struct PreparedClip {
    Id id;
    double start, end, offset, rate;
    std::shared_ptr<const PreparedSource> source;
    std::array<Curve, 13> curves;
    std::vector<PreparedEffect> effects, trackEffects;
    std::vector<PreparedGroup> groups;

    double localTime(double time) const { return offset + (time - start) * rate; }
    bool active(double time) const { return time >= start && time < end; }
    double weight(double time) const {
        const auto value = curves[12].evaluate(localTime(time));
        double weight = std::isfinite(value) ? std::clamp(value, 0.0, 1000000.0) : 0.0;
        for (const auto& group : groups) {
            weight *= group.weight(time);
        }
        // At most 32 ancestors, each bounded to 1e6, keeps this product
        // below 1e198. Saturate only after outer attenuation is applied.
        return std::clamp(weight, 0.0, 1000000.0);
    }

    osci::Point sample(double time, double phase) const {
        const auto local = localTime(time);
        auto point = applyEffects(effects, source->sample(local, phase), local);
        point = applyTransform(point, curves, local);
        point = applyEffects(trackEffects, point, time);
        for (const auto& group : groups) {
            point = group.apply(point, time);
        }
        return point;
    }
};

struct PreparedCamera {
    Id id;
    std::array<Curve, 7> curves;

    osci::Point projectPoint(osci::Point point, double time) const {
        std::array<double, 7> values;
        for (std::size_t index = 0; index < values.size(); ++index) {
            values[index] = curves[index].evaluate(time);
            if (!std::isfinite(values[index])) {
                return { 0, 0, 0, 0, 0, 0 };
            }
        }
        const auto fieldOfView = values[6];
        // Cubic interpolation can overshoot otherwise valid FOV key values.
        if (fieldOfView <= 0.0 || fieldOfView >= 180.0) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        point.translate(-values[0], -values[1], -values[2]);
        constexpr auto radians = std::numbers::pi / 180.0;
        // Point::rotate applies X, then Y, then Z. Invert in reverse order;
        // negating all three angles in a single rotate call is not the inverse.
        point.rotate(0.0f, 0.0f, -values[5] * radians);
        point.rotate(0.0f, -values[4] * radians, 0.0f);
        point.rotate(-values[3] * radians, 0.0f, 0.0f);
        const auto depth = -point.z;
        if (!std::isfinite(depth) || depth <= 0.05f) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        const auto focalLength = 1.0 / std::tan(fieldOfView * radians * 0.5);
        point.x *= focalLength / depth;
        point.y *= focalLength / depth;
        point.z = 1.0f;
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        return point;
    }
};

struct PreparedComposition {
    explicit PreparedComposition(const Project& project) : duration(project.duration), effects(prepareEffects(project.effects)) {
        for (const auto& camera : project.cameras) {
            PreparedCamera item { camera.id, {} };
            const Camera defaults;
            for (std::size_t index = 0; index < cameraPropertyNames.size(); ++index) {
                const auto found = camera.properties.find(cameraPropertyNames[index]);
                item.curves[index] = found != camera.properties.end() ? found->second : defaults.properties.at(cameraPropertyNames[index]);
            }
            cameras.push_back(std::move(item));
        }
        for (const auto& cut : project.cameraCuts) {
            const auto camera = std::find_if(cameras.begin(), cameras.end(), [&](const auto& item) { return item.id == cut.camera; });
            if (cut.valid() && camera != cameras.end()) {
                cameraCuts.push_back({ cut.start, cut.end(), static_cast<std::size_t>(camera - cameras.begin()) });
            }
        }
        std::sort(cameraCuts.begin(), cameraCuts.end(), [](const auto& left, const auto& right) { return left.start < right.start; });
        for (const auto& track : project.tracks) {
            if (!trackIsAudible(project, track)) {
                continue;
            }
            for (const auto& clip : track.clips) {
                const auto asset = std::find_if(project.assets.begin(), project.assets.end(),
                    [&](const auto& item) { return item->id == clip.asset; });
                if (asset == project.assets.end() || ((*asset)->source == nullptr && (*asset)->drawing == nullptr)) {
                    continue;
                }
                auto source = (*asset)->source;
                if (source == nullptr) {
                    source = std::make_shared<PreparedSource>(std::vector<std::shared_ptr<const osci::PreparedDrawing>> { (*asset)->drawing }, 30.0);
                }
                PreparedClip item { clip.id, clip.start, clip.end(), clip.offset, clip.rate, std::move(source), {} };
                for (std::size_t i = 0; i < propertyNames.size(); ++i) {
                    const auto curve = clip.properties.find(propertyNames[i]);
                    item.curves[i] = curve != clip.properties.end() ? curve->second : Curve(i >= 6 ? 1.0 : 0.0);
                }
                item.effects = prepareEffects(clip.effects);
                item.trackEffects = prepareEffects(track.effects);
                auto groupId = track.group;
                while (groupId != 0 && item.groups.size() < maximumGroupDepth) {
                    const auto* group = findGroup(project, groupId);
                    if (group == nullptr) {
                        break;
                    }
                    item.groups.emplace_back(*group);
                    groupId = group->parent;
                }
                clips.push_back(std::move(item));
            }
        }
    }

    osci::Point sample(double time, double phase) const {
        double allocation = 0.0;
        for (const auto& clip : clips) {
            if (clip.active(time)) {
                allocation += std::max(1.0, clip.weight(time));
            }
        }
        if (allocation <= 0.0) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        auto cursor = phase * allocation;
        for (const auto& clip : clips) {
            if (!clip.active(time)) {
                continue;
            }
            const auto weight = clip.weight(time);
            if (cursor < weight) {
                return projectPoint(clip.sample(time, cursor / weight), time);
            }
            cursor -= weight;
        }
        // Unused allocation is explicitly dark; normalizing it away would
        // cancel a fade when the composition has only one visible object.
        return { 0, 0, 0, 0, 0, 0 };
    }

    const PreparedCamera* activeCamera(double time) const {
        if (cameras.empty()) {
            return nullptr;
        }
        const auto next = std::upper_bound(cameraCuts.begin(), cameraCuts.end(), time,
            [](double value, const auto& cut) { return value < cut.start; });
        if (next != cameraCuts.begin()) {
            const auto& cut = *(next - 1);
            if (time >= cut.start && time < cut.end) {
                return &cameras[cut.cameraIndex];
            }
        }
        return &cameras.front();
    }

    osci::Point applyCompositionEffects(osci::Point point, double time) const {
        return applyEffects(effects, point, time);
    }

    osci::Point projectPoint(osci::Point point, double time) const {
        if (!std::isfinite(time) || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        point = applyCompositionEffects(point, time);
        const auto* camera = activeCamera(time);
        if (camera != nullptr) {
            return camera->projectPoint(point, time);
        }
        // Empty camera collections retain the original fixed output framing.
        const auto depth = 4.0f - point.z;
        if (depth <= 0.05f) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        point.x *= 4.0f / depth;
        point.y *= 4.0f / depth;
        point.z = 1.0f;
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        return point;
    }

    double duration;
    std::vector<PreparedClip> clips;
    std::vector<PreparedCamera> cameras;

private:
    struct PreparedCameraCut {
        double start, end;
        std::size_t cameraIndex;
    };
    std::vector<PreparedCameraCut> cameraCuts;
    std::vector<PreparedEffect> effects;
};
}
