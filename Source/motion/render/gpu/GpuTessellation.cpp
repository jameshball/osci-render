#include "GpuTessellation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace motion::gpu {
namespace {
// What the tessellation's kernels share: a face, a cell of one, a view, and
// where a lattice's points are.
constexpr const char* shared = R"(
struct Face { a: vec4<f32>, b: vec4<f32>, c: vec4<f32>, colourA: vec4<f32>, colourB: vec4<f32>, colourC: vec4<f32>, worldA: vec4<f32>, worldB: vec4<f32>, worldC: vec4<f32>, program: u32, padding0: u32, cells: u32, padding: u32, surface: u32, face: u32, flags: u32, padding2: u32 }
struct Start { first: u32, cuts: u32, firstPiece: u32, padding: u32 }
struct Cell { face: u32, padding: u32, corners: array<i32, 6> }
struct View { position: vec4<f32>, right: vec4<f32>, up: vec4<f32>, forward: vec4<f32>, focal: f32, parallel: u32, farthest: f32, span: f32, reachX: f32, reachY: f32, pixel: f32, padding: f32 }

// The index of row `row`, column `column` of a triangle's lattice of `cuts`
// cuts a side, its points kept row by row (row j holding cuts + 1 - j).
fn gridIndex(cuts: u32, row: u32, column: u32) -> u32 {
    return row * (cuts + 1u) - (row * (row + 0xffffffffu)) / 2u + column;
}
// The column and row of a lattice's `index`th point.
fn latticeAt(cuts: u32, index: u32) -> vec2<u32> {
    var left = index;
    var row = 0u;
    while (left >= cuts + 1u - row) {
        left -= cuts + 1u - row;
        row++;
    }
    return vec2<u32>(left, row);
}
fn sourceAt(face: Face, s: f32, t: f32) -> vec3<f32> { return face.a.xyz + (face.b.xyz - face.a.xyz) * s + (face.c.xyz - face.a.xyz) * t; }
fn colourAt(face: Face, s: f32, t: f32) -> vec3<f32> { return face.colourA.xyz + (face.colourB.xyz - face.colourA.xyz) * s + (face.colourC.xyz - face.colourA.xyz) * t; }
)";

// Pass one: each cell's corners and edges' middles, bent (w is 0 where the
// bend can't place a point), six a cell.
constexpr const char* probing = R"(
@group(0) @binding(2) var<storage, read> faces: array<Face>;
@group(0) @binding(3) var<storage, read> cells: array<Cell>;
@group(0) @binding(4) var<storage, read_write> probes: array<vec4<f32>>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let index = id.x + id.y * groups.x * 64u;
    if (index >= 6u * arrayLength(&cells)) { return; }
    let cell = cells[index / 6u];
    let face = faces[cell.face];
    var c = cell.corners;
    let k = index % 6u;
    // In halves of the face's units, so an edge's middle is whole.
    var at = vec2<i32>(2 * c[2u * min(k, 2u)], 2 * c[2u * min(k, 2u) + 1u]);
    if (k >= 3u) {
        let start = k - 3u;
        let finish = (start + 1u) % 3u;
        at = vec2<i32>(c[2u * start] + c[2u * finish], c[2u * start + 1u] + c[2u * finish + 1u]);
    }
    let whole = f32(2u * face.cells);
    let s = f32(at.x) / whole;
    let t = f32(at.y) / whole;
    let p = bend(face.program, sourceAt(face, s, t), colourAt(face, s, t));
    probes[index] = vec4<f32>(p, select(0.0, 1.0, finite3(p)));
}
)";

// Pass two: how finely each cell is cut, as Dicer's grid would cut it: as
// the bend curves it, at its edges' middles, in the views that see it (from
// the flat triangle between its corners, on screen and in depth), never
// finer than a cell nor coarser than the cell allows a grid; 0 for a cell no
// view sees.
constexpr const char* measuring = R"(
@group(0) @binding(0) var<storage, read> faces: array<Face>;
@group(0) @binding(1) var<storage, read> probes: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> cells: array<Cell>;
@group(0) @binding(3) var<storage, read> views: array<View>;
@group(0) @binding(4) var<storage, read_write> cuts: array<u32>;

