#include "GpuCrossings.h"

#include <algorithm>
#include <cstring>

namespace motion::gpu {
namespace {
constexpr const char* kernel = R"(
struct Triangle { corners: array<vec4<f32>, 6>, surface: u32, padding0: u32, padding1: u32, padding2: u32 }
struct Box { left: f32, right: f32, bottom: f32, top: f32, nearby: f32, farthest: f32, padding0: f32, padding1: f32 }
struct Cover { x0: i32, x1: i32, y0: i32, y1: i32 }
struct Grid { cells: u32, padding0: u32, padding1: u32, padding2: u32 }
struct Found { count: atomic<u32>, padding0: u32, padding1: u32, padding2: u32, pairs: array<vec4<u32>> }

@group(0) @binding(0) var<storage, read> triangles: array<Triangle>;
@group(0) @binding(1) var<storage, read> boxes: array<Box>;
@group(0) @binding(2) var<storage, read> covers: array<Cover>;
@group(0) @binding(3) var<storage, read> cellOf: array<u32>;
@group(0) @binding(4) var<storage, read> starts: array<u32>;
@group(0) @binding(5) var<storage, read> members: array<u32>;
@group(0) @binding(6) var<uniform> grid: Grid;
@group(0) @binding(7) var<storage, read_write> found: Found;

// Boxes overlap, widened a little so single precision never parts two that do.
fn overlaps(p: Box, q: Box) -> bool {
    let slack = 1.0e-5 * max(max(max(abs(p.left), abs(p.right)), max(abs(p.bottom), abs(p.top))), 1.0);
    let depth = 1.0e-5 * max(max(abs(p.nearby), abs(p.farthest)), 1.0);
    return !(p.right + slack < q.left || q.right + slack < p.left || p.top + slack < q.bottom || q.top + slack < p.bottom || p.farthest + depth < q.nearby || q.farthest + depth < p.nearby);
}

// Corner `k` less corner `m` of the same or another triangle: each corner is
// held as a high and a low part (its double split in two), so a difference
// keeps the precision of the triangles' size, not of their distance.
fn less(t: Triangle, k: u32, u: Triangle, m: u32) -> vec3<f32> {
    return (t.corners[k].xyz - u.corners[m].xyz) + (t.corners[k + 3u].xyz - u.corners[m + 3u].xyz);
}

// crossing::through, from a triangle's corners' distances to a plane, as
// shares of its corners along its two cut points; `ambiguous` when a
// distance is too near the tolerance for single precision to classify it
// as the CPU does.
struct Through { found: bool, ambiguous: bool, first: vec3<f32>, second: vec3<f32> }
fn through(d: vec3<f32>, tolerance: f32, band: f32) -> Through {
    var result: Through;
    var side = vec3<i32>(0);
    for (var k = 0; k < 3; k++) {
        if (abs(abs(d[k]) - tolerance) < band) { result.ambiguous = true; return result; }
        side[k] = select(select(0, -1, d[k] < -tolerance), 1, d[k] > tolerance);
    }
    let on = i32(side.x == 0) + i32(side.y == 0) + i32(side.z == 0);
    if (on == 0) {
        for (var corner = 0; corner < 3; corner++) {
            let next = (corner + 1) % 3;
            let last = (corner + 2) % 3;
            if (side[corner] != side[next] && side[corner] != side[last]) {
                let t1 = d[corner] / (d[corner] - d[next]);
                let t2 = d[corner] / (d[corner] - d[last]);
                var first = vec3<f32>(0.0);
                var second = vec3<f32>(0.0);
                first[corner] = 1.0 - t1;
                first[next] = t1;
                second[corner] = 1.0 - t2;
                second[last] = t2;
                result.found = true;
                result.first = first;
                result.second = second;
                return result;
            }
        }
        return result;
    }
    if (on == 1) {
        for (var corner = 0; corner < 3; corner++) {
            if (side[corner] != 0) { continue; }
            let next = (corner + 1) % 3;
            let last = (corner + 2) % 3;
            if (side[next] == side[last]) { return result; }
            let t = d[next] / (d[next] - d[last]);
            var first = vec3<f32>(0.0);
            var second = vec3<f32>(0.0);
            first[corner] = 1.0;
            second[next] = 1.0 - t;
            second[last] = t;
            result.found = true;
            result.first = first;
            result.second = second;
            return result;
        }
    }
    return result;
}

// Whether two triangles may cut through each other, as crossing::between
// tests them: false only where single precision is sure the CPU finds no
// crossing.
fn mayCross(first: Triangle, second: Triangle) -> bool {
    let abA = less(first, 1u, first, 0u);
    let acA = less(first, 2u, first, 0u);
    let abB = less(second, 1u, second, 0u);
    let acB = less(second, 2u, second, 0u);
    let normalA = cross(abA, acA);
    let normalB = cross(abB, acB);
    if (!(length(normalA) > 0.0) || !(length(normalB) > 0.0)) { return true; }
    let unitA = normalize(normalA);
    let unitB = normalize(normalB);
    let size = max(max(length(abA), length(acA)), max(length(abB), length(acB)));
    let tolerance = size * 1.0e-6;
    // Single precision's error in a distance, at the triangles' size.
    let band = size * 2.0e-6;
    let toA = vec3<f32>(dot(unitA, less(second, 0u, first, 0u)), dot(unitA, less(second, 1u, first, 0u)), dot(unitA, less(second, 2u, first, 0u)));
    let toB = vec3<f32>(dot(unitB, less(first, 0u, second, 0u)), dot(unitB, less(first, 1u, second, 0u)), dot(unitB, less(first, 2u, second, 0u)));
    let cutA = through(toB, tolerance, band);
    if (cutA.ambiguous) { return true; }
    if (!cutA.found) { return false; }
    let cutB = through(toA, tolerance, band);
    if (cutB.ambiguous) { return true; }
    if (!cutB.found) { return false; }
    // Both cuts lie on the line the planes meet on: where they overlap along
    // it, the triangles cross. Positions are taken from the first
    // triangle's first corner.
    let direction = cross(unitA, unitB);
    let offsetB = less(second, 0u, first, 0u);
    let atA = vec2<f32>(dot(direction, cutA.first.y * abA + cutA.first.z * acA), dot(direction, cutA.second.y * abA + cutA.second.z * acA));
    let atB = vec2<f32>(dot(direction, offsetB + cutB.first.y * abB + cutB.first.z * acB), dot(direction, offsetB + cutB.second.y * abB + cutB.second.z * acB));
    let low = max(min(atA.x, atA.y), min(atB.x, atB.y));
    let high = min(max(atA.x, atA.y), max(atB.x, atB.y));
    let needed = tolerance * length(direction);
    return high - low > needed - band * 4.0;
}

// One entry of a cell: its triangle against the later members there, as
// crossing::detail::eachPair pairs them.
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let entry = id.x + id.y * groups.x * 64u;
    if (entry >= arrayLength(&members)) { return; }
    let cell = cellOf[entry];
    let x = i32(cell % grid.cells);
    let y = i32(cell / grid.cells);
    let i = members[entry];
    let mine = triangles[i];
    let box = boxes[i];
    let cover = covers[i];
    for (var k = entry + 1u; k < starts[cell + 1u]; k++) {
        let j = members[k];
        let theirs = triangles[j];
        if (theirs.surface <= mine.surface) { continue; }
        // Each pair once: in the cell where their boxes' overlap starts.
        let other = covers[j];
        if (max(cover.x0, other.x0) != x || max(cover.y0, other.y0) != y) { continue; }
        if (!overlaps(box, boxes[j]) || !mayCross(mine, theirs)) { continue; }
        let at = atomicAdd(&found.count, 1u);
        if (at < arrayLength(&found.pairs)) { found.pairs[at] = vec4<u32>(cell, i, j, 0u); }
    }
}
)";

