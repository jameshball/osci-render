#include "GpuScene.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>

namespace motion::gpu {
namespace {
// What the scene's kernels share: a triangle, a view, a triangle drawn into
// a view's pixels (a slot, kept in a row of those drawn; its order is where
// it was drawn, two a triangle for the two a cut at the near plane can
// make), and a plane as DepthBuffer keeps one.
constexpr const char* shared = R"(
struct Triangle { corners: array<vec4<f32>, 3>, sources: array<vec4<f32>, 3>, surface: u32, face: u32, flags: u32, padding: u32, colours: vec4<u32> }
struct View {
    position: vec4<f32>, right: vec4<f32>, up: vec4<f32>, forward: vec4<f32>,
    focal: f32, parallel: u32, farthest: f32, span: f32,
    originX: f32, originY: f32, size: f32, solidOnly: u32,
    width: u32, height: u32, tilesAcross: u32, tiles: u32,
    triangles: u32, capacity: u32, firstPixel: u32, firstPlane: u32,
    reachX: f32, reachY: f32, planeCapacity: u32, writeLight: u32,
    statsBase: u32, tileBase: u32, padding1: u32, padding2: u32,
}
// Every view of the frame at once: how many, the triangles, the room the
// slots, the tiles' lists and the planes have, the tiles' and pixels'
// totals, and which view is the camera's.
struct Frame { views: u32, triangles: u32, slotCapacity: u32, memberCapacity: u32, planeCapacity: u32, tileTotal: u32, pixelTotal: u32, camera: u32 }
struct Slot { a: vec2<f32>, b: vec2<f32>, c: vec2<f32>, w: f32, dwdx: f32, dwdy: f32, sign: f32, left: i32, right: i32, bottom: i32, top: i32, surface: u32, face: u32, order: u32, view: u32 }
struct Plane { w: f32, dwdx: f32, dwdy: f32, slope: f32, e0: vec4<f32>, e1: vec4<f32>, e2: vec4<f32> }

const nearPlane = 0.05;
const cutAt = 0.05 * (1.0 + 1.0e-6);
const overscan = 1.05;
const depthPush = 2.0e-3;
const insidePush = 0.25;
const slopePush = 1.5;
const onEdge = 1.0e-3;
const tileSize = 8;

fn see(view: View, point: vec3<f32>) -> vec3<f32> {
    let offset = point - view.position.xyz;
    return vec3<f32>(dot(offset, view.right.xyz), dot(offset, view.up.xyz), dot(offset, view.forward.xyz));
}
fn keeps(view: View, z: f32) -> bool { return view.parallel != 0u || z > cutAt; }
// On the buffer, in output units, with inverse depth: a kept point only.
fn project(view: View, eye: vec3<f32>) -> vec3<f32> {
    if (view.parallel != 0u) { return vec3<f32>(eye.x * view.focal, eye.y * view.focal, 1.0 + (view.farthest - eye.z) / view.span); }
    return vec3<f32>(eye.x / eye.z * view.focal, eye.y / eye.z * view.focal, 1.0 / eye.z);
}

// A triangle in a view's eye space, cut a hair in front of the near plane
// as drawFlat cuts it: up to four corners, each with its point before the
// chain and colour.
struct Clipped { count: u32, eye: array<vec3<f32>, 4>, source: array<vec3<f32>, 4>, colour: array<vec4<f32>, 4> }
fn clip(view: View, triangle: Triangle) -> Clipped {
    var out: Clipped;
    var corners = triangle.corners;
    var sources = triangle.sources;
    var colours = triangle.colours;
    var eyes: array<vec3<f32>, 3>;
    for (var k = 0u; k < 3u; k++) { eyes[k] = see(view, corners[k].xyz); }
    for (var k = 0u; k < 3u; k++) {
        let next = (k + 1u) % 3u;
        let here = keeps(view, eyes[k].z);
        if (here) {
            out.eye[out.count] = eyes[k];
            out.source[out.count] = sources[k].xyz;
            out.colour[out.count] = unpack4x8unorm(colours[k]);
            out.count++;
        }
        if (here != keeps(view, eyes[next].z)) {
            let along = (cutAt - eyes[k].z) / (eyes[next].z - eyes[k].z);
            out.eye[out.count] = eyes[k] + (eyes[next] - eyes[k]) * along;
            out.source[out.count] = sources[k].xyz + (sources[next].xyz - sources[k].xyz) * along;
            out.colour[out.count] = mix(unpack4x8unorm(colours[k]), unpack4x8unorm(colours[next]), along);
            out.count++;
        }
    }
    return out;
}
// Whether every corner is past one side of the view: a bent surface's piece
// there is dropped.
fn offFrame(view: View, at: array<vec3<f32>, 4>, count: u32) -> bool {
    let x = view.reachX * overscan;
    let y = view.reachY * overscan;
    var left = true; var right = true; var below = true; var above = true;
    for (var k = 0u; k < count; k++) {
        left = left && at[k].x < -x;
        right = right && at[k].x > x;
        below = below && at[k].y < -y;
        above = above && at[k].y > y;
    }
    return left || right || below || above;
}
fn toPixel(view: View, at: vec3<f32>) -> vec2<f32> { return vec2<f32>((at.x + view.originX) / view.size, (at.y + view.originY) / view.size); }
fn cell(value: f32) -> i32 { return i32(clamp(value, -1.0e9, 1.0e9)); }
// A polygon on a view's buffer, in pixels, with inverse depth.
struct Polygon { count: u32, at: array<vec3<f32>, 8> }
// Clipped to one side of a line across the buffer (inverse depth, linear on
// it, as it goes).
fn clipSide(polygon: Polygon, axis: u32, bound: f32, above: bool) -> Polygon {
    var out: Polygon;
    var input = polygon;
    for (var k = 0u; k < input.count; k++) {
        let p = input.at[k];
        let q = input.at[(k + 1u) % input.count];
        let pv = select(p.y, p.x, axis == 0u);
        let qv = select(q.y, q.x, axis == 0u);
        let pin = select(pv <= bound, pv >= bound, above);
        let qin = select(qv <= bound, qv >= bound, above);
        if (pin && out.count < 8u) {
            out.at[out.count] = p;
            out.count++;
        }
        if (pin != qin && out.count < 8u) {
            out.at[out.count] = p + (q - p) * ((bound - pv) / (qv - pv));
            out.count++;
        }
    }
    return out;
}
// Within a guard band a buffer wide round the buffer: a triangle reaching
// far past it (one cut at the near plane, just in front of the eye) is drawn
// from corners near it, which single precision places as truly as its
// pixels.
fn guard(view: View, polygon: Polygon) -> Polygon {
    let band = f32(max(view.width, view.height));
    var out = clipSide(polygon, 0u, -band, true);
    out = clipSide(out, 0u, f32(view.width) + band, false);
    out = clipSide(out, 1u, -band, true);
    return clipSide(out, 1u, f32(view.height) + band, false);
}
// A triangle as DepthBuffer::triangle draws it, from its corners in pixels:
// none (left past right) where it is too thin or covers no pixel's centre.
fn slotOf(view: View, a: vec3<f32>, b: vec3<f32>, c: vec3<f32>, surface: u32, face: u32) -> Slot {
    var slot: Slot;
    slot.left = 1;
    slot.right = 0;
    let pa = a.xy;
    let pb = b.xy;
    let pc = c.xy;
    let area = (pb.x - pa.x) * (pc.y - pa.y) - (pb.y - pa.y) * (pc.x - pa.x);
    if (!(abs(area) >= 1.0e-12) || !(abs(area) < 1.0e30)) { return slot; }
    let margin = 1.0e-6;
    let left = max(0, cell(ceil(min(min(pa.x, pb.x), pc.x) - 0.5 - margin)));
    let right = min(i32(view.width) - 1, cell(floor(max(max(pa.x, pb.x), pc.x) - 0.5 + margin)));
    let bottom = max(0, cell(ceil(min(min(pa.y, pb.y), pc.y) - 0.5 - margin)));
    let top = min(i32(view.height) - 1, cell(floor(max(max(pa.y, pb.y), pc.y) - 0.5 + margin)));
    if (left > right || bottom > top) { return slot; }
    slot.a = pa;
    slot.b = pb;
    slot.c = pc;
    slot.w = a.z;
    slot.dwdx = ((b.z - a.z) * (pc.y - pa.y) - (c.z - a.z) * (pb.y - pa.y)) / area;
    slot.dwdy = ((c.z - a.z) * (pb.x - pa.x) - (b.z - a.z) * (pc.x - pa.x)) / area;
    slot.sign = select(-1.0, 1.0, area > 0.0);
    slot.left = left;
    slot.right = right;
    slot.bottom = bottom;
    slot.top = top;
    slot.surface = surface;
    slot.face = face;
    return slot;
}
)";

// Each triangle cut into every view's slots, each kept in the row of those
// drawn as it is made (in no set order: a pixel takes the first drawn by its
// order, and planes are only told apart), counted into the tiles its box
// reaches.
constexpr const char* transforming = R"(
@group(0) @binding(0) var<storage, read> triangles: array<Triangle>;
@group(0) @binding(1) var<storage, read> views: array<View>;
@group(0) @binding(2) var<uniform> frame: Frame;
@group(0) @binding(3) var<storage, read_write> slots: array<Slot>;
@group(0) @binding(4) var<storage, read_write> tileCounts: array<atomic<u32>>;
@group(0) @binding(5) var<storage, read_write> stats: array<atomic<u32>>;

// Whether every corner is within the guard band already.
fn inBand(view: View, polygon: Polygon) -> bool {
    let band = f32(max(view.width, view.height));
    var input = polygon;
    for (var k = 0u; k < input.count; k++) {
        let at = input.at[k];
        if (at.x < -band || at.y < -band || at.x > f32(view.width) + band || at.y > f32(view.height) + band) { return false; }
    }
    return true;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let t = id.x + id.y * groups.x * 64u;
    if (t >= frame.triangles) { return; }
    let triangle = triangles[t];
    for (var v = 0u; v < frame.views; v++) {
        let view = views[v];
        if (view.solidOnly != 0u && (triangle.flags & 1u) == 0u) { continue; }
        var clipped = clip(view, triangle);
        if (clipped.count < 3u) { continue; }
        var at: array<vec3<f32>, 4>;
        for (var k = 0u; k < clipped.count; k++) { at[k] = project(view, clipped.eye[k]); }
        if ((triangle.flags & 2u) != 0u && offFrame(view, at, clipped.count)) { continue; }
        var polygon: Polygon;
        polygon.count = clipped.count;
        for (var k = 0u; k < clipped.count; k++) { polygon.at[k] = vec3<f32>(toPixel(view, at[k]), at[k].z); }
        if (!inBand(view, polygon)) { polygon = guard(view, polygon); }
        for (var piece = 0u; piece + 2u < polygon.count; piece++) {
            var slot = slotOf(view, polygon.at[0], polygon.at[piece + 1u], polygon.at[piece + 2u], triangle.surface, triangle.face);
            if (slot.left > slot.right) { continue; }
            slot.order = 8u * t + piece;
            slot.view = v;
            let index = atomicAdd(&stats[0], 1u);
            if (index >= frame.slotCapacity) {
                atomicStore(&stats[1], 1u);
                continue;
            }
            slots[index] = slot;
            if (atomicLoad(&stats[8u + v]) == 0u) { atomicStore(&stats[8u + v], 1u); }
            for (var y = slot.bottom / tileSize; y <= slot.top / tileSize; y++) {
                for (var x = slot.left / tileSize; x <= slot.right / tileSize; x++) { atomicAdd(&tileCounts[view.tileBase + u32(y) * view.tilesAcross + u32(x)], 1u); }
            }
        }
    }
}
)";