const nearPlane = 0.05;
const overscan = 1.05;
const cellPixels = 4.0;
const gridPixels = 32.0;
const flatTolerance = 0.125;
const depthTolerance = 0.0005;
const mostCuts = 64u;

// A point in a view's eye space, and on its buffer (w 0 behind its eye).
fn see(view: View, point: vec3<f32>) -> vec3<f32> {
    let offset = point - view.position.xyz;
    return vec3<f32>(dot(offset, view.right.xyz), dot(offset, view.up.xyz), dot(offset, view.forward.xyz));
}
fn project(view: View, eye: vec3<f32>) -> vec3<f32> {
    if (view.parallel != 0u) { return vec3<f32>(eye.x * view.focal, eye.y * view.focal, 1.0 + (view.farthest - eye.z) / view.span); }
    if (!(eye.z > nearPlane)) { return vec3<f32>(0.0); }
    return vec3<f32>(eye.x / eye.z * view.focal, eye.y / eye.z * view.focal, 1.0 / eye.z);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let index = id.x + id.y * groups.x * 64u;
    if (index >= arrayLength(&cells)) { return; }
    let cell = cells[index];
    let face = faces[cell.face];
    var bent: array<vec4<f32>, 6>;
    for (var k = 0u; k < 6u; k++) { bent[k] = probes[6u * index + k]; }
    // A corner the bend can't place: drawn as one piece, flat.
    if (bent[0].w == 0.0 || bent[1].w == 0.0 || bent[2].w == 0.0) {
        cuts[index] = 1u;
        return;
    }
    // Which views see it, and the size of a pixel where the finest does.
    var pixel = 1.0e30;
    var seenBy = array<bool, 64>();
    var seen = false;
    let viewCount = min(arrayLength(&views), 64u);
    for (var v = 0u; v < viewCount; v++) {
        let view = views[v];
        let x = view.reachX * overscan;
        let y = view.reachY * overscan;
        var left = 0; var right = 0; var below = 0; var above = 0; var behind = 0;
        var nearest = 1.0e30;
        for (var k = 0u; k < 3u; k++) {
            let eye = see(view, bent[k].xyz);
            let point = project(view, eye);
            if (view.parallel == 0u && !(eye.z > nearPlane)) {
                behind++;
                continue;
            }
            left += i32(point.x < -x);
            right += i32(point.x > x);
            below += i32(point.y < -y);
            above += i32(point.y > y);
            nearest = min(nearest, eye.z);
        }
        if (behind == 3 || (behind == 0 && (left == 3 || right == 3 || below == 3 || above == 3))) { continue; }
        seen = true;
        seenBy[v] = true;
        let depth = select(max(nearest, nearPlane), 1.0, view.parallel != 0u);
        pixel = min(pixel, view.pixel * depth / view.focal);
    }
    if (!seen) {
        cuts[index] = 0u;
        return;
    }
    let longest = max(max(dot(bent[1].xyz - bent[0].xyz, bent[1].xyz - bent[0].xyz), dot(bent[2].xyz - bent[1].xyz, bent[2].xyz - bent[1].xyz)), dot(bent[0].xyz - bent[2].xyz, bent[0].xyz - bent[2].xyz));
    let side = sqrt(longest) / pixel;
    let finest = max(1.0, ceil(side / cellPixels));
    let coarsest = max(1.0, ceil(side / gridPixels));
    // How far the bend takes its edges' middles off the flat triangle.
    var worst = 0.0;
    var endless = bent[3].w == 0.0 || bent[4].w == 0.0 || bent[5].w == 0.0;
    for (var v = 0u; v < viewCount && !endless; v++) {
        if (!seenBy[v]) { continue; }
        let view = views[v];
        var corners: array<vec3<f32>, 3>;
        var behind = false;
        for (var k = 0u; k < 3u; k++) {
            let eye = see(view, bent[k].xyz);
            if (view.parallel == 0u && !(eye.z > nearPlane)) { behind = true; }
            corners[k] = project(view, eye);
        }
        if (behind) { continue; }
        let a = corners[0];
        let u = corners[1] - a;
        let w = corners[2] - a;
        let area = u.x * w.y - u.y * w.x;
        if (!(abs(area) > 0.0)) {
            endless = true;
            break;
        }
        let dwdx = (u.z * w.y - w.z * u.y) / area;
        let dwdy = (w.z * u.x - u.z * w.x) / area;
        let slope = (abs(dwdx) + abs(dwdy)) * view.pixel;
        for (var k = 0u; k < 3u; k++) {
            let eye = see(view, bent[3u + k].xyz);
            if (view.parallel == 0u && !(eye.z > nearPlane)) {
                endless = true;
                break;
            }
            let point = project(view, eye);
            let plane = a.z + dwdx * (point.x - a.x) + dwdy * (point.y - a.y);
            worst = max(worst, abs(point.z - plane) / (depthTolerance * abs(point.z) + flatTolerance * slope));
            let start = corners[k];
            let finish = corners[(k + 1u) % 3u];
            let edge = finish.xy - start.xy;
            let length = sqrt(dot(edge, edge));
            if (length > 0.0) { worst = max(worst, abs((point.x - start.x) * edge.y - (point.y - start.y) * edge.x) / length / (flatTolerance * view.pixel)); }
        }
    }
    let needed = select(ceil(sqrt(worst)), finest, endless || !(worst < 1.0e12));
    cuts[index] = min(u32(clamp(needed, coarsest, finest)), mostCuts);
}
)";

