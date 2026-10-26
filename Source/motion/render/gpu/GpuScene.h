#pragma once

#include "GpuTessellation.h"
#include "../HiddenLineCrossings.h"
#include "../Lighting.h"
#include <map>
#include <memory>

namespace motion::gpu {
// Lights' views kept on the GPU, for gpu::LightingBatch: every view's pixels
// and planes (inverse depth at the origin and slopes) in a row, and where
// each buffer's are.
struct Shadows {
    struct Layout {
        std::uint32_t firstPixel, firstPlane;
        bool empty;
    };
    std::shared_ptr<Device::Kept> pixels, planes;
    std::map<const hidden::DepthBuffer*, Layout> layouts;
};

// A frame's surfaces kept on the GPU, and the depth buffers its views draw
// of them there: every face, those something bends cut there (see
// Tessellation), as one list of triangles in the scene; each view's buffer
// drawn from it as DepthBuffer::triangle draws one a triangle at a time
// (cut at the near plane, a pixel taking the nearest triangle at its centre,
// the first drawn where two are as near), and kept with only the planes its
// pixels take. The camera's buffer is read back for what reads it on the
// CPU, with the solid triangles it sees for crossings found there, as are
// any others asked for; lights' buffers stay for lighting there (and are
// read back too when the CPU lights by them as well).
class Scene {
public:
    // A triangle as the kernels read it: its corners (in the scene, or an
    // eye's space once gathered), its points before the chain, its surface
    // and face, and its colours (packed four to a word).
    struct Triangle {
        std::array<std::array<float, 4>, 3> corners, sources;
        std::uint32_t surface, face, flags, padding;
        std::array<std::uint32_t, 4> colours;
    };
    enum Flags : std::uint32_t { solid = 1, bent = 2 };
    static Triangle triangle(const std::array<Vec3, 3>& corners, const std::array<osci::Point, 3>& sources, std::uint32_t surface, std::uint32_t face, std::uint32_t flags);

    // A view drawn: from where, how (and so `depth`'s layout), whether only
    // solid surfaces are drawn, and how far its frame reaches (a bent
    // surface's pieces wholly past it are dropped, as drawFlat drops them).
    struct View {
        hidden::Eye eye;
        hidden::Projection projection;
        hidden::Reach reach;
        hidden::DepthBuffer* depth = nullptr;
        bool solidOnly = false;
    };
    // What a frame draws: its faces (`flat` as placed; `bent` cut for
    // `diceViews`, through `chain`), and its views, all drawn together: the
    // camera's (none, or one), read back into its depth buffer, with the
    // solid triangles that show at all (see dropUnseen) when `gather`; `also`
    // others read back into theirs; and lights' views, kept on the GPU, and
    // read back into theirs too when `readShadows`.
    struct Frame {
        const ChainBatch* chain = nullptr;
        std::span<const Tessellation::Face> bent;
        const std::vector<Triangle>* flat = nullptr;
        std::span<const hidden::DiceView> diceViews;
        const View* camera = nullptr;
        bool gather = false;
        std::span<const View> also;
        std::span<const View> shadows;
        bool readShadows = false;
    };
    // What the camera's view gathered: each solid triangle that shows, in
    // its eye space, with its points before the chain; and the lights'
    // views, when there were any.
    struct Drawn {
        std::vector<hidden::crossing::Triangle> triangles;
        std::vector<std::array<osci::Point, 3>> sources;
        std::shared_ptr<const Shadows> shadows;
    };
    // None when the device fails.
    static std::optional<Drawn> draw(Device& device, const Frame& frame);

    // As the kernels read them.
    struct GpuView {
        std::array<float, 4> position, right, up, forward;
        float focal;
        std::uint32_t parallel;
        float farthest, span;
        float originX, originY, size;
        std::uint32_t solidOnly;
        std::uint32_t width, height, tilesAcross, tiles;
        std::uint32_t triangles, capacity, firstPixel, firstPlane;
        float reachX, reachY;
        std::uint32_t planeCapacity, writeLight;
        std::uint32_t statsBase, tileBase, padding[2];
    };
};
}