// Each slot listed in the tiles its box reaches.
constexpr const char* filling = R"(
@group(0) @binding(0) var<storage, read> slots: array<Slot>;
@group(0) @binding(1) var<storage, read> views: array<View>;
@group(0) @binding(2) var<uniform> frame: Frame;
@group(0) @binding(3) var<storage, read> tileStarts: array<u32>;
@group(0) @binding(4) var<storage, read_write> tileFill: array<atomic<u32>>;
@group(0) @binding(5) var<storage, read_write> members: array<u32>;
@group(0) @binding(6) var<storage, read_write> stats: array<atomic<u32>>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let s = id.x + id.y * groups.x * 64u;
    if (s >= min(atomicLoad(&stats[0]), frame.slotCapacity)) { return; }
    let slot = slots[s];
    let view = views[slot.view];
    for (var y = slot.bottom / tileSize; y <= slot.top / tileSize; y++) {
        for (var x = slot.left / tileSize; x <= slot.right / tileSize; x++) {
            let tile = view.tileBase + u32(y) * view.tilesAcross + u32(x);
            let at = tileStarts[tile] + atomicAdd(&tileFill[tile], 1u);
            if (at < frame.memberCapacity) {
                members[at] = s;
            } else {
                atomicStore(&stats[1], 1u);
            }
        }
    }
}
)";

