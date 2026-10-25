#include "GpuCrossings.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace motion::gpu {
namespace {
// What both passes share: a triangle as the GPU holds it, its box, the
// cells it covers, the grid, and the pairs found.
constexpr const char* shared = R"(
struct Triangle { corners: array<vec4<i32>, 3>, rests: array<vec4<i32>, 3>, normal: vec4<f32>, surface: u32, padding0: u32, padding1: u32, padding2: u32 }
struct Box { left: f32, right: f32, bottom: f32, top: f32, nearby: f32, farthest: f32, surface: u32, start: u32 }
struct Cover { x0: i32, x1: i32, y0: i32, y1: i32 }
struct Grid { cells: u32, unit: f32, fine: f32, padding: u32 }
struct Found { count: atomic<u32>, padding0: u32, padding1: u32, padding2: u32, pairs: array<vec4<u32>> }
struct Candidates { count: atomic<u32>, padding0: u32, padding1: u32, padding2: u32, pairs: array<vec2<u32>> }
struct Listed { count: u32, padding0: u32, padding1: u32, padding2: u32, pairs: array<vec2<u32>> }

)";

// Pass one: each entry of a cell against the later members there, as
// crossing::detail::eachPair pairs them, listing those whose boxes meet (a
// cheap test most pairs fail, so the dearer one that follows has a thread
// for every pair it tests, not a long, uneven run of them).
constexpr const char* listing = R"(
@group(0) @binding(0) var<storage, read> boxes: array<Box>;
@group(0) @binding(1) var<storage, read> covers: array<Cover>;
@group(0) @binding(2) var<storage, read> cellOf: array<vec2<u32>>;
@group(0) @binding(3) var<storage, read> starts: array<u32>;
@group(0) @binding(4) var<storage, read> members: array<u32>;
@group(0) @binding(5) var<uniform> grid: Grid;
@group(0) @binding(6) var<storage, read_write> candidates: Candidates;

// Boxes overlap, widened a little so single precision never parts two that do.
fn overlaps(p: Box, q: Box) -> bool {
    let slack = 1.0e-5 * max(max(max(abs(p.left), abs(p.right)), max(abs(p.bottom), abs(p.top))), 1.0);
    let depth = 1.0e-5 * max(max(abs(p.nearby), abs(p.farthest)), 1.0);
    return !(p.right + slack < q.left || q.right + slack < p.left || p.top + slack < q.bottom || q.top + slack < p.bottom || p.farthest + depth < q.nearby || q.farthest + depth < p.nearby);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let entry = id.x + id.y * groups.x * 64u;
    if (entry >= arrayLength(&members)) { return; }
    let cell = cellOf[entry].x;
    let x = i32(cell % grid.cells);
    let y = i32(cell / grid.cells);
    let box = boxes[members[entry]];
    let cover = covers[members[entry]];
    // A cell's members come surface by surface: those of later surfaces
    // start at cellOf[entry].y.
    for (var k = cellOf[entry].y; k < starts[cell + 1u]; k++) {
        // Each pair once: in the cell where their boxes' overlap starts
        // (each box's first column and row are packed in its start).
        let theirs = boxes[members[k]];
        if (max(cover.x0, i32(theirs.start & 0xffffu)) != x || max(cover.y0, i32(theirs.start >> 16u)) != y || !overlaps(box, theirs)) { continue; }
        let at = atomicAdd(&candidates.count, 1u);
        if (at < arrayLength(&candidates.pairs)) { candidates.pairs[at] = vec2<u32>(entry, k); }
    }
}
)";

// Pass two: each listed pair tested as crossing::between tests it.
constexpr const char* testing = R"(
@group(0) @binding(0) var<storage, read> triangles: array<Triangle>;
@group(0) @binding(1) var<storage, read> members: array<u32>;
@group(0) @binding(2) var<uniform> grid: Grid;
@group(0) @binding(3) var<storage, read> listed: Listed;
@group(0) @binding(4) var<storage, read_write> found: Found;

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

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let index = id.x + id.y * groups.x * 64u;
    if (index >= min(listed.count, arrayLength(&listed.pairs))) { return; }
    let pair = listed.pairs[index];
    let i = members[pair.x];
    let j = members[pair.y];
    if (!mayCross(triangles[i], triangles[j])) { return; }
    let at = atomicAdd(&found.count, 1u);
    if (at < arrayLength(&found.pairs)) { found.pairs[at] = vec4<u32>(pair.x, i, j, pair.y); }
}
)";