// Pass three: every cell's grid of points, bent. A point is placed in its
// face in whole numbers of a cut, so cells cut as finely place their shared
// edges' points just the same.
constexpr const char* gridding = R"(
@group(0) @binding(2) var<storage, read> faces: array<Face>;
@group(0) @binding(3) var<storage, read> cells: array<Cell>;
@group(0) @binding(4) var<storage, read> starts: array<Start>;
@group(0) @binding(5) var<storage, read_write> points: array<vec4<f32>>;

fn cellOf(index: u32) -> u32 {
    var low = 0u;
    var high = arrayLength(&starts) - 1u;
    while (low < high) {
        let middle = (low + high + 1u) / 2u;
        if (starts[middle].first <= index) { low = middle; } else { high = middle - 1u; }
    }
    return low;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let index = id.x + id.y * groups.x * 64u;
    if (index >= arrayLength(&points)) { return; }
    let k = cellOf(index);
    let cell = cells[k];
    let face = faces[cell.face];
    let n = starts[k].cuts;
    let at = latticeAt(n, index - starts[k].first);
    let c = cell.corners;
    let i = i32(at.x);
    let j = i32(at.y);
    let cuts = i32(n);
    let whole = f32(face.cells * n);
    let s = f32(c[0] * cuts + (c[2] - c[0]) * i + (c[4] - c[0]) * j) / whole;
    let t = f32(c[1] * cuts + (c[3] - c[1]) * i + (c[5] - c[1]) * j) / whole;
    let p = bend(face.program, sourceAt(face, s, t), colourAt(face, s, t));
    points[index] = vec4<f32>(p, select(0.0, 1.0, finite3(p)));
}
)";

int gridIndex(int cuts, int row, int column) { return row * (cuts + 1) - row * (row - 1) / 2 + column; }
std::size_t latticeSize(int cuts) { return static_cast<std::size_t>((cuts + 1) * (cuts + 2) / 2); }

std::array<float, 4> vec4Of(const osci::Point& point) { return {point.x, point.y, point.z, 0}; }
std::array<float, 4> colourOf(const osci::Point& point) { return {point.r, point.g, point.b, 0}; }

