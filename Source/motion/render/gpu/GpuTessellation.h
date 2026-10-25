#pragma once

#include "GpuChain.h"
#include "../HiddenLineDicing.h"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace motion::gpu {
// Faces something bends cut on the GPU into pieces flat enough to draw, as
// Dicer cuts them on the CPU, but all at once rather than piece by piece:
// each face is split into four, and on, wherever it is larger than Dicer
// grids on the screen of the view that sees it most finely (as placed: no
// bend is needed), so cells are as large as grids wherever they are; the
// GPU bends each cell's corners and edges' middles, measures how the bend
// curves it in the views that see it, and picks an even grid for it, as fine
// as that needs and never finer than a Dicer cell; then bends every grid's
// points. Cells cut as finely share their edges exactly.
class Tessellation {
public:
    // A face: its points before the chain, the chain's program (see
    // ChainBatch), its corners in the scene, and which surface's (by index)
    // and which of its faces it is, and whether that surface is solid.
    struct Face {
        std::array<osci::Point, 3> sources;
        std::uint32_t program;
        std::array<Vec3, 3> corners;
        std::uint32_t surface = 0, face = 0;
        bool solid = false;
    };
    // How many pieces a frame's faces may be cut into (as Dicer's budget).
    static constexpr std::size_t budget = hidden::Dicer::budget;
    // The most cells along a face's edge, and grid cuts along a cell's.
    static constexpr int mostCells = 64, mostCuts = 64;

    // As the kernels read them.
    struct GpuFace {
        std::array<float, 4> a, b, c, colourA, colourB, colourC, worldA, worldB, worldC;
        std::uint32_t program, padding0, cells, padding;
        std::uint32_t surface, face, flags, padding2;
    };
    struct GpuCell {
        std::uint32_t face, padding;
        std::array<std::int32_t, 6> corners;
    };
    // A frame's faces planned: each one's cells, how finely each cell is
    // cut, and where its points (`first`, `cuts`) and pieces (`firstPiece`)
    // are among all of them.
    struct Start {
        std::uint32_t first, cuts, firstPiece, padding;
    };
    struct Plan {
        std::vector<GpuFace> faces;
        std::vector<GpuCell> cells;
        std::vector<std::uint32_t> faceOf, cellsOf;
        std::vector<Start> starts;
        std::uint32_t points = 0, pieces = 0;
        std::vector<ChainBatch::Step> steps;
        std::vector<ChainBatch::Range> programs;
    };
    // Plans `faces` for `views` (one round trip); none when the device fails.
    static std::optional<Plan> plan(Device& device, const ChainBatch& chain, std::span<const Face> faces, std::span<const hidden::DiceView> views);
    // Records bending every planned cell's grid into `work`: the buffers
    // it makes, of the faces, cells, starts and bent points.
    struct Recorded {
        std::size_t faces, cells, starts, points;
    };
    static Recorded recordGrids(Device::Work& work, const Plan& plan);
    // The kernels' shared declarations (Face, Cell, Start, gridIndex,
    // latticeAt, sourceAt, colourAt), for another kernel reading a plan.
    static const char* sharedFunctions();

    struct GpuView {
        std::array<float, 4> position, right, up, forward;
        float focal;
        std::uint32_t parallel;
        float farthest, span, reachX, reachY, pixel, padding;
    };
};
}
