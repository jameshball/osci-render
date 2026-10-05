#include <JuceHeader.h>
#include <chrono>
#include <random>
#include "../Source/motion/export/SignalExporter.h"

class MotionBeamRendererTest : public juce::UnitTest {
public:
    MotionBeamRendererTest() : juce::UnitTest("Motion beam renderer", "Motion") {}

    static bool dark(const osci::Point& point) { return point.r == 0 && point.g == 0 && point.b == 0; }
    static double distance(const osci::Point& a, const osci::Point& b) { return std::hypot(a.x - b.x, a.y - b.y); }

    static std::shared_ptr<motion::Asset> polygon(motion::Id id, std::vector<osci::Point> corners, bool close = true) {
        if (close) { corners.push_back(corners.front()); }
        std::vector<std::unique_ptr<osci::Shape>> lines;
        for (std::size_t i = 1; i < corners.size(); ++i) { lines.push_back(std::make_unique<osci::Line>(corners[i - 1], corners[i])); }
        auto asset = std::make_shared<motion::Asset>();
        asset->id = id;
        asset->source = std::make_shared<motion::PreparedSource>(std::vector<std::shared_ptr<const motion::PreparedDrawing>> {std::make_shared<motion::PreparedDrawing>(std::move(lines))}, 30.0);
        return asset;
    }
    static std::shared_ptr<motion::Asset> square(motion::Id id, float half) {
        return polygon(id, {{-half, -half}, {half, -half}, {half, half}, {-half, half}});
    }
    // Many disconnected strokes, like typeset text.
    static std::shared_ptr<motion::Asset> strokes(motion::Id id, int count) {
        std::vector<std::unique_ptr<osci::Shape>> lines;
        for (int i = 0; i < count; ++i) {
            const auto x = -0.3f + 0.6f * static_cast<float>(i) / static_cast<float>(count);
            lines.push_back(std::make_unique<osci::Line>(osci::Point(x, -0.05f), osci::Point(x + 0.004f, 0.05f)));
        }
        auto asset = std::make_shared<motion::Asset>();
        asset->id = id;
        asset->source = std::make_shared<motion::PreparedSource>(std::vector<std::shared_ptr<const motion::PreparedDrawing>> {std::make_shared<motion::PreparedDrawing>(std::move(lines))}, 30.0);
        return asset;
    }
    struct Layer { std::shared_ptr<motion::Asset> asset; double x = 0, y = 0, weight = 1; };
    static motion::Project project(const std::vector<Layer>& layers, double duration = 2) {
        motion::Project result;
        result.duration = duration;
        motion::Id id = 1000;
        for (const auto& layer : layers) {
            if (std::none_of(result.assets.begin(), result.assets.end(), [&](const auto& a) { return a->id == layer.asset->id; })) {
                result.assets.push_back(layer.asset);
            }
            motion::Clip clip;
            clip.id = ++id; clip.asset = layer.asset->id; clip.duration = duration;
            clip.properties["position.x"] = motion::Curve(layer.x);
            clip.properties["position.y"] = motion::Curve(layer.y);
            clip.properties["weight"] = motion::Curve(layer.weight);
            motion::Track track;
            track.id = ++id;
            track.insert(clip, motion::Tempo(120));
            result.tracks.push_back(track);
        }
        return result;
    }
    // One whole cycle, played from its first sample.
    static std::vector<osci::Point> cycle(const motion::PreparedComposition& composition, double rate, std::int64_t cycleIndex, motion::BeamRenderer& beam) {
        const auto first = motion::BeamRenderer::cycleStart(cycleIndex, rate, composition.beamRate);
        const auto end = motion::BeamRenderer::cycleStart(cycleIndex + 1, rate, composition.beamRate);
        std::vector<osci::Point> points;
        for (auto index = first; index < end; ++index) { points.push_back(beam.sample(composition, index / rate, index, rate, true, 1)); }
        return points;
    }
    // Lit samples owned by the draw segment whose clip sits at position.x == x.
    static int litNear(const std::vector<osci::Point>& points, float x, float radius) {
        return static_cast<int>(std::count_if(points.begin(), points.end(), [&](const auto& p) { return !dark(p) && std::abs(p.x - x) < radius; }));
    }
    // The renderer's jump length: doubles from the float endpoints.
    static double jump(const osci::Point& a, const osci::Point& b) {
        const auto dx = static_cast<double>(a.x) - b.x, dy = static_cast<double>(a.y) - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    // Total dark travel of the current plan, including the return jump.
    static double travelled(const motion::BeamRenderer& beam) {
        double total = 0;
        for (const auto& segment : beam.plannedSegments()) {
            if (segment.kind == motion::BeamRenderer::Kind::move) { total += jump(segment.from, segment.to); }
        }
        return total;
    }

    void runTest() override {
        beginTest("Cycle rates are whole multiples of the frame rate at or above 45 Hz");
        expectEquals(motion::beamCycleRate(24), 48.0);
        expectEquals(motion::beamCycleRate(25), 50.0);
        expectEquals(motion::beamCycleRate(30), 60.0);
        expectEquals(motion::beamCycleRate(60), 60.0);
        expectEquals(motion::beamCycleRate(12), 48.0);
        expectEquals(motion::beamCycleRate(120), 120.0);
        expectWithinAbsoluteError(motion::beamCycleRate(29.97), 59.94, 1e-9);
        for (const double rate : {44100.0, 47999.0, 48000.0, 192000.0}) {
            for (std::int64_t index : {0ll, 1ll, 799ll, 800ll, 801ll, 123456ll, 9999999ll}) {
                const auto cycle = motion::BeamRenderer::cycleOf(index, rate, 60);
                expect(motion::BeamRenderer::cycleStart(cycle, rate, 60) <= index && index < motion::BeamRenderer::cycleStart(cycle + 1, rate, 60));
            }
        }

        beginTest("Lit neighbours are always spatially continuous; every jump is dark on both sides");
        {
            const auto shape = square(1, .1f);
            const auto layers = project({{shape, -.6}, {shape, .6}, {shape, 0, .6}, {shape, 0, -.6, .5}});
            for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
                const motion::PreparedComposition composition(layers, rate);
                motion::BeamRenderer beam;
                int visibleJumps = 0;
                osci::Point previous;
                for (std::int64_t index = 0; index < static_cast<std::int64_t>(rate / 4); ++index) {
                    const auto point = beam.sample(composition, index / rate, index, rate, true, 1);
                    if (index > 0 && !dark(point) && !dark(previous) && distance(point, previous) > 0.05) { ++visibleJumps; }
                    previous = point;
                }
                expectEquals(visibleJumps, 0, "at " + juce::String(rate) + " Hz");
            }
        }

        beginTest("Closed vector outlines close and keep every corner lit");
        {
            const auto composition = motion::PreparedComposition(project({{square(1, .25f)}}), 48000);
            motion::BeamRenderer beam;
            const auto points = cycle(composition, 48000, 3, beam);
            std::vector<osci::Point> lit;
            std::copy_if(points.begin(), points.end(), std::back_inserter(lit), [](const auto& p) { return !dark(p); });
            expect(lit.size() > 500, "A single layer receives most of the cycle: " + juce::String(static_cast<int>(lit.size())));
            expect(distance(lit.front(), lit.back()) < 1e-4, "The outline closes");
            for (const osci::Point corner : {osci::Point(-.25f, -.25f), osci::Point(.25f, -.25f), osci::Point(.25f, .25f), osci::Point(-.25f, .25f)}) {
                expect(std::any_of(lit.begin(), lit.end(), [&](const auto& p) { return distance(p, corner) < 1e-4; }), "corner lit");
            }
            const auto darkAtOrigin = std::count_if(points.begin(), points.end(), [](const auto& p) { return dark(p) && std::abs(p.x) < 1e-3 && std::abs(p.y) < 1e-3; });
            expectEquals(static_cast<int>(darkAtOrigin), 0, "Idle and dwell samples hold on the path, never at the origin");
        }

        beginTest("Budgets follow projected length so equal weights share brightness per unit length");
        {
            const motion::PreparedComposition composition(project({{square(1, .2f), -.5}, {square(2, .1f), .5}}), 48000);
            motion::BeamRenderer beam;
            const auto points = cycle(composition, 48000, 5, beam);
            const auto big = litNear(points, -.5f, .3f), little = litNear(points, .5f, .2f);
            expect(little > 0);
            expectWithinAbsoluteError(static_cast<double>(big) / little, 2.0, 0.15);
        }

        beginTest("Stroke-heavy drawings are budgeted by their true length, not their probe continuity");
        {
            // 60 strokes of height ~0.1 have the same drawn length (6.0) as a square of half 0.75.
            const motion::PreparedComposition composition(project({{strokes(1, 60), 0, .5}, {square(2, .75f), 0, -.2}}), 192000);
            motion::BeamRenderer beam;
            cycle(composition, 192000, 3, beam);
            std::vector<std::int64_t> counts;
            for (const auto& segment : beam.plannedSegments()) {
                if (segment.kind == motion::BeamRenderer::Kind::draw) { counts.push_back(segment.count); }
            }
            expectEquals(static_cast<int>(counts.size()), 2);
            if (counts.size() == 2) {
                const auto ratio = static_cast<double>(std::max(counts[0], counts[1])) / static_cast<double>(std::min(counts[0], counts[1]));
                expect(ratio < 1.1, "equal drawn lengths get equal budgets: " + juce::String(counts[0]) + " vs " + juce::String(counts[1]));
            }
        }

        beginTest("A fading layer reserves its share: other layers keep their brightness");
        {
            const auto shape = square(1, .15f);
            const motion::PreparedComposition full(project({{shape, -.5}, {shape, .5}}), 48000);
            const motion::PreparedComposition faded(project({{shape, -.5}, {shape, .5, 0, .5}}), 48000);
            motion::BeamRenderer a, b;
            const auto fullPoints = cycle(full, 48000, 7, a), fadedPoints = cycle(faded, 48000, 7, b);
            expect(std::abs(litNear(fullPoints, -.5f, .3f) - litNear(fadedPoints, -.5f, .3f)) <= 2);
            expectWithinAbsoluteError(static_cast<double>(litNear(fadedPoints, .5f, .3f)) / litNear(fullPoints, .5f, .3f), 0.5, 0.05);
        }

        beginTest("Travel order visits nearby layers first");
        {
            const auto shape = square(1, .05f);
            const motion::PreparedComposition composition(project({{shape, -.8}, {shape, .8}, {shape, -.7}, {shape, .7}}), 48000);
            motion::BeamRenderer beam;
            cycle(composition, 48000, 2, beam);
            std::vector<float> order;
            for (const auto& segment : beam.plannedSegments()) {
                if (segment.kind == motion::BeamRenderer::Kind::draw) { order.push_back(segment.from.x); }
            }
            expectEquals(static_cast<int>(order.size()), 4);
            double travel = 0;
            for (std::size_t i = 1; i < order.size(); ++i) { travel += std::abs(order[i] - order[i - 1]); }
            expect(travel < 2.0, "Greedy order travels " + juce::String(travel) + " instead of 4.5 in document order");
        }

        beginTest("Random access, sequential playback and signal export are sample-identical");
        for (const auto& profile : {motion::ScopeProfile{}, motion::scopeProfilePresets[1].profile, motion::ScopeProfile{37.5, 55.25, 20}}) {
            const auto shape = square(1, .1f);
            auto layers = project({{shape, -.4}, {strokes(2, 12), .2, .3}, {shape, .5, -.4, .7}, {strokes(3, 5), -.6, -.5}, {shape, .1, .6}});
            layers.frameRate = 24;
            layers.scope = profile;
            layers.tracks[0].clips[0].properties["position.x"].setKey({0, -.4, motion::Interpolation::linear});
            layers.tracks[0].clips[0].properties["position.x"].setKey({2, .4, motion::Interpolation::linear});
            const double rate = 44100;
            const motion::PreparedComposition composition(layers, rate);
            const auto label = "profile " + juce::String(profile.dwellMicros) + "/" + juce::String(profile.travelMicrosPerUnit) + "/" + juce::String(profile.settleMicros);
            expectEquals(composition.beamRate, 48.0);
            motion::BeamRenderer sequential;
            std::vector<osci::Point> reference;
            for (std::int64_t index = 0; index < 20000; ++index) { reference.push_back(sequential.sample(composition, index / rate, index, rate, true, 1)); }
            std::mt19937 random(7);
            motion::BeamRenderer jumping;
            int differences = 0;
            for (int trial = 0; trial < 400; ++trial) {
                const auto index = static_cast<std::int64_t>(random() % reference.size());
                const auto point = jumping.sample(composition, index / rate, index, rate, true, 1);
                if (point.x != reference[index].x || point.y != reference[index].y || point.r != reference[index].r) { ++differences; }
            }
            expectEquals(differences, 0, label);
            juce::TemporaryFile exported(".wav");
            const std::atomic<bool> cancel{false};
            const auto result = motion::SignalExporter::write(composition, exported.getFile(), rate, cancel);
            expect(result.wasOk(), result.getErrorMessage());
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(exported.getFile().createInputStream().release(), true));
            expect(reader != nullptr);
            if (reader != nullptr) {
                juce::AudioBuffer<float> samples(5, static_cast<int>(reference.size()));
                expect(reader->read(samples.getArrayOfWritePointers(), 5, 0, samples.getNumSamples()));
                int mismatches = 0;
                for (int i = 0; i < samples.getNumSamples(); ++i) {
                    const auto& p = reference[static_cast<std::size_t>(i)];
                    if (samples.getSample(0, i) != p.x || samples.getSample(1, i) != p.y || samples.getSample(2, i) != p.r) { ++mismatches; }
                }
                expectEquals(mismatches, 0, label);
            }
        }