// A tile of 8 by 8 pixels of some view a workgroup, a pixel each: of the
// slots whose boxes reach it, the nearest whose triangle covers the pixel's
// centre (its edges included, with room for rounding), the first drawn where
// two are as near; the tile's slots loaded 64 at a time, together.
constexpr const char* rastering = R"(
@group(0) @binding(0) var<storage, read> slots: array<Slot>;
@group(0) @binding(1) var<storage, read> views: array<View>;
@group(0) @binding(2) var<uniform> frame: Frame;
@group(0) @binding(3) var<storage, read> tileStarts: array<u32>;
@group(0) @binding(4) var<storage, read> tileCounts: array<u32>;
@group(0) @binding(5) var<storage, read> members: array<u32>;
@group(0) @binding(6) var<storage, read_write> pixels: array<vec2<u32>>;
var<workgroup> chunk: array<Slot, 64>;
var<workgroup> chunkIndex: array<u32, 64>;

fn covers(slot: Slot, x: f32, y: f32) -> bool {
    var corners = array<vec2<f32>, 3>(slot.a, slot.b, slot.c);
    for (var k = 0u; k < 3u; k++) {
        let start = corners[k];
        let along = corners[(k + 1u) % 3u] - start;
        let side = slot.sign * (along.x * (y - start.y) - along.y * (x - start.x));
        if (side < -1.0e-3 * length(along)) { return false; }
    }
    return true;
}

@compute @workgroup_size(64)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>, @builtin(local_invocation_index) local: u32) {
    // Every invocation reaches the barriers: a tile past the last, or a
    // pixel past its buffer's edge, only takes no part.
    let tile = group.x + group.y * groups.x;
    let inTiles = tile < frame.tileTotal;
    var v = 0u;
    for (var k = 1u; k < frame.views; k++) {
        if (views[k].tileBase <= tile) { v = k; }
    }
    let view = views[v];
    let local_tile = tile - view.tileBase;
    let x = i32((local_tile % max(view.tilesAcross, 1u)) * 8u + local % 8u);
    let y = i32((local_tile / max(view.tilesAcross, 1u)) * 8u + local / 8u);
    let inside = inTiles && x < i32(view.width) && y < i32(view.height);
    let px = f32(x) + 0.5;
    let py = f32(y) + 0.5;
    var nearest = 0.0;
    var owner = 0u;
    var order = 0u;
    var first = 0u;
    var last = 0u;
    if (inTiles) {
        first = tileStarts[tile];
        last = min(first + tileCounts[tile], frame.memberCapacity);
    }
    for (var start = first; start < last; start += 64u) {
        workgroupBarrier();
        if (start + local < last) {
            let s = members[start + local];
            chunk[local] = slots[s];
            chunkIndex[local] = s;
        }
        workgroupBarrier();
        let count = min(64u, last - start);
        for (var k = 0u; k < count; k++) {
            let slot = chunk[k];
            if (!inside || x < slot.left || x > slot.right || y < slot.bottom || y > slot.top || !covers(slot, px, py)) { continue; }
            let here = slot.w + slot.dwdx * (px - slot.a.x) + slot.dwdy * (py - slot.a.y);
            if (here > nearest || (here == nearest && here > 0.0 && slot.order < order)) {
                nearest = here;
                owner = chunkIndex[k];
                order = slot.order;
            }
        }
    }
    if (inside) { pixels[view.firstPixel + u32(y) * view.width + u32(x)] = vec2<u32>(bitcast<u32>(nearest), owner); }
}
)";

// The slots some pixel takes.
constexpr const char* marking = R"(
@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var<storage, read> pixels: array<vec2<u32>>;
@group(0) @binding(2) var<storage, read_write> taken: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let p = id.x + id.y * groups.x * 64u;
    if (p >= frame.pixelTotal) { return; }
    let pixel = pixels[p];
    if (bitcast<f32>(pixel.x) > 0.0) { taken[pixel.y] = 1u; }
}
)";

// Each slot a pixel takes, as a plane, in a row of every view's (and for
// lighting, just its depth and slopes); then each pixel's owner, among them.
constexpr const char* compacting = R"(
@group(0) @binding(0) var<storage, read> slots: array<Slot>;
@group(0) @binding(1) var<uniform> frame: Frame;
@group(0) @binding(2) var<storage, read> taken: array<u32>;
@group(0) @binding(3) var<storage, read> planeIndex: array<u32>;
@group(0) @binding(4) var<storage, read_write> planes: array<Plane>;
@group(0) @binding(5) var<storage, read_write> lightPlanes: array<vec4<f32>>;
@group(0) @binding(6) var<storage, read> stats: array<u32>;

