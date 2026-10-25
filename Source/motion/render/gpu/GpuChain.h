#pragma once

#include "GpuDevice.h"
#include "../CompositionRenderer.h"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace motion::gpu {
// Points bent through their clips' chains on the GPU, as
// PreparedComposition::worldPoint bends them on the CPU: each clip's chain at
// a time (its colour, effects, transform, track, groups and composition
// effects, then its nesting's, then the composition's own) becomes a short
// program of steps whose values the CPU has already evaluated, and one kernel
// runs any program over any number of points.
class ChainBatch {
public:
    // One step of a program, as the kernel reads it: a kind, a copy (for an
    // effect that makes copies), an effect kind or flag, and its values.
    struct Step {
        std::uint32_t kind = 0, copy = 0, flag = 0, padding = 0;
        std::array<float, 24> values {};
    };
    enum StepKind : std::uint32_t { colourGains = 0, transform = 1, effect = 2 };

    // A clip's chain at a time, as a program; its index, for add.
    std::uint32_t program(const PreparedComposition& composition, const PreparedClip& clip, double time);
    // A point to bend through a program.
    void add(std::uint32_t program, const osci::Point& source);
    std::size_t size() const { return points.size(); }
    void clear();

    // Every point added, in the scene, in order (none where it isn't
    // finite); none when the device fails.
    std::optional<std::vector<std::optional<Vec3>>> run(Device& device) const;

    // The steps of a program (for tests).
    std::vector<Step> stepsOf(std::uint32_t program) const;

private:
    struct Range {
        std::uint32_t first, count;
    };
    struct Point {
        std::array<float, 3> position;
        std::uint32_t program;
        std::array<float, 4> colour;
    };
    std::vector<Step> steps;
    std::vector<Range> programs;
    std::vector<Point> points;
};
}
