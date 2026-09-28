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
        asset->drawing = std::make_shared<osci::PreparedDrawing>(std::move(lines));
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
        asset->drawing = std::make_shared<osci::PreparedDrawing>(std::move(lines));
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
            track.insert(clip);
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
            const auto big = litNear(points, -.5f, .3f), small = litNear(points, .5f, .2f);
            expect(small > 0);
            expectWithinAbsoluteError(static_cast<double>(big) / small, 2.0, 0.15);
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
        {
            const auto shape = square(1, .1f);
            auto layers = project({{shape, -.4}, {strokes(2, 12), .2, .3}, {shape, .5, -.4, .7}});
            layers.frameRate = 24;
            layers.tracks[0].clips[0].properties["position.x"].setKey({0, -.4, motion::Interpolation::linear});
            layers.tracks[0].clips[0].properties["position.x"].setKey({2, .4, motion::Interpolation::linear});
            const double rate = 44100;
            const motion::PreparedComposition composition(layers, rate);
            expectEquals(composition.beamRate, 48.0);
            motion::BeamRenderer sequential;
            std::vector<osci::Point> reference;
            for (std::int64_t index = 0; index < 20000; ++index) { reference.push_back(sequential.sample(composition, index / rate, index, rate, true, 1)); }
            std::mt19937 random(7);
            motion::BeamRenderer jumping;
            for (int trial = 0; trial < 400; ++trial) {
                const auto index = static_cast<std::int64_t>(random() % reference.size());
                const auto point = jumping.sample(composition, index / rate, index, rate, true, 1);
                expect(point.x == reference[index].x && point.y == reference[index].y && point.r == reference[index].r, "index " + juce::String(index));
            }
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
                expectEquals(mismatches, 0);
            }
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
                logMessage("Dense layers at " + juce::String(rate) + " Hz interleave over " + juce::String(beam.plannedInterleave()) + " cycles; worst lit fraction " + juce::String(worstLit, 3));
                expect(worstLit > 0.3, "every cycle stays mostly lit");
                expect(std::all_of(drawn.begin(), drawn.end(), [](int count) { return count > 180; }), "every layer draws its complete strokes");
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
    }
};
static MotionBeamRendererTest motionBeamRendererTest;