        beginTest("Scope profiles set dwell, settle and travel samples");
        {
            const auto shape = square(1, .02f);
            const auto nan = std::numeric_limits<double>::quiet_NaN();
            for (const auto& profile : {motion::ScopeProfile{}, motion::scopeProfilePresets[1].profile, motion::scopeProfilePresets[2].profile, motion::ScopeProfile{0, 0, 0}, motion::ScopeProfile{nan, 30, 0}}) {
                // Invalid profiles fall back to the analog scope defaults.
                const auto effective = profile.valid() ? profile : motion::ScopeProfile{};
                const auto label = "profile " + juce::String(profile.dwellMicros) + "/" + juce::String(profile.travelMicrosPerUnit) + "/" + juce::String(profile.settleMicros);
                for (const double rate : {48000.0, 192000.0}) {
                    auto layers = project({{shape, -.5}, {shape, .5}});
                    layers.scope = profile;
                    const motion::PreparedComposition composition(layers, rate);
                    motion::BeamRenderer beam;
                    cycle(composition, rate, 3, beam);
                    const auto dwell = std::max<std::int64_t>(1, std::llround(effective.dwellMicros / 1.0e6 * rate));
                    const auto settle = static_cast<std::int64_t>(std::llround(effective.settleMicros / 1.0e6 * rate));
                    const auto travel = [&](const motion::BeamRenderer::Segment& move) {
                        return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(jump(move.from, move.to) * (effective.travelMicrosPerUnit / 1.0e6) * rate)));
                    };
                    const auto segments = beam.plannedSegments();
                    using Kind = motion::BeamRenderer::Kind;
                    expect(segments.size() >= 8, label);
                    if (segments.size() < 8) { continue; }
                    // Arrive and settle, draw, dwell, jump, arrive and settle, draw, dwell, return.
                    const std::array<Kind, 8> kinds {Kind::hold, Kind::draw, Kind::hold, Kind::move, Kind::hold, Kind::draw, Kind::hold, Kind::move};
                    for (std::size_t i = 0; i < kinds.size(); ++i) { expect(segments[i].kind == kinds[i], label + " segment " + juce::String(static_cast<int>(i))); }
                    expectEquals(segments[0].count, dwell + settle, label + " arrival");
                    expectEquals(segments[2].count, dwell, label + " departure");
                    expectEquals(segments[4].count, dwell + settle, label + " arrival after the jump");
                    expectEquals(segments[6].count, dwell, label + " final departure");
                    expectWithinAbsoluteError(jump(segments[3].from, segments[3].to), 1.0, 1e-6, label);
                    expectEquals(segments[3].count, travel(segments[3]), label + " jump");
                    expectEquals(segments[7].count, travel(segments[7]), label + " return");
                }
            }
            // Slow displays need more time per jump, so dense scenes span more cycles.
            std::vector<Layer> dense;
            for (int i = 0; i < 20; ++i) { dense.push_back({strokes(static_cast<motion::Id>(i + 1), 60), (i % 5) * .4 - .8, (i / 5) * .4 - .6}); }
            auto slow = project(dense), fast = project(dense);
            slow.scope = motion::scopeProfilePresets[1].profile;
            fast.scope = motion::scopeProfilePresets[2].profile;
            motion::BeamRenderer slowBeam, fastBeam;
            cycle(motion::PreparedComposition(slow, 192000), 192000, 0, slowBeam);
            cycle(motion::PreparedComposition(fast, 192000), 192000, 0, fastBeam);
            expect(slowBeam.plannedInterleave() > fastBeam.plannedInterleave(), "laser " + juce::String(slowBeam.plannedInterleave()) + " vs fast " + juce::String(fastBeam.plannedInterleave()));
        }

        beginTest("Tour improvement never lengthens the greedy tour and shortens a greedy trap");
        {
            // Nearest-neighbour takes the close pair first and must cross back.
            const auto shape = square(1, .02f);
            const motion::PreparedComposition trap(project({{shape, 0, 0}, {shape, .3, 0}, {shape, .35, .3}, {shape, -.3, .05}, {shape, .65, 0}}), 48000);
            motion::BeamRenderer greedy, improved;
            greedy.setOrderImprovement(false);
            cycle(trap, 48000, 2, greedy);
            cycle(trap, 48000, 2, improved);
            logMessage("Greedy trap travel: greedy " + juce::String(travelled(greedy), 4) + ", improved " + juce::String(travelled(improved), 4));
            expectWithinAbsoluteError(travelled(greedy), 2.2839, 1e-3);
            expectWithinAbsoluteError(travelled(improved), 2.0748, 1e-3);
            std::mt19937 random(11);
            const auto uniform = [&random] { return static_cast<double>(random() % 20001) / 10000.0 - 1.0; };
            double greedyTotal = 0, improvedTotal = 0;
            int shorter = 0, longer = 0, scenes = 0;
            for (int scene = 0; scene < 40; ++scene) {
                const auto count = 3 + static_cast<int>(random() % 30);
                std::vector<Layer> layers;
                for (int i = 0; i < count; ++i) {
                    // Open strokes, so direction matters as well as order.
                    const auto x = uniform() * .8;
                    const auto y = uniform() * .8;
                    const auto dx = static_cast<float>(uniform() * .25);
                    const auto dy = static_cast<float>(uniform() * .25);
                    layers.push_back({polygon(static_cast<motion::Id>(i + 1), {{0, 0}, {dx, dy}}, false), x, y});
                }
                const motion::PreparedComposition composition(project(layers), 48000);
                motion::BeamRenderer before, after;
                before.setOrderImprovement(false);
                cycle(composition, 48000, 1, before);
                cycle(composition, 48000, 1, after);
                if (before.plannedLayers() != after.plannedLayers()) { continue; }
                ++scenes;
                greedyTotal += travelled(before);
                improvedTotal += travelled(after);
                if (travelled(after) > travelled(before) + 1e-6) { ++longer; }
                if (travelled(after) < travelled(before) - 1e-6) { ++shorter; }
            }
            logMessage("Random stroke scenes: greedy travel " + juce::String(greedyTotal, 2) + ", improved " + juce::String(improvedTotal, 2)
                + " (" + juce::String(100.0 * (1.0 - improvedTotal / greedyTotal), 1) + "% shorter); shorter in " + juce::String(shorter) + " of " + juce::String(scenes));
            expectEquals(scenes, 40, "every scene keeps all of its layers");
            expectEquals(longer, 0);
            expect(shorter >= 30);
        }

        beginTest("Twenty dense text-like layers all stay visible every cycle");
        {
            std::vector<Layer> layers;
            for (int i = 0; i < 20; ++i) { layers.push_back({strokes(static_cast<motion::Id>(i + 1), 60), (i % 5) * .4 - .8, (i / 5) * .4 - .6}); }
            for (const double rate : {48000.0, 192000.0}) {
                const motion::PreparedComposition composition(project(layers), rate);
                motion::BeamRenderer beam;
                std::vector<int> drawn(layers.size(), 0);
                double worstLit = 1;
                for (std::int64_t c = 0; c < 16; ++c) {
                    const auto points = cycle(composition, rate, c, beam);
                    const auto lit = std::count_if(points.begin(), points.end(), [](const auto& p) { return !dark(p); });
                    worstLit = std::min(worstLit, static_cast<double>(lit) / points.size());
                    for (std::size_t i = 0; i < layers.size(); ++i) {
                        drawn[i] += static_cast<int>(std::count_if(points.begin(), points.end(), [&](const auto& p) {
                            return !dark(p) && std::abs(p.x - layers[i].x) < .31 && std::abs(p.y - layers[i].y) < .06;
                        }));
                    }
                }
                logMessage("Dense layers at " + juce::String(rate) + " Hz span " + juce::String(beam.plannedInterleave()) + " cycles; worst lit fraction " + juce::String(worstLit, 3));
                expect(worstLit > 0.3, "every cycle stays mostly lit");
                expect(std::all_of(drawn.begin(), drawn.end(), [](int count) { return count > 180; }), "every layer draws its complete strokes");
            }
        }

        beginTest("Oversized content spans cycles and never leaves a cycle dark");
        for (const int count : {1, 2, 3}) {
            std::vector<Layer> layers;
            for (int i = 0; i < count; ++i) { layers.push_back({strokes(static_cast<motion::Id>(i + 1), 400), i * .5 - .5}); }
            const motion::PreparedComposition composition(project(layers), 48000);
            motion::BeamRenderer beam;
            for (std::int64_t c = 0; c < 12; ++c) {
                const auto points = cycle(composition, 48000, c, beam);
                const auto lit = std::count_if(points.begin(), points.end(), [](const auto& p) { return !dark(p); });
                expect(lit > 0, juce::String(count) + " oversized layers, cycle " + juce::String(c) + " is lit");
                expect(beam.plannedInterleave() >= 2 && beam.plannedInterleave() <= motion::BeamRenderer::maximumSpan, "the plan spans several cycles");
            }
        }

        beginTest("Animated sources change frame only between cycles");
        {
            std::vector<motion::PointSample> points;
            for (int frame = 0; frame < 2; ++frame) {
                for (int i = 0; i < 16; ++i) {
                    const auto angle = i / 16.0f * juce::MathConstants<float>::twoPi;
                    points.push_back({frame * .5f + .1f * std::cos(angle), .1f * std::sin(angle), 0, 1, 1, 1});
                }
            }
            auto asset = std::make_shared<motion::Asset>();
            asset->id = 1;
            asset->source = std::make_shared<motion::PreparedSource>(motion::PreparedPointFrames::create(45, 2, 16, std::move(points)).source);
            const motion::PreparedComposition composition(project({{asset}}), 48000);
            motion::BeamRenderer beam;
            for (std::int64_t c = 0; c < 60; ++c) {
                const auto samples = cycle(composition, 48000, c, beam);
                bool left = false, right = false;
                for (const auto& p : samples) {
                    if (dark(p)) { continue; }
                    (p.x > .25f ? right : left) = true;
                }
                expect(left != right, "cycle " + juce::String(c) + " draws exactly one frame");
            }
        }

        beginTest("Geometry through the camera plane never streaks or parks lit at the origin");
        {
            auto layers = project({{square(1, .5f)}});
            motion::Camera camera;
            camera.id = 50;
            // The camera flies through the square: some cycles place it behind the lens.
            camera.properties["position.z"] = motion::Curve(1.0);
            camera.properties["position.z"].setKey({0, 1.0, motion::Interpolation::linear});
            camera.properties["position.z"].setKey({2, -1.0, motion::Interpolation::linear});
            layers.cameras = {camera};
            const motion::PreparedComposition composition(layers, 48000);
            motion::BeamRenderer beam;
            int huge = 0, litOrigin = 0;
            for (std::int64_t index = 0; index < 96000; ++index) {
                const auto p = beam.sample(composition, index / 48000.0, index, 48000, true, 1);
                if (std::abs(p.x) > motion::PreparedComposition::outputLimit || std::abs(p.y) > motion::PreparedComposition::outputLimit) { ++huge; }
                if (!dark(p) && p.x == 0 && p.y == 0) { ++litOrigin; }
            }
            expectEquals(huge, 0);
            expectEquals(litOrigin, 0);
        }

        beginTest("Soundtrack loudness modulation follows project time through clip clocks");
        {
            auto layers = project({{square(1, .1f)}}, 3);
            auto& clip = layers.tracks[0].clips[0];
            clip.start = .2; clip.duration = 2.5; clip.offset = 1; clip.rate = 2;
            clip.properties["position.x"].base = 0;
            motion::Modulator follower;
            follower.id = 96;
            follower.shape.waveform = motion::ModulationWaveform::soundtrack;
            layers.modulators.push_back(follower);
            motion::ModulationRoute route;
            route.id = 95; route.modulator = 96; route.target = clip.id; route.property = "position.x"; route.amount = .5;
            layers.routes.push_back(route);
            // One quiet second, then one loud second.
            std::vector<float> pcm(24000, 0.0f);
            for (std::size_t i = 8000; i < 16000; ++i) { pcm[i] = static_cast<float>(std::sin(i * .3) * .8); }
            const std::array<std::span<const float>, 1> channels {pcm};
            auto audio = std::make_shared<motion::Asset>();
            audio->id = 99; audio->name = "Beat"; audio->audio = motion::PreparedAudio::fromPlanar(8000, channels).audio;
            layers.assets.push_back(audio);
            motion::Clip sound; sound.id = 98; sound.asset = 99; sound.duration = 3;
            sound.properties["gain"] = motion::Curve(1); sound.properties["pan"] = motion::Curve(0);
            motion::Track track; track.id = 97; track.kind = motion::TrackKind::audio; track.clips = {sound};
            layers.tracks.push_back(track);
            const motion::PreparedComposition composition(layers, 48000);
            expect(composition.preparationError.isEmpty(), composition.preparationError);
            if (!composition.clips.empty()) {
                const auto& visual = composition.clips[0];
                const auto quiet = visual.processPoint({0, 0, 0, 1, 1, 1}, .6).x;
                const auto loud = visual.processPoint({0, 0, 0, 1, 1, 1}, 1.6).x;
                expectWithinAbsoluteError(quiet, 0.0f, .01f, "Silence leaves the keyed value");
                expect(loud > .4f, "Loudness pushes the value by up to the amount: " + juce::String(loud));
            }
        }

        beginTest("Twenty-layer renderer throughput diagnostic");
        {
            std::vector<Layer> layers;
            for (int i = 0; i < 20; ++i) { layers.push_back({square(static_cast<motion::Id>(i + 1), .05f), i * .08 - .8, 0, 1}); }
            auto animated = project(layers);
            for (auto& track : animated.tracks) {
                track.clips[0].properties["rotation.z"].setKey({0, 0, motion::Interpolation::linear});
                track.clips[0].properties["rotation.z"].setKey({2, 360, motion::Interpolation::linear});
            }
            const double rate = 192000;
            const motion::PreparedComposition composition(animated, rate);
            motion::BeamRenderer beam;
            double checksum = 0;
            const auto start = std::chrono::steady_clock::now();
            for (std::int64_t index = 0; index < static_cast<std::int64_t>(rate); ++index) {
                const auto p = beam.sample(composition, index / rate, index, rate, true, 1);
                checksum += p.x + p.r;
            }
            const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            expect(std::isfinite(checksum));
            logMessage("20 animated layers, 1 s at 192 kHz: " + juce::String(seconds * 1000, 1) + " ms (" + juce::String(seconds * 100, 2) + "% of realtime)");
        }

        beginTest("Planning cost at the layer limit stays a small share of a cycle");
        {
            std::mt19937 random(5);
            const auto uniform = [&random] { return static_cast<double>(random() % 20001) / 10000.0 - 1.0; };
            for (const std::size_t count : {std::size_t{20}, motion::BeamRenderer::maximumLayers}) {
                std::vector<Layer> layers;
                for (std::size_t i = 0; i < count; ++i) {
                    const auto x = uniform() * .9;
                    const auto y = uniform() * .9;
                    const auto dx = static_cast<float>(uniform() * .05);
                    const auto dy = static_cast<float>(uniform() * .05);
                    layers.push_back({polygon(static_cast<motion::Id>(i + 1), {{0, 0}, {dx, dy}}, false), x, y});
                }
                const double rate = 192000;
                const motion::PreparedComposition composition(project(layers), rate);
                const auto period = 1.0 / composition.beamRate;
                const int plans = count > 100 ? 20 : 200;
                std::array<double, 2> perPlan {};
                for (const bool improving : {false, true}) {
                    motion::BeamRenderer beam;
                    beam.setOrderImprovement(improving);
                    double checksum = 0;
                    const auto start = std::chrono::steady_clock::now();
                    for (int plan = 0; plan < plans; ++plan) {
                        // A reset forces a full plan: span search, measurement, ordering and budgets.
                        beam.reset();
                        const auto first = motion::BeamRenderer::cycleStart(plan, rate, composition.beamRate);
                        checksum += beam.sample(composition, first / rate, first, rate, true, 1).x;
                    }
                    perPlan[improving ? 1 : 0] = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() / plans;
                    expect(std::isfinite(checksum));
                }
                logMessage(juce::String(static_cast<int>(count)) + " layers, one plan: greedy " + juce::String(perPlan[0] * 1.0e6, 1) + " us, improved "
                    + juce::String(perPlan[1] * 1.0e6, 1) + " us (" + juce::String(100.0 * perPlan[1] / period, 2) + "% of a " + juce::String(period * 1000, 2) + " ms cycle)");
                expect(perPlan[1] < period * .25, "planning must leave the audio thread most of each cycle");
            }
        }
    }
};
static MotionBeamRendererTest motionBeamRendererTest;