struct Triangle {
    std::array<std::array<float, 4>, 6> corners;
    std::uint32_t surface, padding[3];
};
struct Box {
    float left, right, bottom, top, nearby, farthest, padding[2];
};
}

std::optional<std::vector<std::array<std::uint32_t, 2>>> mayCross(Device& device, const std::vector<hidden::crossing::Triangle>& triangles, const hidden::crossing::detail::Pairing& pairing) {
    const auto& grid = *pairing.grid;
    std::vector<Triangle> corners;
    std::vector<Box> boxes;
    std::vector<std::array<std::int32_t, 4>> covers;
    corners.reserve(triangles.size());
    boxes.reserve(triangles.size());
    covers.reserve(triangles.size());
    // A double as two floats, high and low, whose sum is it to about twice
    // single precision.
    const auto high = [](const Vec3& value) { return std::array<float, 4> {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z), 0}; };
    const auto low = [](const Vec3& value) { return std::array<float, 4> {static_cast<float>(value.x - static_cast<float>(value.x)), static_cast<float>(value.y - static_cast<float>(value.y)), static_cast<float>(value.z - static_cast<float>(value.z)), 0}; };
    for (std::size_t index = 0; index < triangles.size(); ++index) {
        const auto& triangle = triangles[index];
        corners.push_back({{high(triangle.eye[0]), high(triangle.eye[1]), high(triangle.eye[2]), low(triangle.eye[0]), low(triangle.eye[1]), low(triangle.eye[2])}, triangle.surface, {}});
        const auto& box = pairing.boxes[index];
        boxes.push_back({static_cast<float>(box.left), static_cast<float>(box.right), static_cast<float>(box.bottom), static_cast<float>(box.top), static_cast<float>(box.nearby), static_cast<float>(box.farthest), {}});
        const auto range = grid.range(box);
        covers.push_back({range[0], range[1], range[2], range[3]});
    }
    std::vector<std::uint32_t> starts(grid.starts().begin(), grid.starts().end());
    const auto members = grid.members();
    // Each entry's cell, for a kernel run once an entry.
    std::vector<std::uint32_t> cellOf(members.size());
    for (std::size_t cell = 0; cell + 1 < starts.size(); ++cell) { std::fill(cellOf.begin() + starts[cell], cellOf.begin() + starts[cell + 1], static_cast<std::uint32_t>(cell)); }
    const std::array<std::uint32_t, 4> uniforms {static_cast<std::uint32_t>(grid.across()), 0, 0, 0};
    const std::array<Device::Input, 6> inputs {std::as_bytes(std::span(corners)), std::as_bytes(std::span(boxes)), std::as_bytes(std::span(covers)), std::as_bytes(std::span(cellOf)), std::as_bytes(std::span(starts)), std::as_bytes(members)};
    // Room for a few pairs a candidate; when more are found, again with room for all.
    auto capacity = pairing.candidates.size() * 6 + 1024;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto output = device.run(kernel, inputs, std::as_bytes(std::span(uniforms)), 16 + capacity * 16, static_cast<std::uint32_t>(members.size()));
        if (!output.has_value()) { return std::nullopt; }
        std::uint32_t count = 0;
        std::memcpy(&count, output->data(), sizeof(count));
        if (count > capacity) {
            capacity = count;
            continue;
        }
        std::vector<std::array<std::uint32_t, 4>> found(count);
        std::memcpy(found.data(), output->data() + 16, count * 16);
        std::sort(found.begin(), found.end());
        std::vector<std::array<std::uint32_t, 2>> pairs;
        pairs.reserve(found.size());
        for (const auto& pair : found) { pairs.push_back({pair[1], pair[2]}); }
        return pairs;
    }
    return std::nullopt;
}

std::optional<std::vector<std::vector<hidden::crossing::Piece>>> crossingRuns(Device& device, const std::vector<hidden::crossing::Triangle>& triangles, double focalLength, hidden::Reach reach) {
    namespace crossing = hidden::crossing;
    const auto paired = crossing::detail::pairing(triangles, focalLength, reach);
    if (!paired.has_value()) { return std::vector<std::vector<crossing::Piece>> {}; }
    const auto pairs = mayCross(device, triangles, *paired);
    if (!pairs.has_value()) { return std::nullopt; }
    std::vector<crossing::Piece> pieces;
    for (const auto& [a, b] : *pairs) {
        // The GPU's boxes are a little wider than the CPU's.
        if (!paired->boxes[a].overlaps(paired->boxes[b])) { continue; }
        const auto piece = crossing::between(triangles, a, b);
        if (piece.has_value()) { pieces.push_back(*piece); }
    }
    return crossing::detail::joined(pieces, triangles);
}
}