fn edgeOf(start: vec2<f32>, finish: vec2<f32>, sign: f32) -> vec3<f32> {
    let edge = vec3<f32>(-sign * (finish.y - start.y), sign * (finish.x - start.x), sign * ((finish.y - start.y) * start.x - (finish.x - start.x) * start.y));
    let length = sqrt(edge.x * edge.x + edge.y * edge.y);
    return select(edge, edge / length, length > 0.0);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let s = id.x + id.y * groups.x * 64u;
    if (s >= min(stats[0], frame.slotCapacity) || taken[s] == 0u) { return; }
    let index = planeIndex[s];
    if (index >= frame.planeCapacity) { return; }
    let slot = slots[s];
    let slope = abs(slot.dwdx) + abs(slot.dwdy);
    let w = slot.w - slot.dwdx * slot.a.x - slot.dwdy * slot.a.y - insidePush * slope;
    var plane: Plane;
    plane.w = w;
    plane.dwdx = slot.dwdx;
    plane.dwdy = slot.dwdy;
    plane.slope = slopePush * slope;
    plane.e0 = vec4<f32>(edgeOf(slot.a, slot.b, slot.sign), bitcast<f32>(slot.face));
    plane.e1 = vec4<f32>(edgeOf(slot.b, slot.c, slot.sign), bitcast<f32>(slot.surface));
    plane.e2 = vec4<f32>(edgeOf(slot.c, slot.a, slot.sign), 0.0);
    planes[index] = plane;
    lightPlanes[index] = vec4<f32>(w, slot.dwdx, slot.dwdy, 0.0);
}
)";

constexpr const char* remapping = R"(
@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var<storage, read_write> pixels: array<vec2<u32>>;
@group(0) @binding(2) var<storage, read> planeIndex: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let p = id.x + id.y * groups.x * 64u;
    if (p >= frame.pixelTotal) { return; }
    let pixel = pixels[p];
    if (bitcast<f32>(pixel.x) > 0.0) { pixels[p] = vec2<u32>(pixel.x, planeIndex[pixel.y]); }
}
)";

// DepthBuffer::hiddenBy over a view's pixels and planes, for the camera:
// whether what is drawn hides a point (on its buffer, with inverse depth).
constexpr const char* hiding = R"(
fn planeAt(plane: Plane, x: f32, y: f32) -> f32 { return plane.w + plane.dwdx * x + plane.dwdy * y; }
fn outside(plane: Plane, x: f32, y: f32) -> f32 {
    let distance = -min(min(plane.e0.x * x + plane.e0.y * y + plane.e0.z, plane.e1.x * x + plane.e1.y * y + plane.e1.z), plane.e2.x * x + plane.e2.y * y + plane.e2.z);
    return select(0.0, distance, distance > onEdge);
}
fn inFront(w: f32, plane: f32) -> bool { return w * (1.0 + depthPush) >= plane; }
fn hidden(point: vec3<f32>) -> bool {
    let at = toPixel(view, point);
    let x = cell(floor(at.x));
    let y = cell(floor(at.y));
    if (x < 0 || y < 0 || x >= i32(view.width) || y >= i32(view.height)) { return false; }
    let here = pixels[view.firstPixel + u32(y) * view.width + u32(x)];
    if (!(bitcast<f32>(here.x) > 0.0)) { return false; }
    let plane = planes[here.y];
    let out = outside(plane, at.x, at.y);
    if (out <= 0.0) { return !inFront(point.z, planeAt(plane, at.x, at.y)); }
    var covered = false;
    for (var row = max(0, y - 1); row <= min(i32(view.height) - 1, y + 1); row++) {
        for (var column = max(0, x - 1); column <= min(i32(view.width) - 1, x + 1); column++) {
            let nearby = pixels[view.firstPixel + u32(row) * view.width + u32(column)];
            if (!(bitcast<f32>(nearby.x) > 0.0) || nearby.y == here.y) { continue; }
            let other = planes[nearby.y];
            if (outside(other, at.x, at.y) > 0.0) { continue; }
            covered = true;
            if (!inFront(point.z, planeAt(other, at.x, at.y))) { return true; }
        }
    }
    if (covered) { return false; }
    return !inFront(point.z, planeAt(plane, at.x, at.y) - plane.slope * out);
}
)";

// Which of the camera's solid slots show at all, as dropUnseen finds them:
// any wider than a few pixels, and any other with a corner, an edge's middle
// or its middle in view.
constexpr const char* seeing = R"(
@group(0) @binding(0) var<storage, read> triangles: array<Triangle>;
@group(0) @binding(1) var<storage, read> views: array<View>;
@group(0) @binding(2) var<uniform> frame: Frame;
@group(0) @binding(3) var<storage, read> pixels: array<vec2<u32>>;
@group(0) @binding(4) var<storage, read> planes: array<Plane>;
@group(0) @binding(5) var<storage, read_write> visible: array<u32>;
var<private> view: View;

const slight = 3.0 * 2.0 * 1.05 / 1024.0;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let t = id.x + id.y * groups.x * 64u;
    if (t >= frame.triangles) { return; }
    view = views[frame.camera];
    let triangle = triangles[t];
    if ((triangle.flags & 1u) == 0u) { return; }
    var clipped = clip(view, triangle);
    if (clipped.count < 3u) { return; }
    var at: array<vec3<f32>, 4>;
    for (var k = 0u; k < clipped.count; k++) { at[k] = project(view, clipped.eye[k]); }
    if ((triangle.flags & 2u) != 0u && offFrame(view, at, clipped.count)) { return; }
    for (var piece = 0u; piece + 2u < clipped.count; piece++) {
        let corners = array<vec3<f32>, 3>(clipped.eye[0], clipped.eye[piece + 1u], clipped.eye[piece + 2u]);
        let screens = array<vec3<f32>, 3>(at[0], at[piece + 1u], at[piece + 2u]);
        let reach = max(max(abs(screens[1].x - screens[0].x), abs(screens[2].x - screens[0].x)), max(abs(screens[1].y - screens[0].y), abs(screens[2].y - screens[0].y)));
        var seen = reach > slight;
        var probes = array<vec3<f32>, 7>(corners[0], corners[1], corners[2], (corners[0] + corners[1]) * 0.5, (corners[1] + corners[2]) * 0.5, (corners[2] + corners[0]) * 0.5, (corners[0] + corners[1] + corners[2]) / 3.0);
        for (var k = 0u; k < 7u && !seen; k++) { seen = !hidden(project(view, probes[k])); }
        if (seen) { visible[2u * t + piece] = 1u; }
    }
}
)";

