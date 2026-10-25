#include "GpuChain.h"

#include <cstring>

namespace motion::gpu {
namespace {
// The kernel: each point runs its program's steps. The effects are those of
// osci_PointEffectKernels.h and PreparedEffects.h, in single precision;
// rounding is C's (halves away from zero), not WGSL's (halves to even).
constexpr const char* functions = R"(
struct Step {
    kind: u32, copy: u32, flag: u32, padding: u32,
    v: array<vec4<f32>, 6>,
}
struct Range { first: u32, count: u32 }

@group(0) @binding(0) var<storage, read> steps: array<Step>;
@group(0) @binding(1) var<storage, read> programs: array<Range>;

const pi = 3.14159265358979323846;
const twoPi = 6.28318530717958647692;

fn roundC(x: f32) -> f32 { return sign(x) * floor(abs(x) + 0.5); }
fn finite(x: f32) -> bool { return x == x && abs(x) <= 3.4028234e38; }
fn finite3(v: vec3<f32>) -> bool { return finite(v.x) && finite(v.y) && finite(v.z); }
fn fmodC(a: f32, b: f32) -> f32 { return a - b * trunc(a / b); }
fn wrapAngle(angle: f32) -> f32 { return fmodC(fmodC(angle + pi, twoPi) + twoPi, twoPi) - pi; }
// osci::Point::rotate: about X, then Y, then Z.
fn rotate(p: vec3<f32>, x: f32, y: f32, z: f32) -> vec3<f32> {
    let y2 = cos(x) * p.y - sin(x) * p.z;
    let z2 = sin(x) * p.y + cos(x) * p.z;
    let x2 = cos(y) * p.x + sin(y) * z2;
    let z3 = -sin(y) * p.x + cos(y) * z2;
    return vec3<f32>(cos(z) * x2 - sin(z) * y2, sin(z) * x2 + cos(z) * y2, z3);
}

fn skew(p: vec3<f32>, x: f32, y: f32, z: f32) -> vec3<f32> { return vec3<f32>(p.x + x * p.y, p.y + y * p.z, p.z + z * p.x); }
fn swirl(p: vec3<f32>, amount: f32) -> vec3<f32> {
    let angle = 10.0 * amount * length(p);
    return vec3<f32>(p.x * cos(angle) - p.y * sin(angle), p.x * sin(angle) + p.y * cos(angle), p.z);
}
fn bulge(p: vec3<f32>, amount: f32) -> vec3<f32> {
    let radius = length(p.xy);
    if (radius == 0.0) { return p; }
    let scale = pow(radius, 1.0 - amount) / radius;
    return vec3<f32>(scale * p.x, scale * p.y, p.z);
}
fn ripple(p: vec3<f32>, depth: f32, phase: f32, amount: f32) -> vec3<f32> {
    let distance = 100.0 * amount * (p.x * p.x + p.y * p.y);
    return vec3<f32>(p.x, p.y, p.z + depth * sin(phase * pi + distance));
}
fn vortex(p: vec3<f32>, amount: f32, rotation: f32) -> vec3<f32> {
    let exponent = max(1.0, floor(amount + 0.001));
    let reference = rotation * twoPi;
    if (p.x == 0.0 && p.y == 0.0) { return vec3<f32>(0.0, 0.0, p.z); }
    let theta = atan2(p.y, p.x) - reference;
    let radius = pow(p.x * p.x + p.y * p.y, 0.5 * exponent);
    let angle = exponent * theta + reference;
    return vec3<f32>(radius * cos(angle), radius * sin(angle), p.z);
}
fn bitCrush(p: vec3<f32>, crush: f32) -> vec3<f32> {
    let power = pow(2.0, 1.0 - crush * 0.78) - 1.0;
    let quant = 0.5 * pow(2.0, power * 12.0);
    let dequant = 1.0 / quant;
    return vec3<f32>(dequant * roundC(p.x * quant), dequant * roundC(p.y * quant), dequant * roundC(p.z * quant));
}
fn twist(p: vec3<f32>, amount: f32) -> vec3<f32> { return rotate(p, 0.0, amount * 4.0 * pi * p.y, 0.0); }
fn polygon(p: vec3<f32>, sides: f32, stripeSize: f32, rotation: f32, phase: f32) -> vec3<f32> {
    let count = max(2.0, sides);
    let stripe = pow(0.63 * max(1e-4, stripeSize), 1.5);
    var output = vec3<f32>(0.0);
    if (p.x != 0.0 || p.y != 0.0) {
        let r = length(p.xy);
        let theta = wrapAngle(atan2(-p.x, p.y) - rotation * twoPi + pi) - pi;
        let centre = roundC(theta * count / twoPi) / count * twoPi;
        let distance = r * cos(theta - centre);
        let snapped = max(0.0, (roundC(distance / stripe - phase) + phase) * stripe);
        output.x = snapped / distance * p.x;
        output.y = snapped / distance * p.y;
    }
    let depth = abs(p.z);
    if (depth > 0.0001) { output.z = select(-1.0, 1.0, p.z > 0.0) * max(0.0, (roundC(depth / stripe - phase) + phase) * stripe); }
    return output;
}
fn spiralCrush(p: vec3<f32>, density: f32, spiralTwist: f32, zoomTurns: f32, rotationTurns: f32) -> vec3<f32> {
    let domainX = max(2.0, floor(density + 0.001));
    let domainY = roundC(domainX * spiralTwist);
    let zoom = zoomTurns * twoPi;
    let rotation = rotationTurns * twoPi;
    let domainTheta = atan2(domainY, domainX);
    let scale = length(vec2<f32>(domainX, domainY)) / twoPi;
    var output = vec3<f32>(0.0);
    if (p.x != 0.0 || p.y != 0.0) {
        var cell = rotate(vec3<f32>(atan2(p.x, -p.y) - rotation, log(length(p.xy)) - zoom, 0.0), 0.0, 0.0, domainTheta);
        cell = vec3<f32>(roundC(cell.x * scale), roundC(cell.y * scale), 0.0) / scale;
        cell = rotate(cell, 0.0, 0.0, -domainTheta);
        let snapped = exp(cell.y + zoom);
        let theta = cell.x + rotation;
        output.x = snapped * sin(theta);
        output.y = snapped * -cos(theta);
    }
    if (p.z != 0.0) {
        let logZ = roundC((log(abs(p.z)) - zoom) * scale) / scale + zoom;
        output.z = select(-1.0, 1.0, p.z > 0.0) * exp(logZ);
    }
    return output;
}
fn colour(c: vec3<f32>, hue: f32, saturation: f32, brightness: f32) -> vec3<f32> {
    if (c.r < 0.0) { return c; }
    if (!finite3(c)) { return vec3<f32>(0.0); }
    if (hue == 0.0 && saturation == 1.0 && brightness == 1.0) { return c; }
    let k = clamp(c, vec3<f32>(0.0), vec3<f32>(1.0));
    let maximum = max(k.r, max(k.g, k.b));
    let chroma = maximum - min(k.r, min(k.g, k.b));
    if (maximum == 0.0) { return vec3<f32>(0.0); }
    var angle = 0.0;
    if (chroma > 0.0) {
        if (maximum == k.r) { angle = (k.g - k.b) / chroma; }
        else if (maximum == k.g) { angle = 2.0 + (k.b - k.r) / chroma; }
        else { angle = 4.0 + (k.r - k.g) / chroma; }
    }
    angle = fmodC(angle + hue / 60.0, 6.0);
    if (angle < 0.0) { angle += 6.0; }
    let value = clamp(maximum * brightness, 0.0, 1.0);
    let adjusted = value * clamp(chroma / maximum * saturation, 0.0, 1.0);
    let secondary = adjusted * (1.0 - abs(fmodC(angle, 2.0) - 1.0));
    let floorValue = value - adjusted;
    var rgb = vec3<f32>(0.0);
    if (angle < 1.0) { rgb = vec3<f32>(adjusted, secondary, 0.0); }
    else if (angle < 2.0) { rgb = vec3<f32>(secondary, adjusted, 0.0); }
    else if (angle < 3.0) { rgb = vec3<f32>(0.0, adjusted, secondary); }
    else if (angle < 4.0) { rgb = vec3<f32>(0.0, secondary, adjusted); }
    else if (angle < 5.0) { rgb = vec3<f32>(secondary, 0.0, adjusted); }
    else { rgb = vec3<f32>(adjusted, 0.0, secondary); }
    return rgb + floorValue;
}
fn clipToPlane(p: vec3<f32>, normal: vec3<f32>) -> vec3<f32> {
    let distance = dot(p, normal);
    return p + (max(0.0, distance) - distance) * normal;
}

// Position and colour, through one step.
fn applyStep(step: Step, p: ptr<function, vec3<f32>>, c: ptr<function, vec3<f32>>) {
    let v = step.v;
    if (step.kind == 0u) {
        // The source's colour (white when it has none) times the gains.
        let base = select(*c, vec3<f32>(1.0), (*c).r < 0.0);
        *c = clamp(base * v[0].xyz, vec3<f32>(0.0), vec3<f32>(1.0));
        return;
    }
    if (step.kind == 1u) {
        let local = (*p - v[0].xyz) * v[1].xyz;
        *p = vec3<f32>(dot(v[2].xyz, local), dot(v[3].xyz, local), dot(v[4].xyz, local)) + v[5].xyz;
        if (step.flag == 1u) {
            let base = select(*c, vec3<f32>(1.0), (*c).r < 0.0);
            *c = clamp(base * vec3<f32>(v[0].w, v[1].w, v[2].w), vec3<f32>(0.0), vec3<f32>(1.0));
        }
        if (!finite3(*p) || !finite3(*c)) { *p = vec3<f32>(0.0); *c = vec3<f32>(0.0); }
        return;
    }
    // An effect: values[0] is its strength, which mixes it in.
    let input = *p;
    let inputColour = *c;
    let strength = v[0].x;
    var output = input;
    var outputColour = inputColour;
    let a = v[0].y; let b = v[0].z; let d = v[0].w; let e = v[1].x; let f = v[1].y;
    switch (step.flag) {
        case 0u: { output = skew(input, a, b, d); }
        case 1u: { output = swirl(input, a); }
        case 2u: { output = bulge(input, a); }
        case 3u: { output = ripple(input, a, b, d); }
        case 4u: { output = vortex(input, a, b); }
        case 5u: { outputColour = colour(inputColour, a, b, d); }
        case 6u: { output = bitCrush(input, a); }
        case 7u: { output = twist(input, a); }
        case 8u: { output = polygon(input, a, b, d, e); }
        case 9u: { output = spiralCrush(input, a, b, d, e); }
        case 10u: {
            // Kaleidoscope: segment `copy` of round(a) segments.
            let segments = roundC(a);
            var stood = (1.0 - d) * vec3<f32>(input.y, -input.x, input.z);
            stood.x += d;
            if (step.copy % 2u == 1u) { stood.y *= 1.0 - 2.0 * b; }
            let size = twoPi / segments;
            let upper = vec3<f32>(sin(0.5 * size), -cos(0.5 * size), 0.0);
            let lower = vec3<f32>(upper.x, -upper.y, 0.0);
            var clipped = stood;
            var light = 1.0;
            if (size < pi) {
                clipped = clipToPlane(clipToPlane(stood, upper), lower);
                if (clipped.x < 0.0) { clipped = vec3<f32>(0.0, 0.0, clipped.z); light = 1.0 - e; }
            } else {
                clipped = clipToPlane(stood, select(lower, upper, stood.y > 0.0));
            }
            output = rotate((1.0 - e) * stood + e * clipped, 0.0, 0.0, (f32(step.copy) / segments + f) * twoPi);
            outputColour = inputColour * light;
        }
        case 11u: {
            // Duplicator: copy `copy` of round(a), pushed out by b, turned by d.
            let theta = (f32(step.copy) / roundC(a) + d) * twoPi;
            output = (1.0 - b) * input + b * vec3<f32>(cos(theta), sin(theta), 0.0);
        }
        default: {}
    }
    *p = (1.0 - strength) * input + strength * output;
    *c = inputColour + strength * (outputColour - inputColour);
    if (!finite3(*p) || !finite3(*c)) { *p = vec3<f32>(0.0); *c = vec3<f32>(0.0); }
}

// A point through its program.
fn bend(program: u32, position: vec3<f32>, colour: vec3<f32>) -> vec3<f32> {
    let range = programs[program];
    var p = position;
    var c = colour;
    for (var step = range.first; step < range.first + range.count; step++) {
        applyStep(steps[step], &p, &c);
    }
    return p;
}
)";

constexpr const char* bending = R"(
struct Point { position: vec3<f32>, program: u32, colour: vec4<f32> }
@group(0) @binding(2) var<storage, read> points: array<Point>;
@group(0) @binding(3) var<storage, read_write> bent: array<vec4<f32>>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>) {
    let index = id.x + id.y * groups.x * 64u;
    if (index >= arrayLength(&points)) { return; }
    let point = points[index];
    let p = bend(point.program, point.position, point.colour.xyz);
    bent[index] = vec4<f32>(p, select(0.0, 1.0, finite3(p)));
}
)";

