#pragma once

#include "DrawingFill.h"
#include "GeneratorGeometry.h"
#include "PreparedSource.h"
#include <bit>
#include <list>
#include <unordered_map>

namespace motion::generator {
// A generator's geometry as a drawing: its strokes as lines and curves, and
// what hides behind it when it is solid (a solid's faces, or a filled
// shape's inside).
inline std::shared_ptr<const PreparedDrawing> drawingOf(const Geometry& geometry) {
    std::vector<std::unique_ptr<osci::Shape>> shapes;
    const auto point = [](const Vec3& value) { return osci::Point(static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)); };
    for (const auto& path : geometry.paths) {
        for (const auto& segment : path) {
            const auto& p = segment.points;
            // Curves are flat; a stroke out of the plane is made of lines.
            if (segment.curve && p[0].z == 0 && p[1].z == 0 && p[2].z == 0 && p[3].z == 0) {
                shapes.push_back(std::make_unique<osci::CubicBezierCurve>(static_cast<float>(p[0].x), static_cast<float>(p[0].y), static_cast<float>(p[1].x), static_cast<float>(p[1].y),
                    static_cast<float>(p[2].x), static_cast<float>(p[2].y), static_cast<float>(p[3].x), static_cast<float>(p[3].y)));
            } else {
                shapes.push_back(std::make_unique<osci::Line>(point(p[0]), point(p[3])));
            }
        }
    }
    auto drawing = std::make_shared<PreparedDrawing>(std::move(shapes));
    if (!geometry.triangles.empty()) {
        PreparedDrawing::Mesh mesh;
        mesh.vertices.reserve(geometry.vertices.size());
        for (const auto& vertex : geometry.vertices) { mesh.vertices.push_back(point(vertex)); }
        mesh.triangles = geometry.triangles;
        drawing->setFaces(std::move(mesh));
    } else if (geometry.filled) {
        drawing->setFaces(fill::of(*drawing));
    }
    return drawing;
}

// Drawings already made, by generator and values, so a value seen before (a
// still clip, a held key, an earlier preparation, another clip) is never made
// again. Shared by every thread that prepares compositions; the least recently
// used are let go past a budget of memory (an animated generator makes a
// drawing a frame).
class DrawingCache {
public:
    static DrawingCache& shared() {
        static DrawingCache cache;
        return cache;
    }

    // `values` settled, in the definition's order.
    std::shared_ptr<const PreparedDrawing> at(const GeneratorDefinition& definition, const std::vector<double>& values) {
        auto key = keyOf(definition, values);
        {
            const juce::SpinLock::ScopedLockType lock(guard);
            const auto found = entries.find(key);
            if (found != entries.end()) {
                order.splice(order.begin(), order, found->second.place);
                return found->second.drawing;
            }
        }
        auto made = drawingOf(build(definition, values));
        const auto size = made->bytes();
        // Those let go are freed once the lock is released.
        std::vector<std::shared_ptr<const PreparedDrawing>> released;
        const juce::SpinLock::ScopedLockType lock(guard);
        const auto found = entries.find(key);
        if (found != entries.end()) { return found->second.drawing; }
        order.push_front(key);
        entries.emplace(std::move(key), Entry {made, order.begin(), size});
        held += size;
        while (held > budget && entries.size() > 1) {
            const auto oldest = entries.find(order.back());
            held -= oldest->second.bytes;
            released.push_back(std::move(oldest->second.drawing));
            entries.erase(oldest);
            order.pop_back();
        }
        return made;
    }

private:
    static constexpr std::size_t budget = std::size_t {256} << 20;
    struct Entry {
        std::shared_ptr<const PreparedDrawing> drawing;
        std::list<std::string>::iterator place;
        std::size_t bytes;
    };
    std::size_t held = 0;
    static std::string keyOf(const GeneratorDefinition& definition, const std::vector<double>& values) {
        std::string key(definition.id);
        key.push_back(':');
        for (const auto value : values) {
            const auto bits = std::bit_cast<std::uint64_t>(value);
            key.append(reinterpret_cast<const char*>(&bits), sizeof(bits));
        }
        return key;
    }

    juce::SpinLock guard;
    std::unordered_map<std::string, Entry> entries;
    std::list<std::string> order;
};

// A generator clip's frames, one a frame of its content from its start to
// `contentEnd` at `frameRate` (fewer over long content: contentFrames), with
// `valuesAt(seconds)` its parameters' values then. Frames whose values match
// share a drawing, and a clip whose values never change has a single frame.
template <typename ValuesAt>
std::shared_ptr<const PreparedSource> clipSource(const GeneratorDefinition& definition, double contentEnd, double frameRate, ValuesAt&& valuesAt) {
    const auto [rate, count] = contentFrames(contentEnd, frameRate, 3600);
    std::vector<std::shared_ptr<const PreparedDrawing>> frames;
    frames.reserve(count);
    std::vector<double> previous;
    bool varies = false;
    for (std::size_t frame = 0; frame < count; ++frame) {
        auto values = settle(definition, valuesAt(static_cast<double>(frame) / rate));
        if (frame > 0 && values == previous) {
            frames.push_back(frames.back());
            continue;
        }
        varies = varies || frame > 0;
        frames.push_back(DrawingCache::shared().at(definition, values));
        previous = std::move(values);
    }
    if (!varies) { frames.resize(1); }
    return std::make_shared<PreparedSource>(std::move(frames), rate);
}
}
