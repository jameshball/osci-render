#include "GpuCrossings.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace motion::gpu {
namespace {
constexpr const char* kernel = R"(
struct Triangle { corners: array<vec4<i32>, 3>, rests: array<vec4<i32>, 3>, normal: vec4<f32>, surface: u32, padding0: u32, padding1: u32, padding2: u32 }
struct Box { left: f32, right: f32, bottom: f32, top: f32, nearby: f32, farthest: f32, padding0: f32, padding1: f32 }
struct Cover { x0: i32, x1: i32, y0: i32, y1: i32 }
struct Grid { cells: u32, unit: f32, fine: f32, padding: u32 }
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

// Corner `k` less corner `m` of the same or another triangle. Corners are
// held as whole numbers of `grid.unit` (the frame's triangles fit in 2^29 of
// them, so their differences fit too) and the rest as whole numbers of `grid.fine`, so a difference is
// exact however far the triangles are from the eye, and keeps the
// precision of their size however small (no compiler's rearranging of
// arithmetic can lose it, as it can a float's split in two).
fn less(t: Triangle, k: u32, u: Triangle, m: u32) -> vec3<f32> {
    return vec3<f32>(t.corners[k].xyz - u.corners[m].xyz) * grid.unit + vec3<f32>(t.rests[k].xyz - u.rests[m].xyz) * grid.fine;
}

// crossing::through, from a triangle's corners' distances to a plane, as
// shares of its corners along its two cut points; `ambiguous` when a
// distance is too near the tolerance for single precision to classify it
// as the CPU does.
struct Through { found: bool, ambiguous: bool, first: vec3<f32>, second: vec3<f32>, gap: f32 }
fn through(d: vec3<f32>, tolerance: f32, band: vec3<f32>) -> Through {
    var result: Through;
    var side = vec3<i32>(0);
    for (var k = 0; k < 3; k++) {
        if (abs(abs(d[k]) - tolerance) < band[k]) { result.ambiguous = true; return result; }
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
                result.gap = min(abs(d[corner] - d[next]), abs(d[corner] - d[last]));
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
            result.gap = abs(d[next] - d[last]);
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

// The most a distance to a plane, from an offset to it, can be out by, with
// a little to spare: ten roundings of single precision in each part of the
// offset (its parts, the normal's, and the sum), and a fine unit in each of
// its two corners.
fn errorOf(offset: vec3<f32>) -> f32 {
    return 6.0e-7 * (abs(offset.x) + abs(offset.y) + abs(offset.z)) + 4.0 * grid.fine;
}

// Whether two triangles may cut through each other, as crossing::between
// tests them: false only where single precision is sure the CPU finds no
// crossing.
fn mayCross(first: Triangle, second: Triangle) -> bool {
    let abA = less(first, 1u, first, 0u);
    let acA = less(first, 2u, first, 0u);
    let abB = less(second, 1u, second, 0u);
    let acB = less(second, 2u, second, 0u);
    // A triangle with no plane (its corners in line) crosses nothing.
    if (first.normal.w == 0.0 || second.normal.w == 0.0) { return false; }
    let size = max(max(length(abA), length(acA)), max(length(abB), length(acB)));
    let unitA = first.normal.xyz;
    let unitB = second.normal.xyz;
    let tolerance = size * 1.0e-5;
    let offsetsA = array<vec3<f32>, 3>(less(second, 0u, first, 0u), less(second, 1u, first, 0u), less(second, 2u, first, 0u));
    let offsetsB = array<vec3<f32>, 3>(less(first, 0u, second, 0u), less(first, 1u, second, 0u), less(first, 2u, second, 0u));
    let toA = vec3<f32>(dot(unitA, offsetsA[0]), dot(unitA, offsetsA[1]), dot(unitA, offsetsA[2]));
    let toB = vec3<f32>(dot(unitB, offsetsB[0]), dot(unitB, offsetsB[1]), dot(unitB, offsetsB[2]));
    // How near the tolerance each distance is too near to be sure of.
    let bandA = vec3<f32>(errorOf(offsetsA[0]), errorOf(offsetsA[1]), errorOf(offsetsA[2]));
    let bandB = vec3<f32>(errorOf(offsetsB[0]), errorOf(offsetsB[1]), errorOf(offsetsB[2]));
    let band = max(max(max(bandA.x, bandA.y), max(bandA.z, bandB.x)), max(bandB.y, bandB.z));
    let cutA = through(toB, tolerance, bandB);
    if (cutA.ambiguous) { return true; }
    if (!cutA.found) { return false; }
    let cutB = through(toA, tolerance, bandA);
    if (cutB.ambiguous) { return true; }
    if (!cutB.found) { return false; }
    // Both cuts lie on the line the planes meet on: where they overlap along
    // it, the triangles cross. Positions are taken from the first
    // triangle's first corner.
    let direction = cross(unitA, unitB);
    // Nearly parallel, the line the planes meet on is too rough to trust.
    if (!(length(direction) > 1.0e-2)) { return true; }
    let offsetB = less(second, 0u, first, 0u);
    let atA = vec2<f32>(dot(direction, cutA.first.y * abA + cutA.first.z * acA), dot(direction, cutA.second.y * abA + cutA.second.z * acA));
    let atB = vec2<f32>(dot(direction, offsetB + cutB.first.y * abB + cutB.first.z * acB), dot(direction, offsetB + cutB.second.y * abB + cutB.second.z * acB));
    let low = max(min(atA.x, atA.y), min(atB.x, atB.y));
    let high = min(max(atA.x, atA.y), max(atB.x, atB.y));
    let needed = tolerance * length(direction);
    // What the overlap may be out by: single precision's along the line,
    // more as the planes near parallel, and more where a cut point's share
    // comes from two distances close together.
    let margin = size * 1.0e-5 + band * 8.0 / length(direction) + size * 2.0 * (band / cutA.gap + band / cutB.gap);
    return high - low > needed - margin;
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
        if (at < arrayLength(&found.pairs)) { found.pairs[at] = vec4<u32>(entry, i, j, 0u); }
    }
}
)";

struct Triangle {
    std::array<std::array<std::int32_t, 4>, 3> corners, rests;
    std::array<float, 4> normal;
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
    // Corners as whole numbers of a unit that fits every corner in 2^29.
    double farthest = 1.0e-9;
    for (const auto& triangle : triangles) {
        for (const auto& corner : triangle.eye) { farthest = std::max({farthest, std::abs(corner.x), std::abs(corner.y), std::abs(corner.z)}); }
    }
    // Units single precision holds exactly, so the GPU's corners are these.
    const auto unit = static_cast<double>(static_cast<float>(farthest / static_cast<double>(1 << 29)));
    const auto fine = unit / static_cast<double>(1 << 20);
    const auto whole = [unit](const Vec3& value) { return std::array<std::int32_t, 4> {static_cast<std::int32_t>(std::llround(value.x / unit)), static_cast<std::int32_t>(std::llround(value.y / unit)), static_cast<std::int32_t>(std::llround(value.z / unit)), 0}; };
    const auto rest = [unit, fine](const Vec3& value, const std::array<std::int32_t, 4>& whole) {
        const auto of = [&](double part, std::int32_t units) { return static_cast<std::int32_t>(std::llround((part - static_cast<double>(units) * unit) / fine)); };
        return std::array<std::int32_t, 4> {of(value.x, whole[0]), of(value.y, whole[1]), of(value.z, whole[2]), 0};
    };
    for (std::size_t index = 0; index < triangles.size(); ++index) {
        const auto& triangle = triangles[index];
        // Its normal as crossing::between finds it, in double precision, so
        // a sliver's is as true as any other's.
        const auto normal = (triangle.eye[1] - triangle.eye[0]).cross(triangle.eye[2] - triangle.eye[0]).normalized(1.0e-300);
        const auto planar = normal.has_value() ? std::array<float, 4> {static_cast<float>(normal->x), static_cast<float>(normal->y), static_cast<float>(normal->z), 1.0f} : std::array<float, 4> {};
        const std::array<std::array<std::int32_t, 4>, 3> wholes {whole(triangle.eye[0]), whole(triangle.eye[1]), whole(triangle.eye[2])};
        corners.push_back({wholes, {rest(triangle.eye[0], wholes[0]), rest(triangle.eye[1], wholes[1]), rest(triangle.eye[2], wholes[2])}, planar, triangle.surface, {}});
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
    const auto unitBits = std::bit_cast<std::uint32_t>(static_cast<float>(unit));
    const std::array<std::uint32_t, 4> uniforms {static_cast<std::uint32_t>(grid.across()), unitBits, std::bit_cast<std::uint32_t>(static_cast<float>(fine)), 0};
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
        // In order of the entry that found them: eachPair's order, as an
        // entry's own pairs, counted up one at a time, come back in order.
        std::vector<std::uint32_t> firsts(members.size() + 1, 0);
        for (const auto& pair : found) { ++firsts[pair[0] + 1]; }
        for (std::size_t entry = 0; entry < members.size(); ++entry) { firsts[entry + 1] += firsts[entry]; }
        std::vector<std::array<std::uint32_t, 2>> pairs(found.size());
        for (const auto& pair : found) { pairs[firsts[pair[0]]++] = {pair[1], pair[2]}; }
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