// The camera's solid slots that show, each in a row of them, in its eye
// space with its points before the chain.
constexpr const char* gathering = R"(
@group(0) @binding(0) var<storage, read> triangles: array<Triangle>;
@group(0) @binding(1) var<storage, read> views: array<View>;
@group(0) @binding(2) var<uniform> frame: Frame;
@group(0) @binding(3) var<storage, read> visible: array<u32>;
@group(0) @binding(4) var<storage, read> positions: array<u32>;
@group(0) @binding(5) var<storage, read_write> gathered: array<Triangle>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let t = id.x + id.y * groups.x * 64u;
    if (t >= frame.triangles) { return; }
    let view = views[frame.camera];
    if (visible[2u * t] == 0u && visible[2u * t + 1u] == 0u) { return; }
    let triangle = triangles[t];
    var clipped = clip(view, triangle);
    for (var piece = 0u; piece + 2u < clipped.count; piece++) {
        if (visible[2u * t + piece] == 0u) { continue; }
        var corners = array<u32, 3>(0u, piece + 1u, piece + 2u);
        var made: Triangle;
        for (var k = 0u; k < 3u; k++) {
            made.corners[k] = vec4<f32>(clipped.eye[corners[k]], 0.0);
            made.sources[k] = vec4<f32>(clipped.source[corners[k]], 0.0);
            made.colours[k] = pack4x8unorm(clipped.colour[corners[k]]);
        }
        made.surface = triangle.surface;
        made.face = triangle.face;
        made.flags = triangle.flags;
        gathered[positions[2u * t + piece]] = made;
    }
}
)";

// An exclusive running sum over `values` (in blocks of 1024, each a
// workgroup's), its total into `totals` at `at`.
constexpr const char* scanBlocks = R"(
struct Sum { count: u32, blocks: u32, at: u32, padding: u32 }
@group(0) @binding(0) var<storage, read> values: array<u32>;
@group(0) @binding(1) var<storage, read_write> sums: array<u32>;
@group(0) @binding(2) var<storage, read_write> blockSums: array<u32>;
@group(0) @binding(3) var<uniform> sum: Sum;
var<workgroup> partial: array<u32, 64>;

@compute @workgroup_size(64)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>, @builtin(local_invocation_index) local: u32) {
    let block = group.x + group.y * groups.x;
    let base = block * 1024u + local * 16u;
    var running: array<u32, 16>;
    var total = 0u;
    for (var k = 0u; k < 16u; k++) {
        running[k] = total;
        if (base + k < sum.count) { total += values[base + k]; }
    }
    partial[local] = total;
    workgroupBarrier();
    for (var offset = 1u; offset < 64u; offset *= 2u) {
        var add = 0u;
        if (local >= offset) { add = partial[local - offset]; }
        workgroupBarrier();
        partial[local] += add;
        workgroupBarrier();
    }
    let before = partial[local] - total;
    for (var k = 0u; k < 16u; k++) {
        if (base + k < sum.count) { sums[base + k] = before + running[k]; }
    }
    if (local == 63u && block < sum.blocks) { blockSums[block] = partial[63]; }
}
)";
constexpr const char* scanSums = R"(
struct Sum { count: u32, blocks: u32, at: u32, padding: u32 }
@group(0) @binding(0) var<storage, read_write> blockSums: array<u32>;
@group(0) @binding(1) var<storage, read_write> totals: array<u32>;
@group(0) @binding(2) var<uniform> sum: Sum;
var<workgroup> partial: array<u32, 64>;

@compute @workgroup_size(64)
fn main(@builtin(local_invocation_index) local: u32) {
    let chunk = (sum.blocks + 63u) / 64u;
    let base = local * chunk;
    var total = 0u;
    for (var k = 0u; k < chunk; k++) {
        if (base + k < sum.blocks) { total += blockSums[base + k]; }
    }
    partial[local] = total;
    workgroupBarrier();
    for (var offset = 1u; offset < 64u; offset *= 2u) {
        var add = 0u;
        if (local >= offset) { add = partial[local - offset]; }
        workgroupBarrier();
        partial[local] += add;
        workgroupBarrier();
    }
    var running = partial[local] - total;
    for (var k = 0u; k < chunk; k++) {
        if (base + k < sum.blocks) {
            let value = blockSums[base + k];
            blockSums[base + k] = running;
            running += value;
        }
    }
    if (local == 63u) { totals[sum.at] = partial[63]; }
}
)";
constexpr const char* scanAdd = R"(
struct Sum { count: u32, blocks: u32, at: u32, padding: u32 }
@group(0) @binding(0) var<storage, read_write> sums: array<u32>;
@group(0) @binding(1) var<storage, read> blockSums: array<u32>;
@group(0) @binding(2) var<uniform> sum: Sum;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let index = id.x + id.y * groups.x * 64u;
    if (index < sum.count) { sums[index] += blockSums[index / 1024u]; }
}
)";

// Each bent face's cells' grids as triangles, after the flat ones.
constexpr const char* expanding = R"(
struct SceneTriangle { corners: array<vec4<f32>, 3>, sources: array<vec4<f32>, 3>, surface: u32, face: u32, flags: u32, padding: u32, colours: vec4<u32> }
@group(0) @binding(0) var<storage, read> faces: array<Face>;
@group(0) @binding(1) var<storage, read> cells: array<Cell>;
@group(0) @binding(2) var<storage, read> starts: array<Start>;
@group(0) @binding(3) var<storage, read> points: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read> flat: array<SceneTriangle>;
@group(0) @binding(5) var<storage, read_write> scene: array<SceneTriangle>;
@group(0) @binding(6) var<uniform> counts: vec4<u32>;

