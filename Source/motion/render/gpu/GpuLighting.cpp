#include "GpuLighting.h"

#include <cstring>
#include <map>

namespace motion::gpu {
namespace {
// Lighting after the chain's functions: illumination and lightAt from
// Lighting.h and HiddenLineScene.h, LightView::reaching and texel, and
// DepthBuffer::shownShare over the copied buffers.
constexpr const char* lighting = R"(
struct Light {
    position: vec4<f32>,  // w: 0 point, 1 spot, 2 directional, 3 ambient
    direction: vec4<f32>, // w: range
    colour: vec4<f32>,    // w: intensity
    shape: vec4<f32>,     // cone, softness, shadows
    firstView: u32, viewCount: u32, padding0: u32, padding1: u32,
}
struct View {
    position: vec4<f32>, right: vec4<f32>, up: vec4<f32>, forward: vec4<f32>,
    focal: f32, parallel: u32, farthest: f32, span: f32,
    buffer: u32, padding0: u32, padding1: u32, padding2: u32,
}
struct Buffer { originX: f32, originY: f32, size: f32, empty: u32, width: i32, height: i32, firstPixel: u32, firstPlane: u32 }
struct Pixel { w: f32, owner: i32 }
struct Plane { w: f32, dwdx: f32, dwdy: f32, padding: f32 }
struct Piece { first: vec4<f32>, second: vec4<f32>, response: f32, padding0: f32, padding1: f32, padding2: f32 }
struct Sample { source: vec3<f32>, program: u32, piece: u32, t: f32, padding0: f32, padding1: f32 }

@group(0) @binding(2) var<storage, read> samples: array<Sample>;
@group(0) @binding(3) var<storage, read> pieces: array<Piece>;
@group(0) @binding(4) var<storage, read> lights: array<Light>;
@group(0) @binding(5) var<storage, read> views: array<View>;
@group(0) @binding(6) var<storage, read> buffers: array<Buffer>;
@group(0) @binding(7) var<storage, read> pixels: array<Pixel>;
@group(0) @binding(8) var<storage, read> planes: array<Plane>;
@group(0) @binding(9) var<storage, read_write> lit: array<vec4<f32>>;

const nearPlane = 0.05;
const depthPush = 2.0e-3;
const normalLift = 1.5;

fn see(view: View, world: vec3<f32>) -> vec3<f32> {
    let offset = world - view.position.xyz;
    return vec3<f32>(dot(offset, view.right.xyz), dot(offset, view.up.xyz), dot(offset, view.forward.xyz));
}
// The point on the view's buffer (x, y, inverse depth), or w < 0 where it has none.
fn project(view: View, eye: vec3<f32>) -> vec3<f32> {
    if (view.parallel == 1u) { return vec3<f32>(eye.x * view.focal, eye.y * view.focal, 1.0 + (view.farthest - eye.z) / view.span); }
    if (!(eye.z > nearPlane)) { return vec3<f32>(0.0, 0.0, -1.0); }
    return vec3<f32>(eye.x / eye.z * view.focal, eye.y / eye.z * view.focal, 1.0 / eye.z);
}
fn shownShare(buffer: Buffer, point: vec3<f32>) -> f32 {
    if (buffer.empty == 1u) { return 1.0; }
    let reach = 1.5;
    let px = (point.x + buffer.originX) / buffer.size;
    let py = (point.y + buffer.originY) / buffer.size;
    let cx = px - 0.5;
    let cy = py - 0.5;
    let x0 = i32(floor(cx - reach)) + 1;
    let y0 = i32(floor(cy - reach)) + 1;
    var shown = 0.0;
    var total = 0.0;
    for (var y = y0; y < y0 + 3; y++) {
        let wy = reach - abs(f32(y) - cy);
        if (!(wy > 0.0)) { continue; }
        for (var x = x0; x < x0 + 3; x++) {
            let wx = reach - abs(f32(x) - cx);
            if (!(wx > 0.0)) { continue; }
            let weight = wx * wy;
            total += weight;
            if (x < 0 || y < 0 || x >= buffer.width || y >= buffer.height) {
                shown += weight;
                continue;
            }
            let here = pixels[buffer.firstPixel + u32(y * buffer.width + x)];
            if (here.w == 0.0) {
                shown += weight;
                continue;
            }
            let plane = planes[buffer.firstPlane + u32(here.owner)];
            if (point.z * (1.0 + depthPush) >= plane.w + plane.dwdx * px + plane.dwdy * py) { shown += weight; }
        }
    }
    return select(1.0, shown / total, total > 0.0);
}
fn reaching(light: Light, view: View, world: vec3<f32>) -> f32 {
    let at = project(view, see(view, world));
    if (at.z < 0.0) { return 1.0; }
    return 1.0 - light.shape.z * (1.0 - shownShare(buffers[view.buffer], at));
}
// A point light's cube face the point is furthest along, else its only view.
fn viewOf(light: Light, world: vec3<f32>) -> View {
    if (light.viewCount != 6u) { return views[light.firstView]; }
    let offset = world - light.position.xyz;
    var best = light.firstView;
    for (var face = light.firstView + 1u; face < light.firstView + 6u; face++) {
        if (dot(offset, views[face].forward.xyz) > dot(offset, views[best].forward.xyz)) { best = face; }
    }
    return views[best];
}
fn texel(view: View, world: vec3<f32>) -> f32 {
    let size = buffers[view.buffer].size / max(1.0e-9, view.focal);
    if (view.parallel == 1u) { return size; }
    return size * max(nearPlane, see(view, world).z);
}

// The light reaching a point (red, green, blue) and how much shadows took.
fn illumination(point: vec3<f32>, given: array<vec3<f32>, 2>, count: i32) -> vec4<f32> {
    var normals = given;
    var total = vec3<f32>(0.0);
    var shade = 0.0;
    for (var index = 0u; index < arrayLength(&lights); index++) {
        let light = lights[index];
        var strength = light.colour.w;
        if (!(strength > 0.0)) { continue; }
        let kind = u32(light.position.w);
        if (kind != 3u) {
            var toward = vec3<f32>(0.0);
            if (kind == 2u) {
                toward = -light.direction.xyz;
            } else {
                let offset = light.position.xyz - point;
                let distance = length(offset);
                if (!(distance > 1.0e-9)) { continue; }
                toward = offset / distance;
                let reach = distance / max(1.0e-9, light.direction.w);
                let falloff = max(0.0, 1.0 - reach * reach);
                strength *= falloff * falloff;
                if (kind == 1u) {
                    let radians = 3.14159265358979323846 / 360.0;
                    let along = -dot(toward, light.direction.xyz);
                    let outer = cos(light.shape.x * radians);
                    let inner = cos(light.shape.x * (1.0 - light.shape.y) * radians);
                    if (inner - outer > 1.0e-9) {
                        let t = clamp((along - outer) / (inner - outer), 0.0, 1.0);
                        strength *= t * t * (3.0 - 2.0 * t);
                    } else {
                        strength *= select(0.0, 1.0, along >= outer);
                    }
                }
            }
            let casts = light.viewCount > 0u && light.shape.z > 0.0;
            if (count > 0) {
                var reached = 0.0;
                if (casts) {
                    let view = viewOf(light, point);
                    let lift = normalLift * texel(view, point);
                    for (var n = 0; n < count; n++) {
                        let facing = dot(normals[n], toward);
                        if (facing == 0.0) { continue; }
                        let through = reaching(light, view, point + normals[n] * select(-lift, lift, facing > 0.0));
                        shade += 1.0 - through;
                        reached += abs(facing) * through;
                    }
                } else {
                    for (var n = 0; n < count; n++) { reached += abs(dot(normals[n], toward)); }
                }
                strength *= reached / f32(count);
            } else if (casts) {
                let through = reaching(light, viewOf(light, point), point);
                shade += 1.0 - through;
                strength *= through;
            }
        }
        total += light.colour.xyz * strength;
    }
    return vec4<f32>(total, shade);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let index = id.x + id.y * groups.x * 64u;
    if (index >= arrayLength(&samples)) { return; }
    let sample = samples[index];
    let point = bend(sample.program, sample.source, vec3<f32>(-1.0));
    if (!finite3(point)) {
        lit[index] = vec4<f32>(0.0, 0.0, 0.0, -1.0);
        return;
    }
    // The faces it runs along there: a smooth surface's turn from the first
    // normal to the second along the piece.
    let piece = pieces[sample.piece];
    var normals = array<vec3<f32>, 2>(piece.first.xyz, piece.second.xyz);
    var count = i32(piece.first.w);
    if (piece.second.w > 0.0) {
        let blended = piece.first.xyz * (1.0 - sample.t) + piece.second.xyz * sample.t;
        normals[0] = select(piece.first.xyz, normalize(blended), length(blended) > 0.0);
        count = 1;
    }
    let total = illumination(point, normals, count);
    lit[index] = vec4<f32>(vec3<f32>(1.0) + piece.response * (total.xyz - vec3<f32>(1.0)), total.w);
}
)";

const juce::String& kernel() {
    static const juce::String source = juce::String(ChainBatch::chainFunctions()) + lighting;
    return source;
}

std::array<float, 4> four(Vec3 value, double w) { return {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z), static_cast<float>(w)}; }
}

