#pragma once

#include "GpuChain.h"
#include "../Lighting.h"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace motion::gpu {
// Points along kept lines lit on the GPU, as hidden::lightAt lights them:
// each bent through its clip's chain (see ChainBatch), then lit by every
// light, through the lights' shadow buffers copied over. A sample belongs to
// a piece, which carries the faces it runs along and how far lights light it.
class LightingBatch {
public:
    // `chain` holds the programs samples are bent through, and outlives this.
    explicit LightingBatch(const ChainBatch& chain) : chain(chain) {}

    // The frame's lights, with their shadow views (whose buffers are copied).
    void setLights(const std::vector<hidden::LightView>& lights);
    // A piece: the faces it runs along, and its clip's response to light.
    std::uint32_t piece(const hidden::LineNormals& normals, float response);
    // A point of a piece, `t` along it, through a program of the chain.
    void sample(std::uint32_t program, std::uint32_t piece, const osci::Point& source, float t);
    std::size_t size() const { return samples.size(); }

    // Each sample's light (red, green, blue) and how much shadows took from
    // it (-1 where its point can't be placed), in order; none when the
    // device fails.
    std::optional<std::vector<std::array<float, 4>>> run(Device& device) const;

    struct Light {
        std::array<float, 4> position, direction, colour, shape;
        std::uint32_t firstView, viewCount, padding[2];
    };
    struct View {
        std::array<float, 4> position, right, up, forward;
        float focal;
        std::uint32_t parallel;
        float farthest, span;
        std::uint32_t buffer, padding[3];
    };
    struct Buffer {
        float originX, originY, size;
        std::uint32_t empty;
        std::int32_t width, height;
        std::uint32_t firstPixel, firstPlane;
    };
    struct Pixel {
        float w;
        std::int32_t owner;
    };
    struct Plane {
        float w, dwdx, dwdy, padding;
    };
    struct Piece {
        std::array<float, 4> first, second;
        float response, padding[3];
    };
    struct Sample {
        std::array<float, 3> source;
        std::uint32_t program, piece;
        float t, padding[2];
    };

private:
    const ChainBatch& chain;
    std::vector<Light> lights;
    std::vector<View> views;
    std::vector<Buffer> buffers;
    std::vector<Pixel> pixels;
    std::vector<Plane> planes;
    std::vector<Piece> pieces;
    std::vector<Sample> samples;
};
}