const juce::String& chainKernel() {
    static const juce::String source = juce::String(functions) + bending;
    return source;
}

std::uint32_t effectFlag(EffectKind kind) {
    switch (kind) {
        case EffectKind::skew: return 0;
        case EffectKind::swirl: return 1;
        case EffectKind::bulge: return 2;
        case EffectKind::ripple: return 3;
        case EffectKind::vortex: return 4;
        case EffectKind::colour: return 5;
        case EffectKind::bitCrush: return 6;
        case EffectKind::twist: return 7;
        case EffectKind::polygon: return 8;
        case EffectKind::spiralCrush: return 9;
        case EffectKind::kaleidoscope: return 10;
        case EffectKind::duplicator: return 11;
    }
    return 0;
}

// The steps of a chain, appended as PreparedClipStage::processStage and
// PreparedClip::processPoint run them.
struct Compiler {
    std::vector<ChainBatch::Step>& steps;

    void effects(const std::vector<PreparedEffect>& list, double time) {
        for (const auto& effect : list) {
            const auto parameters = effect.parametersAt(time);
            if (!parameters.active) { continue; }
            ChainBatch::Step step;
            step.kind = ChainBatch::effect;
            step.flag = effectFlag(effect.kind);
            step.copy = static_cast<std::uint32_t>(std::max(0, effect.copy));
            for (std::size_t index = 0; index < parameters.values.size(); ++index) { step.values[index] = static_cast<float>(parameters.values[index]); }
            steps.push_back(step);
        }
    }
    void transform(const TransformPose& pose, bool colour) {
        ChainBatch::Step step;
        step.kind = ChainBatch::transform;
        step.flag = colour ? 1 : 0;
        auto& v = step.values;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            v[axis] = pose.anchor[axis];
            v[4 + axis] = pose.scale[axis];
            for (std::size_t column = 0; column < 3; ++column) { v[8 + 4 * axis + column] = pose.rotation[axis][column]; }
            v[20 + axis] = pose.translation[axis];
        }
        v[3] = pose.colour[0];
        v[7] = pose.colour[1];
        v[11] = pose.colour[2];
        steps.push_back(step);
    }
    void stage(const PreparedClipStage& stage, double time) {
        const auto local = stage.localTime(time);
        const auto& pose = stage.poses.at(local, [&] { return TransformPose::at(stage.curves, local, stage.spatial.get()); });
        ChainBatch::Step gains;
        gains.kind = ChainBatch::colourGains;
        for (std::size_t channel = 0; channel < 3; ++channel) { gains.values[channel] = pose.colour[channel]; }
        steps.push_back(gains);
        effects(stage.effects, local);
        transform(pose, false);
        const auto scope = stage.scopeTime(time);
        effects(stage.trackEffects, scope);
        for (const auto& group : stage.groups) {
            transform(group.pose(scope), true);
            effects(group.effects, scope);
        }
        effects(stage.compositionEffects, scope);
    }
};
}