void LightingBatch::setLights(const std::vector<hidden::LightView>& frameLights) {
    lights.clear();
    views.clear();
    buffers.clear();
    pixels.clear();
    planes.clear();
    std::map<const hidden::DepthBuffer*, std::uint32_t> copied;
    for (const auto& light : frameLights) {
        const auto& state = light.state;
        const auto kind = state.ambient() ? 3 : state.parallel() ? 2 : state.spot() ? 1 : 0;
        Light made {};
        made.position = four(state.position, kind);
        made.direction = four(state.direction, state.range);
        made.colour = {static_cast<float>(state.colour[0]), static_cast<float>(state.colour[1]), static_cast<float>(state.colour[2]), static_cast<float>(state.intensity)};
        made.shape = {static_cast<float>(state.cone), static_cast<float>(state.softness), static_cast<float>(state.shadows), 0};
        made.firstView = static_cast<std::uint32_t>(views.size());
        for (const auto& view : light.views) {
            auto found = copied.find(view.depth);
            if (found == copied.end()) {
                const auto layout = view.depth->layout();
                Buffer buffer {static_cast<float>(layout.originX), static_cast<float>(layout.originY), static_cast<float>(layout.size), layout.empty ? 1u : 0u, layout.width, layout.height, static_cast<std::uint32_t>(pixels.size()), static_cast<std::uint32_t>(planes.size())};
                view.depth->copyInto([&](float w, std::int32_t owner) { pixels.push_back({w, owner}); }, [&](double w, double dwdx, double dwdy) { planes.push_back({static_cast<float>(w), static_cast<float>(dwdx), static_cast<float>(dwdy), 0}); });
                found = copied.emplace(view.depth, static_cast<std::uint32_t>(buffers.size())).first;
                buffers.push_back(buffer);
            }
            View made_view {};
            made_view.position = four(view.eye.position, 0);
            made_view.right = four(view.eye.right, 0);
            made_view.up = four(view.eye.up, 0);
            made_view.forward = four(view.eye.forward, 0);
            made_view.focal = static_cast<float>(view.projection.focalLength);
            made_view.parallel = view.projection.parallel ? 1u : 0u;
            made_view.farthest = static_cast<float>(view.projection.farthest);
            made_view.span = static_cast<float>(view.projection.span);
            made_view.buffer = found->second;
            views.push_back(made_view);
        }
        made.viewCount = static_cast<std::uint32_t>(light.views.size());
        lights.push_back(made);
    }
}