fn cellOf(index: u32) -> u32 {
    var low = 0u;
    var high = arrayLength(&starts) - 1u;
    while (low < high) {
        let middle = (low + high + 1u) / 2u;
        if (starts[middle].firstPiece <= index) { low = middle; } else { high = middle - 1u; }
    }
    return low;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let index = id.x + id.y * groups.x * 64u;
    let flatCount = counts.x;
    if (index >= flatCount + counts.y) { return; }
    if (index < flatCount) {
        scene[index] = flat[index];
        return;
    }
    let piece = index - flatCount;
    let k = cellOf(piece);
    let start = starts[k];
    let cell = cells[k];
    let face = faces[cell.face];
    let n = start.cuts;
    // Its row and place along it: row j holds 2 (n - j) - 1, up and down in turn.
    var left = piece - start.firstPiece;
    var j = 0u;
    while (left >= 2u * (n - j) - 1u) {
        left -= 2u * (n - j) - 1u;
        j++;
    }
    let i = left / 2u;
    var lattice = array<vec2<u32>, 3>(vec2<u32>(i, j), vec2<u32>(i + 1u, j), vec2<u32>(i, j + 1u));
    if (left % 2u == 1u) { lattice = array<vec2<u32>, 3>(vec2<u32>(i + 1u, j), vec2<u32>(i + 1u, j + 1u), vec2<u32>(i, j + 1u)); }
    var c = cell.corners;
    let cuts = i32(n);
    let whole = f32(face.cells * n);
    var made: SceneTriangle;
    for (var corner = 0u; corner < 3u; corner++) {
        let at = lattice[corner];
        let s = f32(c[0] * cuts + (c[2] - c[0]) * i32(at.x) + (c[4] - c[0]) * i32(at.y)) / whole;
        let t = f32(c[1] * cuts + (c[3] - c[1]) * i32(at.x) + (c[5] - c[1]) * i32(at.y)) / whole;
        let bent = points[start.first + gridIndex(n, at.y, at.x)];
        let flatAt = face.worldA.xyz + (face.worldB.xyz - face.worldA.xyz) * s + (face.worldC.xyz - face.worldA.xyz) * t;
        made.corners[corner] = vec4<f32>(select(flatAt, bent.xyz, bent.w != 0.0), 0.0);
        made.sources[corner] = vec4<f32>(sourceAt(face, s, t), 0.0);
        made.colours[corner] = pack4x8unorm(vec4<f32>(clamp(colourAt(face, s, t), vec3<f32>(0.0), vec3<f32>(1.0)), 1.0));
    }
    made.surface = face.surface;
    made.face = face.face;
    made.flags = face.flags | 2u;
    scene[index] = made;
}
)";

constexpr int tileSize = 8;

const juce::String& kernelOf(const char* body, bool hides = false) {
    static std::map<const char*, juce::String> made;
    static std::mutex lock;
    const std::lock_guard guard(lock);
    auto found = made.find(body);
    if (found == made.end()) { found = made.emplace(body, juce::String(shared) + (hides ? hiding : "") + body).first; }
    return found->second;
}

// An exclusive running sum of `count` values into `sums`, its total into
// `totals` at `at`.
void recordScan(Device::Work& work, std::size_t values, std::size_t sums, std::size_t totals, std::uint32_t count, std::uint32_t at) {
    const auto blocks = std::max<std::uint32_t>(1, (count + 1023) / 1024);
    const auto blockSums = work.scratch(blocks * sizeof(std::uint32_t));
    const auto uniform = work.uniform(std::array<std::uint32_t, 4> {count, blocks, at, 0});
    work.run(scanBlocks, {values, sums, blockSums, uniform}, blocks * 64);
    work.run(scanSums, {blockSums, totals, uniform}, 64);
    work.run(scanAdd, {sums, blockSums, uniform}, count);
}

std::array<float, 4> four(const Vec3& value) { return {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z), 0}; }
std::uint32_t packed(const osci::Point& point) {
    const auto unit = [](float value) { return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };
    return unit(point.r) | unit(point.g) << 8 | unit(point.b) << 16 | 255u << 24;
}
osci::Point unpacked(const std::array<float, 4>& position, std::uint32_t colour) {
    const auto unit = [&](int shift) { return static_cast<float>(colour >> shift & 255u) / 255.0f; };
    return osci::Point(position[0], position[1], position[2], unit(0), unit(8), unit(16));
}

template <typename T>
std::vector<T> valuesOf(const std::vector<std::byte>& bytes, std::size_t count) {
    std::vector<T> values(count);
    if (bytes.size() >= count * sizeof(T)) { std::memcpy(values.data(), bytes.data(), count * sizeof(T)); }
    return values;
}

// What a view needs drawn, as the kernels read it: where its tiles and
// pixels start among every view's.
Scene::GpuView gpuViewOf(const Scene::View& view, std::uint32_t tileBase, std::uint32_t firstPixel) {
    const auto layout = view.depth->layout();
    const auto tilesAcross = static_cast<std::uint32_t>((layout.width + tileSize - 1) / tileSize);
    const auto tilesDown = static_cast<std::uint32_t>((layout.height + tileSize - 1) / tileSize);
    Scene::GpuView made {};
    made.position = four(view.eye.position);
    made.right = four(view.eye.right);
    made.up = four(view.eye.up);
    made.forward = four(view.eye.forward);
    made.focal = static_cast<float>(view.projection.focalLength);
    made.parallel = view.projection.parallel ? 1u : 0u;
    made.farthest = static_cast<float>(view.projection.farthest);
    made.span = static_cast<float>(view.projection.span);
    made.originX = static_cast<float>(layout.originX);
    made.originY = static_cast<float>(layout.originY);
    made.size = static_cast<float>(layout.size);
    made.solidOnly = view.solidOnly ? 1u : 0u;
    made.width = static_cast<std::uint32_t>(layout.width);
    made.height = static_cast<std::uint32_t>(layout.height);
    made.tilesAcross = tilesAcross;
    made.tiles = tilesAcross * tilesDown;
    made.firstPixel = firstPixel;
    made.reachX = static_cast<float>(view.reach.x);
    made.reachY = static_cast<float>(view.reach.y);
    made.tileBase = tileBase;
    return made;
}
}