std::uint32_t ChainBatch::program(const PreparedComposition& composition, const PreparedClip& clip, double time) {
    const auto first = static_cast<std::uint32_t>(steps.size());
    Compiler compiler {steps};
    compiler.stage(clip, time);
    for (const auto& ancestor : clip.ancestors) { compiler.stage(ancestor, time); }
    compiler.effects(clip.rootEffects, time);
    compiler.effects(composition.compositionEffects(), time);
    programs.push_back({first, static_cast<std::uint32_t>(steps.size()) - first});
    return static_cast<std::uint32_t>(programs.size() - 1);
}

void ChainBatch::add(std::uint32_t program, const osci::Point& source) {
    Point point {};
    point.position = {source.x, source.y, source.z};
    point.program = program;
    point.colour = {source.r, source.g, source.b, 0};
    points.push_back(point);
}

void ChainBatch::clear() {
    steps.clear();
    programs.clear();
    points.clear();
}

const char* ChainBatch::chainFunctions() { return functions; }

std::vector<ChainBatch::Step> ChainBatch::stepsOf(std::uint32_t program) const {
    const auto& range = programs.at(program);
    return {steps.begin() + range.first, steps.begin() + range.first + range.count};
}

std::optional<std::vector<std::optional<Vec3>>> ChainBatch::run(Device& device) const {
    std::vector<std::optional<Vec3>> bent;
    if (points.empty()) { return bent; }
    // An empty program list still needs a step to bind.
    const std::vector<Step> none(1);
    const std::array<Device::Input, 3> inputs {std::as_bytes(std::span(steps.empty() ? none : steps)), std::as_bytes(std::span(programs)), std::as_bytes(std::span(points))};
    const auto result = device.run(chainKernel(), inputs, {}, points.size() * 4 * sizeof(float), static_cast<std::uint32_t>(points.size()));
    if (!result.has_value()) { return std::nullopt; }
    bent.reserve(points.size());
    for (std::size_t index = 0; index < points.size(); ++index) {
        std::array<float, 4> value {};
        std::memcpy(value.data(), result->data() + index * sizeof(value), sizeof(value));
        bent.push_back(value[3] > 0 ? std::optional(Vec3 {value[0], value[1], value[2]}) : std::nullopt);
    }
    return bent;
}
}