std::uint32_t LightingBatch::piece(const hidden::LineNormals& normals, float response) {
    Piece made {};
    made.first = four(normals.values[0], normals.count);
    made.second = four(normals.values[1], normals.turns ? 1 : 0);
    made.response = response;
    pieces.push_back(made);
    return static_cast<std::uint32_t>(pieces.size() - 1);
}

void LightingBatch::sample(std::uint32_t program, std::uint32_t piece, const osci::Point& source, float t) {
    Sample made {};
    made.source = {source.x, source.y, source.z};
    made.program = program;
    made.piece = piece;
    made.t = t;
    samples.push_back(made);
}

std::optional<std::vector<std::array<float, 4>>> LightingBatch::run(Device& device) const {
    std::vector<std::array<float, 4>> result;
    if (samples.empty()) { return result; }
    // Storage bindings may not be empty: a list with none binds one unused.
    const auto bytes = [](const auto& list) {
        using Value = typename std::decay_t<decltype(list)>::value_type;
        static const Value none {};
        return list.empty() ? std::as_bytes(std::span(&none, 1)) : std::as_bytes(std::span(list));
    };
    const std::vector<ChainBatch::Step> noSteps(1);
    const std::vector<ChainBatch::Range> noPrograms(1);
    const auto steps = chain.stepData();
    const auto programs = chain.programData();
    const std::array<Device::Input, 9> inputs {steps.empty() ? std::as_bytes(std::span(noSteps)) : std::as_bytes(steps), programs.empty() ? std::as_bytes(std::span(noPrograms)) : std::as_bytes(programs), bytes(samples), bytes(pieces), bytes(lights), bytes(views), bytes(buffers), bytes(pixels), bytes(planes)};
    const auto output = device.run(kernel(), inputs, {}, samples.size() * sizeof(std::array<float, 4>), static_cast<std::uint32_t>(samples.size()));
    if (!output.has_value()) { return std::nullopt; }
    result.resize(samples.size());
    std::memcpy(result.data(), output->data(), result.size() * sizeof(std::array<float, 4>));
    return result;
}
}