template <typename T>
std::vector<T> valuesOf(const std::vector<std::byte>& bytes, std::size_t count) {
    std::vector<T> values(count);
    if (bytes.size() >= count * sizeof(T)) { std::memcpy(values.data(), bytes.data(), count * sizeof(T)); }
    return values;
}
}

const char* Tessellation::sharedFunctions() { return shared; }

std::optional<Tessellation::Plan> Tessellation::plan(Device& device, const ChainBatch& chain, std::span<const Face> faces, std::span<const hidden::DiceView> views) {
    Plan plan;
    if (faces.empty() || views.empty()) { return plan; }
    // Each face split into cells as large as Dicer grids, by its corners.
    hidden::Dicer measurer;
    measurer.begin(views);
    const auto four = [](const Vec3& value) { return std::array<float, 4> {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z), 0}; };
    // Each face split into four, and on, wherever it is larger on the
    // screen of the view that sees it most finely than Dicer grids (as
    // placed, before the bend: the GPU measures it bent), in whole units of
    // a face's 2^12, so cells as finely split share their corners exactly.
    constexpr int levels = 12;
    constexpr std::int32_t whole = 1 << levels;
    constexpr std::size_t mostSplit = std::size_t {1} << 18;
    for (std::uint32_t index = 0; index < faces.size(); ++index) {
        const auto& face = faces[index];
        if (!measurer.measure(face.corners[0], face.corners[1], face.corners[2]).has_value()) { continue; }
        const auto& sources = face.sources;
        plan.faces.push_back({vec4Of(sources[0]), vec4Of(sources[1]), vec4Of(sources[2]), colourOf(sources[0]), colourOf(sources[1]), colourOf(sources[2]), four(face.corners[0]), four(face.corners[1]), four(face.corners[2]), face.program, 0, static_cast<std::uint32_t>(whole), 0, face.surface, face.face, face.solid ? 1u : 0u, 0});
        plan.faceOf.push_back(index);
        const auto faceIndex = static_cast<std::uint32_t>(plan.faces.size() - 1);
        const auto placed = [&](std::int32_t i, std::int32_t j) { return face.corners[0] + (face.corners[1] - face.corners[0]) * (static_cast<double>(i) / whole) + (face.corners[2] - face.corners[0]) * (static_cast<double>(j) / whole); };
        std::vector<std::array<std::int32_t, 6>> pending {{0, 0, whole, 0, 0, whole}};
        std::uint32_t count = 0;
        while (!pending.empty()) {
            const auto c = pending.back();
            pending.pop_back();
            const auto p = placed(c[0], c[1]), q = placed(c[2], c[3]), r = placed(c[4], c[5]);
            const auto measured = measurer.measure(p, q, r);
            const auto apart = std::max({std::abs(c[2] - c[0]), std::abs(c[3] - c[1]), std::abs(c[4] - c[0]), std::abs(c[5] - c[1])});
            const auto side = measured.has_value() ? std::max({(q - p).length(), (r - q).length(), (p - r).length()}) / measured->pixel : 0.0;
            if (side > hidden::Dicer::gridReach && apart >= 2 && plan.cells.size() + pending.size() < mostSplit) {
                const std::array<std::int32_t, 6> middles {(c[0] + c[2]) / 2, (c[1] + c[3]) / 2, (c[2] + c[4]) / 2, (c[3] + c[5]) / 2, (c[4] + c[0]) / 2, (c[5] + c[1]) / 2};
                // Pushed last to first, so they come in order.
                pending.push_back({middles[0], middles[1], middles[2], middles[3], middles[4], middles[5]});
                pending.push_back({middles[4], middles[5], middles[2], middles[3], c[4], c[5]});
                pending.push_back({middles[0], middles[1], c[2], c[3], middles[2], middles[3]});
                pending.push_back({c[0], c[1], middles[0], middles[1], middles[4], middles[5]});
                continue;
            }
            plan.cells.push_back({faceIndex, 0, c});
            ++count;
        }
        plan.cellsOf.push_back(count);
    }
    if (plan.cells.empty()) { return plan; }
    std::vector<GpuView> gpuViews;
    for (const auto& view : views) {
        const auto& eye = view.eye;
        const auto& projection = view.projection;
        gpuViews.push_back({four(eye.position), four(eye.right), four(eye.up), four(eye.forward), static_cast<float>(projection.focalLength), projection.parallel ? 1u : 0u, static_cast<float>(projection.farthest), static_cast<float>(projection.span), static_cast<float>(view.reach.x), static_cast<float>(view.reach.y), static_cast<float>(view.pixel), 0});
    }
    plan.steps.assign(chain.stepData().begin(), chain.stepData().end());
    plan.programs.assign(chain.programData().begin(), chain.programData().end());
    if (plan.steps.empty()) { plan.steps.resize(1); }
    if (plan.programs.empty()) { plan.programs.resize(1); }
    static const juce::String probeKernel = juce::String(ChainBatch::chainFunctions()) + shared + probing;
    static const juce::String measureKernel = juce::String(shared) + measuring;
    // How finely each cell is cut.
    std::vector<std::uint32_t> cuts;
    {
        Device::Work work(device);
        const auto stepBuffer = work.upload(plan.steps), programBuffer = work.upload(plan.programs), faceBuffer = work.upload(plan.faces);
        const auto cellBuffer = work.upload(plan.cells), viewBuffer = work.upload(gpuViews);
        const auto probeBuffer = work.scratch(plan.cells.size() * 6 * 16);
        work.run(probeKernel, {stepBuffer, programBuffer, faceBuffer, cellBuffer, probeBuffer}, static_cast<std::uint32_t>(plan.cells.size() * 6));
        const auto cutBuffer = work.scratch(plan.cells.size() * sizeof(std::uint32_t));
        work.run(measureKernel, {faceBuffer, probeBuffer, cellBuffer, viewBuffer, cutBuffer}, static_cast<std::uint32_t>(plan.cells.size()));
        work.read(cutBuffer);
        const auto results = work.finish();
        if (!results.has_value()) { return std::nullopt; }
        cuts = valuesOf<std::uint32_t>(results->front(), plan.cells.size());
    }
    // Within the budget, every cell cut alike less finely past it.
    std::size_t wanted = 0;
    for (const auto n : cuts) { wanted += static_cast<std::size_t>(n) * n; }
    if (wanted > budget) {
        const auto scale = std::sqrt(static_cast<double>(budget) / static_cast<double>(wanted));
        for (auto& n : cuts) {
            if (n > 0) { n = static_cast<std::uint32_t>(std::max(1.0, std::floor(n * scale))); }
        }
    }
    for (const auto n : cuts) {
        plan.starts.push_back({plan.points, n, plan.pieces, 0});
        plan.points += n > 0 ? static_cast<std::uint32_t>(latticeSize(static_cast<int>(n))) : 0;
        plan.pieces += n * n;
    }
    return plan;
}

Tessellation::Recorded Tessellation::recordGrids(Device::Work& work, const Plan& plan) {
    static const juce::String gridKernel = juce::String(ChainBatch::chainFunctions()) + shared + gridding;
    const auto stepBuffer = work.upload(plan.steps), programBuffer = work.upload(plan.programs), faceBuffer = work.upload(plan.faces);
    const auto cellBuffer = work.upload(plan.cells), startBuffer = work.upload(plan.starts);
    const auto pointBuffer = work.scratch(static_cast<std::size_t>(std::max<std::uint32_t>(plan.points, 1)) * 16);
    work.run(gridKernel, {stepBuffer, programBuffer, faceBuffer, cellBuffer, startBuffer, pointBuffer}, plan.points);
    return {faceBuffer, cellBuffer, startBuffer, pointBuffer};
}
}