Scene::Triangle Scene::triangle(const std::array<Vec3, 3>& corners, const std::array<osci::Point, 3>& sources, std::uint32_t surface, std::uint32_t face, std::uint32_t flags) {
    Triangle made {};
    for (std::size_t k = 0; k < 3; ++k) {
        made.corners[k] = four(corners[k]);
        made.sources[k] = {sources[k].x, sources[k].y, sources[k].z, 0};
        made.colours[k] = packed(sources[k]);
    }
    made.surface = surface;
    made.face = face;
    made.flags = flags;
    return made;
}

std::optional<Scene::Drawn> Scene::draw(Device& device, const Frame& frame) {
    Drawn drawn;
    // The views, the camera's first (its pixels start the row), then those
    // read back, then the lights'.
    std::vector<const View*> views;
    if (frame.camera != nullptr) { views.push_back(frame.camera); }
    for (const auto& view : frame.also) { views.push_back(&view); }
    const auto firstShadow = views.size();
    for (const auto& view : frame.shadows) { views.push_back(&view); }
    // Those read back: their pixels are a row from the first.
    const auto readCount = frame.readShadows ? views.size() : firstShadow;
    if (views.empty()) { return drawn; }
    std::optional<Tessellation::Plan> plan;
    if (!frame.bent.empty() && !frame.diceViews.empty() && frame.chain != nullptr) {
        plan = Tessellation::plan(device, *frame.chain, frame.bent, frame.diceViews);
        if (!plan.has_value()) { return std::nullopt; }
    }
    const auto& flat = *frame.flat;
    const auto bentCount = plan.has_value() ? plan->pieces : 0u;
    const auto count = static_cast<std::uint32_t>(flat.size()) + bentCount;
    std::vector<GpuView> made;
    std::uint32_t tileTotal = 0, pixelTotal = 0;
    for (const auto* view : views) {
        made.push_back(gpuViewOf(*view, tileTotal, pixelTotal));
        tileTotal += made.back().tiles;
        pixelTotal += made.back().width * made.back().height;
    }
    const auto pixelsOf = [&](std::size_t index) { return static_cast<std::size_t>(made[index].width) * made[index].height; };
    const auto readPixels = readCount < views.size() ? static_cast<std::size_t>(made[readCount].firstPixel) : static_cast<std::size_t>(pixelTotal);
    // Nothing to draw: every buffer empty.
    if (count == 0) {
        for (std::size_t index = 0; index < readCount; ++index) { views[index]->depth->adopt(std::vector<hidden::DepthBuffer::Pixel>(pixelsOf(index)), {}, false); }
        if (!frame.shadows.empty()) {
            auto shadows = std::make_shared<Shadows>();
            for (auto index = firstShadow; index < views.size(); ++index) { shadows->layouts[views[index]->depth] = {made[index].firstPixel, 0, true}; }
            drawn.shadows = std::move(shadows);
        }
        return drawn;
    }
    static const juce::String expandKernel = juce::String(Tessellation::sharedFunctions()) + expanding;
    static const juce::String seeKernel = juce::String(shared) + hiding + seeing;
    // Room for each triangle's slots and their tiles' lists; when that
    // overflows, again with room for more.
    auto slotCapacity = 2 * count + (1u << 20), memberCapacity = 8 * count + (1u << 20);
    for (int attempt = 0; attempt < 3; ++attempt, slotCapacity *= 2, memberCapacity *= 4) {
        const auto planeCapacity = std::max<std::uint32_t>(1, std::min(slotCapacity, pixelTotal));
        const std::array<std::uint32_t, 8> frameValues {static_cast<std::uint32_t>(views.size()), count, slotCapacity, memberCapacity, planeCapacity, tileTotal, pixelTotal, 0};
        const auto gather = frame.camera != nullptr && frame.gather;
        std::vector<std::uint32_t> stats;
        std::vector<hidden::DepthBuffer::Pixel> pixels;
        std::shared_ptr<Device::Kept> sceneKept, planesKept, lightKept, pixelsKept, visibleKept, positionsKept;
        {
            Device::Work work(device);
            // The scene: the flat triangles, then the bent faces' grids.
            const auto flatBuffer = work.upload(flat.empty() ? std::vector<Triangle>(1) : flat);
            const auto scene = work.scratch(static_cast<std::size_t>(count) * sizeof(Triangle));
            const auto counts = work.uniform(std::array<std::uint32_t, 4> {static_cast<std::uint32_t>(flat.size()), bentCount, 0, 0});
            if (plan.has_value() && plan->points > 0) {
                const auto recorded = Tessellation::recordGrids(work, *plan);
                work.run(expandKernel, {recorded.faces, recorded.cells, recorded.starts, recorded.points, flatBuffer, scene, counts}, count);
            } else {
                const auto none = work.scratch(256);
                work.run(expandKernel, {none, none, none, none, flatBuffer, scene, counts}, count);
            }
            // Every view drawn at once.
            const auto viewBuffer = work.upload(made);
            const auto frameBuffer = work.uniform(frameValues);
            const auto slots = work.scratch(static_cast<std::size_t>(slotCapacity) * 72);
            const auto tileCounts = work.buffer(static_cast<std::size_t>(tileTotal + 1) * 4), tileStarts = work.scratch(static_cast<std::size_t>(tileTotal + 1) * 4), tileFill = work.buffer(static_cast<std::size_t>(tileTotal + 1) * 4);
            const auto members = work.scratch(static_cast<std::size_t>(memberCapacity) * 4);
            const auto taken = work.buffer(static_cast<std::size_t>(slotCapacity) * 4), planeIndex = work.scratch(static_cast<std::size_t>(slotCapacity) * 4);
            const auto statsBuffer = work.buffer((8 + views.size()) * 4);
            const auto pixelBuffer = work.scratch(static_cast<std::size_t>(pixelTotal) * 8);
            const auto planeBuffer = work.scratch(static_cast<std::size_t>(planeCapacity) * 64), lightBuffer = work.scratch(static_cast<std::size_t>(planeCapacity) * 16);
            work.run(kernelOf(transforming), {scene, viewBuffer, frameBuffer, slots, tileCounts, statsBuffer}, count);
            recordScan(work, tileCounts, tileStarts, statsBuffer, tileTotal, 4);
            work.run(kernelOf(filling), {slots, viewBuffer, frameBuffer, tileStarts, tileFill, members, statsBuffer}, slotCapacity);
            work.run(kernelOf(rastering), {slots, viewBuffer, frameBuffer, tileStarts, tileCounts, members, pixelBuffer}, tileTotal * 64);
            work.run(kernelOf(marking), {frameBuffer, pixelBuffer, taken}, pixelTotal);
            recordScan(work, taken, planeIndex, statsBuffer, slotCapacity, 2);
            work.run(kernelOf(compacting), {slots, frameBuffer, taken, planeIndex, planeBuffer, lightBuffer, statsBuffer}, slotCapacity);
            work.run(kernelOf(remapping), {frameBuffer, pixelBuffer, planeIndex}, pixelTotal);
            if (gather) {
                const auto visible = work.buffer(static_cast<std::size_t>(2 * count) * 4);
                const auto positions = work.scratch(static_cast<std::size_t>(2 * count) * 4);
                work.run(seeKernel, {scene, viewBuffer, frameBuffer, pixelBuffer, planeBuffer, visible}, count);
                recordScan(work, visible, positions, statsBuffer, 2 * count, 3);
                visibleKept = work.keep(visible);
                positionsKept = work.keep(positions);
                sceneKept = work.keep(scene);
            }
            work.read(statsBuffer);
            if (readCount > 0) {
                work.read(pixelBuffer, readPixels * 8);
                planesKept = work.keep(planeBuffer);
            }
            if (!frame.shadows.empty()) {
                pixelsKept = work.keep(pixelBuffer);
                lightKept = work.keep(lightBuffer);
            }
            const auto results = work.finish();
            if (!results.has_value()) { return std::nullopt; }
            stats = valuesOf<std::uint32_t>((*results)[0], 8 + views.size());
            if (readCount > 0) { pixels = valuesOf<hidden::DepthBuffer::Pixel>((*results)[1], readPixels); }
        }
        if (stats[1] != 0) { continue; }
        if (readCount > 0) {
            const auto planeCount = std::min(stats[2], planeCapacity);
            const auto gatheredCount = gather ? stats[3] : 0u;
            std::vector<hidden::DepthBuffer::DrawnPlane> planes;
            std::vector<Triangle> gathered;
            {
                Device::Work work(device);
                const auto planeBuffer = work.bind(planesKept);
                work.read(planeBuffer, std::max<std::size_t>(planeCount, 1) * 64);
                if (gatheredCount > 0) {
                    const auto scene = work.bind(sceneKept);
                    const auto viewBuffer = work.upload(made);
                    const auto frameBuffer = work.uniform(frameValues);
                    const auto visible = work.bind(visibleKept), positions = work.bind(positionsKept);
                    const auto out = work.scratch(static_cast<std::size_t>(gatheredCount) * sizeof(Triangle));
                    work.run(kernelOf(gathering), {scene, viewBuffer, frameBuffer, visible, positions, out}, count);
                    work.read(out);
                }
                const auto results = work.finish();
                if (!results.has_value()) { return std::nullopt; }
                planes = valuesOf<hidden::DepthBuffer::DrawnPlane>((*results)[0], planeCount);
                if (gatheredCount > 0) { gathered = valuesOf<Triangle>((*results)[1], gatheredCount); }
            }
            // Each view read back with just the planes its pixels take.
            std::vector<std::int32_t> remap(planes.size(), -1);
            for (std::size_t index = 0; index < readCount; ++index) {
                const auto first = static_cast<std::size_t>(made[index].firstPixel);
                std::vector<hidden::DepthBuffer::Pixel> own(pixels.begin() + static_cast<std::ptrdiff_t>(first), pixels.begin() + static_cast<std::ptrdiff_t>(first + pixelsOf(index)));
                std::vector<hidden::DepthBuffer::DrawnPlane> used;
                std::vector<std::size_t> taken;
                for (auto& pixel : own) {
                    const auto owner = static_cast<std::size_t>(pixel.owner);
                    if (!(pixel.w > 0) || owner >= planes.size()) {
                        pixel = {};
                        continue;
                    }
                    if (remap[owner] < 0) {
                        remap[owner] = static_cast<std::int32_t>(used.size());
                        used.push_back(planes[owner]);
                        taken.push_back(owner);
                    }
                    pixel.owner = remap[owner];
                }
                for (const auto owner : taken) { remap[owner] = -1; }
                views[index]->depth->adopt(own, used, stats[8 + index] > 0);
            }
            drawn.triangles.reserve(gathered.size());
            drawn.sources.reserve(gathered.size());
            for (const auto& each : gathered) {
                const auto corner = [&](std::size_t k) { return Vec3 {each.corners[k][0], each.corners[k][1], each.corners[k][2]}; };
                drawn.triangles.push_back({{corner(0), corner(1), corner(2)}, each.surface});
                drawn.sources.push_back({unpacked(each.sources[0], each.colours[0]), unpacked(each.sources[1], each.colours[1]), unpacked(each.sources[2], each.colours[2])});
            }
        }
        if (!frame.shadows.empty()) {
            auto shadows = std::make_shared<Shadows>();
            shadows->pixels = std::move(pixelsKept);
            shadows->planes = std::move(lightKept);
            for (auto index = firstShadow; index < views.size(); ++index) { shadows->layouts[views[index]->depth] = {made[index].firstPixel, 0, stats[8 + index] == 0}; }
            drawn.shadows = std::move(shadows);
        }
        return drawn;
    }
    return std::nullopt;
}
}