struct Triangle {
    std::array<std::array<std::int32_t, 4>, 3> corners, rests;
    std::array<float, 4> normal;
    std::uint32_t surface, padding[3];
};
struct Box {
    float left, right, bottom, top, nearby, farthest;
    std::uint32_t surface, start;
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
        const auto range = grid.range(box);
        boxes.push_back({static_cast<float>(box.left), static_cast<float>(box.right), static_cast<float>(box.bottom), static_cast<float>(box.top), static_cast<float>(box.nearby), static_cast<float>(box.farthest), triangle.surface, static_cast<std::uint32_t>(range[0]) | static_cast<std::uint32_t>(range[2]) << 16});
        covers.push_back({range[0], range[1], range[2], range[3]});
    }
    std::vector<std::uint32_t> starts(grid.starts().begin(), grid.starts().end());
    const auto members = grid.members();
    // Each entry's cell, for a kernel run once an entry, and where in it the members of later surfaces start.
    std::vector<std::array<std::uint32_t, 2>> cellOf(members.size());
    for (std::size_t cell = 0; cell + 1 < starts.size(); ++cell) {
        auto later = starts[cell];
        for (auto entry = starts[cell]; entry < starts[cell + 1]; ++entry) {
            const auto surface = triangles[members[entry]].surface;
            later = std::max(later, entry + 1);
            while (later < starts[cell + 1] && triangles[members[later]].surface <= surface) { ++later; }
            cellOf[entry] = {static_cast<std::uint32_t>(cell), later};
        }
    }
    const auto unitBits = std::bit_cast<std::uint32_t>(static_cast<float>(unit));
    const std::array<std::uint32_t, 4> uniforms {static_cast<std::uint32_t>(grid.across()), unitBits, std::bit_cast<std::uint32_t>(static_cast<float>(fine)), 0};
    const std::array<Device::Input, 6> inputs {std::as_bytes(std::span(corners)), std::as_bytes(std::span(boxes)), std::as_bytes(std::span(covers)), std::as_bytes(std::span(cellOf)), std::as_bytes(std::span(starts)), std::as_bytes(members)};
    // Room for a few pairs a candidate; when more are found, again with room for all.
    static const juce::String listKernel = juce::String(shared) + listing, testKernel = juce::String(shared) + testing;
    // Room for some pairs a candidate whose boxes meet, and a few that may
    // cross; when more are found, again with room for all.
    auto listCapacity = members.size() * 16 + (std::size_t {1} << 20);
    auto capacity = pairing.candidates.size() * 6 + 1024;
    for (int attempt = 0; attempt < 3; ++attempt) {
        // Only the counts start at zero, and only what was found comes back.
        std::uint32_t listedCount = 0, count = 0;
        std::shared_ptr<Device::Kept> kept;
        {
            Device::Work work(device);
            const auto triangleBuffer = work.upload(inputs[0]), boxBuffer = work.upload(inputs[1]), coverBuffer = work.upload(inputs[2]), cellBuffer = work.upload(inputs[3]), startBuffer = work.upload(inputs[4]), memberBuffer = work.upload(inputs[5]);
            const auto uniform = work.uniform(std::as_bytes(std::span(uniforms)));
            const auto listed = work.scratch(16 + listCapacity * 8);
            const auto found = work.scratch(16 + capacity * 16);
            work.clear(listed, 16);
            work.clear(found, 16);
            work.run(listKernel, {boxBuffer, coverBuffer, cellBuffer, startBuffer, memberBuffer, uniform, listed}, static_cast<std::uint32_t>(members.size()));
            work.run(testKernel, {triangleBuffer, memberBuffer, uniform, listed, found}, static_cast<std::uint32_t>(listCapacity));
            work.read(listed, 16);
            work.read(found, 16);
            kept = work.keep(found);
            const auto results = work.finish();
            if (!results.has_value()) { return std::nullopt; }
            std::memcpy(&listedCount, (*results)[0].data(), sizeof(listedCount));
            std::memcpy(&count, (*results)[1].data(), sizeof(count));
        }
        if (listedCount > listCapacity || count > capacity) {
            listCapacity = std::max<std::size_t>(listCapacity, listedCount);
            capacity = std::max<std::size_t>(capacity, count);
            continue;
        }
        std::vector<std::array<std::uint32_t, 4>> found(count);
        if (count > 0) {
            Device::Work work(device);
            work.read(work.bind(kept), 16 + static_cast<std::size_t>(count) * 16);
            const auto results = work.finish();
            if (!results.has_value()) { return std::nullopt; }
            std::memcpy(found.data(), results->front().data() + 16, static_cast<std::size_t>(count) * 16);
        }
        // In eachPair's order: by the entry that found them, then the member
        // they were found with (pairs, tested in any order, sorted).
        std::vector<std::uint32_t> firsts(members.size() + 1, 0);
        for (const auto& pair : found) { ++firsts[pair[0] + 1]; }
        for (std::size_t entry = 0; entry < members.size(); ++entry) { firsts[entry + 1] += firsts[entry]; }
        std::vector<std::array<std::uint32_t, 4>> ordered(found.size());
        {
            auto next = firsts;
            for (const auto& pair : found) { ordered[next[pair[0]]++] = pair; }
        }
        for (std::size_t entry = 0; entry < members.size(); ++entry) {
            const auto from = ordered.begin() + firsts[entry], to = ordered.begin() + firsts[entry + 1];
            if (to - from > 1) { std::sort(from, to, [](const auto& a, const auto& b) { return a[3] < b[3]; }); }
        }
        std::vector<std::array<std::uint32_t, 2>> pairs(ordered.size());
        for (std::size_t index = 0; index < ordered.size(); ++index) { pairs[index] = {ordered[index][1], ordered[index][2]}; }
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
