#include <JuceHeader.h>
#include <chrono>
#include "../Source/motion/export/SignalExporter.h"

class MotionBeamContinuityTest : public juce::UnitTest {
public:
    MotionBeamContinuityTest() : juce::UnitTest("Motion beam transition blanking", "Motion") {}
    void runTest() override {
        testCompleteVectorTraversal();
        testTraversalEligibilityTransitions();
        constexpr double step = 1.0 / 48000, phaseStep = 60 * step;
        beginTest("Separate object allocations blank both sides of travel without moving geometry");
        auto project = makeProject();
        auto second = project.tracks.front();
        second.id = 4;
        second.clips.front().id = 5;
        second.clips.front().properties["position.x"] = motion::Curve(2);
        project.tracks.push_back(second);
        motion::PreparedComposition multiple(project);
        for (const double phase : {0.5 - phaseStep * 0.5, 0.5 + phaseStep * 0.5, phaseStep * 0.5, 1 - phaseStep * 0.5}) {
            const auto raw = multiple.sample(0.25, phase);
            const auto guarded = multiple.sample(0.25, phase, phaseStep, step);
            expect(!dark(raw) && dark(guarded));
            expectEquals(guarded.x, raw.x);
            expectEquals(guarded.y, raw.y);
        }
        expect(!dark(multiple.sample(0.25, 0.25, phaseStep, step)));

        beginTest("Sample-clock aligned allocations keep both endpoints of every jump dark");
        auto alignedProject = makeProject();
        for (int i = 1; i < 3; ++i) {
            auto layer = alignedProject.tracks.front();
            layer.id = 10 + i;
            layer.clips.front().id = 20 + i;
            layer.clips.front().properties["position.x"] = motion::Curve(i * 0.5);
            alignedProject.tracks.push_back(layer);
        }
        for (const double rate : {44100.0, 48000.0, 96000.0}) {
            motion::PreparedComposition aligned(alignedProject, rate);
            osci::Point previous;
            int visibleJumps = 0;
            for (int i = 0; i < static_cast<int>(rate); ++i) {
                const auto point = aligned.sampleAtClock(0.25, i, rate, false);
                if (i > 0 && std::abs(point.x - previous.x) > 0.1 && (!dark(point) || !dark(previous))) {
                    ++visibleJumps;
                }
                previous = point;
            }
            expectEquals(visibleJumps, 0, "Paused live clock at " + juce::String(rate) + " Hz");
            juce::TemporaryFile exported(".wav");
            const std::atomic<bool> cancel {false};
            const auto result = motion::SignalExporter::write(aligned, exported.getFile(), rate, cancel);
            expect(result.wasOk(), result.getErrorMessage());
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(exported.getFile().createInputStream().release(), true));
            expect(reader != nullptr);
            if (reader != nullptr) {
                juce::AudioBuffer<float> samples(5, static_cast<int>(rate));
                expect(reader->read(samples.getArrayOfWritePointers(), 5, 0, samples.getNumSamples()));
                int exportedJumps = 0;
                for (int i = 1; i < samples.getNumSamples(); ++i) {
                    if (std::abs(samples.getSample(0, i) - samples.getSample(0, i - 1)) <= 0.1) { continue; }
                    for (int channel = 2; channel < 5; ++channel) {
                        if (samples.getSample(channel, i) != 0 || samples.getSample(channel, i - 1) != 0) { ++exportedJumps; }
                    }
                }
                expectEquals(exportedJumps, 0, "Exported XYRGB endpoints at " + juce::String(rate) + " Hz");
            }
        }

        beginTest("Exact transport-clock boundaries blank both adjacent clip and camera samples");
        for (const double rate : {44100.0, 48000.0}) {
            const auto boundaryIndex = rate == 44100 ? 13 : 5;
            const auto boundary = boundaryIndex / rate;
            auto sequential = makeProject();
            sequential.tracks[0].clips[0].duration = boundary;
            auto after = sequential.tracks[0].clips[0];
            after.id = 4;
            after.start = boundary;
            after.duration = 1 - boundary;
            after.properties["position.x"] = motion::Curve(1);
            expect(sequential.tracks[0].insert(after));
            motion::PreparedComposition clips(sequential, rate);
            for (int index : {boundaryIndex - 1, boundaryIndex}) {
                expect(dark(clips.sampleAtClock(index / rate, index, rate)));
            }
            auto cameraProject = makeProject();
            motion::Camera one, two;
            one.id = 10;
            two.id = 11;
            two.properties["position.x"] = motion::Curve(1);
            cameraProject.cameras = {one, two};
            cameraProject.cameraCuts.push_back({12, 11, boundary, 1 - boundary});
            motion::PreparedComposition cameras(cameraProject, rate);
            for (int index : {boundaryIndex - 1, boundaryIndex}) {
                expect(dark(cameras.sampleAtClock(index / rate, index, rate)));
            }
            expect(dark(cameras.sampleAtClock(0, -1, rate)));
            expect(dark(cameras.sampleAtClock(0, 0, std::numeric_limits<double>::infinity())));
        }

        beginTest("Tiny positive drawing weights remain finite and preserve raw zero-span sampling");
        auto tinyProject = makeProject();
        tinyProject.tracks.front().clips.front().properties["weight"] = motion::Curve(std::numeric_limits<double>::denorm_min());
        motion::PreparedComposition tiny(tinyProject);
        expect(!dark(tiny.sample(0.25, 0.0)));
        expect(dark(tiny.sample(0.25, 0.0, phaseStep, step)));

        beginTest("Clip edits and camera cuts suppress cross-boundary beam connections");
        project = makeProject();
        project.tracks.front().clips.front().duration = 0.5;
        auto following = project.tracks.front().clips.front();
        following.id = 6;
        following.start = 0.5;
        following.properties["position.x"] = motion::Curve(2);
        project.tracks.front().insert(following);
        motion::PreparedComposition sequence(project);
        for (double time : {0.5 - step * 0.5, 0.5 + step * 0.5}) {
            expect(!dark(sequence.sample(time, 0.2)));
            expect(dark(sequence.sample(time, 0.2, phaseStep, step)));
        }
        project = makeProject();
        motion::Camera first, other;
        first.id = 10;
        other.id = 11;
        other.properties["position.x"] = motion::Curve(1);
        project.cameras = {first, other};
        project.cameraCuts.push_back({12, 11, 0.5, 0.5});
        motion::PreparedComposition cameras(project);
        for (double time : {0.5 - step * 0.5, 0.5 + step * 0.5}) {
            expect(dark(cameras.sample(time, 0.2, phaseStep, step)));
        }
        expect(!dark(cameras.sample(0.4, 0.2, phaseStep, step)));

        beginTest("Unequal frame timing blanks frame changes, source loops, and negative local times");
        std::vector<motion::PointSample> animated(3 * 16, {0.25f, 0, 0, 1, 1, 1});
        const auto frames = motion::PreparedPointFrames::create(6, 3, 16, std::move(animated));
        const auto timing = motion::FrameTiming::create({100, 300, 100});
        motion::PreparedSource source(frames.source, timing.timing);
        for (double boundary : {0.0, 0.1, 0.4, 0.5, -0.1, -0.4}) {
            for (double offset : {-step * 0.5, step * 0.5}) {
                expect(!dark(source.sample(boundary + offset, 0.2)));
                expect(dark(source.sample(boundary + offset, 0.2, phaseStep, step)));
            }
        }
        expect(!dark(source.sample(0.2, 0.2, phaseStep, step)));
        expect(dark(source.sample(0.2, 0.2, phaseStep, 0.49)), "A span spanning several frame boundaries must not alias to the same frame.");
        expect(dark(source.sample(0.2, 0.2, phaseStep, 2.0)));
        motion::PreparedSource uniform(frames.source);
        expect(dark(uniform.sample(1.0 / 6 - step * 0.5, 0.2, phaseStep, step)));
        expect(!dark(uniform.sample(0.2, 0.2, phaseStep, step)));

        beginTest("Export blanks both samples adjoining unequal-time frame jumps");
        std::vector<motion::PointSample> jumps(32, {-0.5f, 0, 0, 1, 1, 1});
        for (std::size_t i = 16; i < jumps.size(); ++i) { jumps[i].x = 0.5f; }
        const auto jumpFrames = motion::PreparedPointFrames::create(2.0 / 0.153, 2, 16, std::move(jumps));
        const auto jumpTiming = motion::FrameTiming::create({53, 100});
        project = makeProject();
        auto jumpAsset = std::make_shared<motion::Asset>(*project.assets.front());
        jumpAsset->source = std::make_shared<motion::PreparedSource>(jumpFrames.source, jumpTiming.timing);
        project.assets.front() = jumpAsset;
        project.duration = 0.2;
        juce::TemporaryFile wave(".wav");
        std::atomic<bool> cancel {false};
        const auto exported = motion::SignalExporter::write(project, wave.getFile(), 48000, cancel);
        expect(exported.wasOk(), exported.getErrorMessage());
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(wave.getFile().createInputStream().release(), true));
        expect(reader != nullptr);
        if (reader != nullptr) {
            juce::AudioBuffer<float> audio(5, 9600);
            expect(reader->read(audio.getArrayOfWritePointers(), 5, 0, 9600));
            for (int boundary : {2544, 7344}) {
                for (int index : {boundary - 1, boundary}) {
                    for (int channel = 2; channel < 5; ++channel) { expectEquals(audio.getSample(channel, index), 0.0f); }
                }
                expect(std::abs(audio.getSample(0, boundary - 1) - audio.getSample(0, boundary)) > 0.9f);
                expectEquals(audio.getSample(2, boundary + 10), 1.0f);
            }
        }

        beginTest("Source offset and playback rate scale temporal travel guards");
        project.tracks.front().clips.front().offset = 0.013;
        project.tracks.front().clips.front().rate = 2;
        motion::PreparedComposition retimed(project);
        for (double time : {0.02 - step * 0.5, 0.02 + step * 0.5}) {
            expect(!dark(retimed.sample(time, 0.2)));
            expect(dark(retimed.sample(time, 0.2, phaseStep, step)));
        }
        expect(!dark(retimed.sample(0.025, 0.2, phaseStep, step)));

        beginTest("Animated allocation expands guards using actual neighbouring clip phases");
        std::vector<motion::PointSample> points(4096, {0.25f, 0, 0, 1, 1, 1});
        points[901].r = points[901].g = points[901].b = 0; // phase approximately .22
        const auto guards = motion::PreparedPointFrames::create(60, 1, 4096, std::move(points));
        project = makeProject();
        auto asset = std::make_shared<motion::Asset>(*project.assets.front());
        asset->source = std::make_shared<motion::PreparedSource>(guards.source);
        project.assets.front() = asset;
        second = project.tracks.front();
        second.id = 4;
        second.clips.front().id = 5;
        project.tracks.push_back(second);
        auto& weight = project.tracks.front().clips.front().properties["weight"];
        weight.setKey({0.5 - step, 1, motion::Interpolation::linear});
        weight.setKey({0.5 + step, 2, motion::Interpolation::linear});
        motion::PreparedComposition changing(project);
        expect(!dark(changing.sample(0.5, 0.12, phaseStep)), "Frozen allocation does not cross the distant guard.");
        expect(dark(changing.sample(0.5, 0.12, phaseStep, step)), "Actual allocation movement does cross the guard.");
        for (double time : {0.6, 0.25, 0.6, 0.5}) {
            const auto a = changing.sample(time, 0.12, phaseStep, step);
            const auto b = changing.sample(time, 0.12, phaseStep, step);
            expect(a.x == b.x && a.r == b.r, "Seeking order has no sampling history.");
        }
    }
private:
    void testCompleteVectorTraversal() {
        beginTest("A 22-sample vector allocation retains every lit corner and closes the outline");
        auto project = makeProject();
        std::vector<std::unique_ptr<osci::Shape>> lines;
        const std::array<osci::Point, 5> corners {osci::Point(0, .1f), osci::Point(.1f, 0),
            osci::Point(0, -.1f), osci::Point(-.1f, 0), osci::Point(0, .1f)};
        for (std::size_t i = 1; i < corners.size(); ++i) { lines.push_back(std::make_unique<osci::Line>(corners[i - 1], corners[i])); }
        auto asset = std::make_shared<motion::Asset>(*project.assets.front());
        asset->source.reset();
        asset->drawing = std::make_shared<osci::PreparedDrawing>(std::move(lines));
        project.assets.front() = asset;
        project.tracks[0].clips[0].properties["weight"] = motion::Curve(.22);
        auto other = project.tracks.front();
        other.id = 4;
        other.clips[0].id = 5;
        other.clips[0].properties["weight"] = motion::Curve(7);
        other.clips[0].properties["position.x"] = motion::Curve(2);
        project.tracks.push_back(other);
        motion::PreparedComposition composition(project);
        // Clock multiplication can put the boundary sample on either side of
        // the nominal 22-sample slot. Use the actual ownership predicate.
        int count = 0;
        while (composition.selectBeam(.25, std::fmod((12000 + count) * 60.0 / 48000, 1.0)).clip == &composition.clips.front()) { ++count; }
        expect(count == 22 || count == 23);
        std::vector<osci::Point> ordered(static_cast<std::size_t>(count));
        std::size_t nextCorner = 0;
        for (int i = 0; i < count; ++i) {
            const auto point = composition.sampleAtClock((12000 + i) / 48000.0, 12000 + i, 48000);
            ordered[static_cast<std::size_t>(i)] = point;
            if (!dark(point) && nextCorner < corners.size() && point.x == corners[nextCorner].x && point.y == corners[nextCorner].y) { ++nextCorner; }
        }
        expectEquals(static_cast<int>(nextCorner), 5);
        expect(dark(ordered.front()) && dark(ordered.back()));
        for (int i = count - 1; i >= 0; --i) {
            const auto point = composition.sampleAtClock((12000 + i) / 48000.0, 12000 + i, 48000);
            expect(point.x == ordered[static_cast<std::size_t>(i)].x && point.y == ordered[static_cast<std::size_t>(i)].y
                && point.r == ordered[static_cast<std::size_t>(i)].r, "Direct seek and reverse access reproduce the same signal");
        }

        beginTest("Fractional vector slots preserve closure at integral and nonintegral beam clocks");
        for (double rate : {44100.0, 47999.0, 96000.0}) {
            motion::PreparedComposition fractional(project, rate);
            for (int cycle = 15; cycle < 18; ++cycle) {
                const auto first = static_cast<std::int64_t>(std::ceil(cycle * rate / 60));
                const auto end = static_cast<std::int64_t>(std::ceil((cycle + 1) * rate / 60));
                nextCorner = 0;
                for (auto index = first; index < end; ++index) {
                    const auto point = fractional.sampleAtClock(index / rate, index, rate);
                    if (!dark(point) && nextCorner < corners.size() && point.x == corners[nextCorner].x && point.y == corners[nextCorner].y) { ++nextCorner; }
                }
                expectEquals(static_cast<int>(nextCorner), 5, "rate " + juce::String(rate) + ", cycle " + juce::String(cycle));
            }
        }

        beginTest("Insufficient vector budgets keep the existing approximate signal visible");
        project.tracks[0].clips[0].properties["weight"] = motion::Curve(.04);
        motion::PreparedComposition insufficient(project);
        int visible = 0;
        for (int i = 0; i < 4; ++i) {
            const auto point = insufficient.sampleAtClock((12000 + i) / 48000.0, 12000 + i, 48000);
            if (!dark(point)) { ++visible; }
        }
        expect(visible > 0);

        beginTest("Another layer's animated allocation retains the existing continuous sampling");
        project.tracks[0].clips[0].properties["weight"] = motion::Curve(.22);
        auto& animatedWeight = project.tracks[1].clips[0].properties["weight"];
        animatedWeight.setKey({0, 7, motion::Interpolation::linear});
        animatedWeight.setKey({1, 6, motion::Interpolation::linear});
        motion::PreparedComposition animated(project);
        for (int i = 12000; i < 12800; ++i) {
            const auto time = i / 48000.0;
            const auto phase = std::fmod(i * 60.0 / 48000, 1.0);
            const motion::PreparedComposition::SamplingNeighbours neighbours {
                std::fmod((i - 1) * 60.0 / 48000, 1.0), std::fmod((i + 1) * 60.0 / 48000, 1.0),
                (i - 1) / 48000.0, (i + 1) / 48000.0, (i - 1) / 48000.0, (i + 1) / 48000.0};
            const auto expected = animated.sample(time, phase, 60.0 / 48000, 1.0 / 48000, time, &neighbours);
            const auto actual = animated.sampleAtClock(time, i, 48000);
            expect(actual.x == expected.x && actual.y == expected.y && actual.r == expected.r);
        }
    }

    static osci::Point legacyAtClock(const motion::PreparedComposition& composition, std::int64_t index, double rate) {
        const auto phase = [rate](std::int64_t frame) { return std::fmod(static_cast<double>(frame) * 60.0 / rate, 1.0); };
        const auto time = index / rate;
        const motion::PreparedComposition::SamplingNeighbours neighbours {phase(index - 1), phase(index + 1),
            (index - 1) / rate, (index + 1) / rate, (index - 1) / rate, (index + 1) / rate};
        return composition.sample(time, phase(index), 60.0 / rate, 1.0 / rate, time, &neighbours);
    }
    void sameSignal(const osci::Point& actual, const osci::Point& expected) {
        expect(actual.x == expected.x && actual.y == expected.y && actual.z == expected.z
            && actual.r == expected.r && actual.g == expected.g && actual.b == expected.b);
    }
    static motion::Project vectorProject() {
        auto project = makeProject();
        std::vector<std::unique_ptr<osci::Shape>> lines;
        const std::array<osci::Point, 5> corners {osci::Point(0, .1f), osci::Point(.1f, 0),
            osci::Point(0, -.1f), osci::Point(-.1f, 0), osci::Point(0, .1f)};
        for (std::size_t i = 1; i < corners.size(); ++i) { lines.push_back(std::make_unique<osci::Line>(corners[i - 1], corners[i])); }
        auto asset = std::make_shared<motion::Asset>(*project.assets.front());
        asset->source.reset();
        asset->drawing = std::make_shared<osci::PreparedDrawing>(std::move(lines));
        project.assets.front() = asset;
        return project;
    }
    void testTraversalEligibilityTransitions() {
        beginTest("Fractional slots straddling the minimum consistently retain legacy sampling");
        auto project = vectorProject();
        project.tracks[0].clips[0].properties["weight"] = motion::Curve(.066);
        auto other = project.tracks.front();
        other.id = 4; other.clips[0].id = 5;
        other.clips[0].properties["weight"] = motion::Curve(7);
        project.tracks.push_back(other);
        constexpr double fractionalRate = 47999;
        motion::PreparedComposition nearMinimum(project, fractionalRate);
        bool sawSix = false, sawSeven = false;
        for (int cycle = 10; cycle < 50; ++cycle) {
            int owned = 0;
            const auto first = static_cast<std::int64_t>(std::ceil(cycle * fractionalRate / 60));
            const auto end = static_cast<std::int64_t>(std::ceil((cycle + 1) * fractionalRate / 60));
            for (auto index = first; index < end; ++index) {
                const auto phase = std::fmod(index * 60.0 / fractionalRate, 1.0);
                if (nearMinimum.selectBeam(index / fractionalRate, phase).clip != &nearMinimum.clips.front()) { continue; }
                ++owned;
                sameSignal(nearMinimum.sampleAtClock(index / fractionalRate, index, fractionalRate), legacyAtClock(nearMinimum, index, fractionalRate));
            }
            sawSix = sawSix || owned == 6;
            sawSeven = sawSeven || owned == 7;
        }
        expect(sawSix && sawSeven, "Fixture exercises both sides of the seven-sample minimum");

        beginTest("A same-clip traversal-to-legacy change has two dark transition samples");
        project = vectorProject();
        auto& weight = project.tracks[0].clips[0].properties["weight"];
        weight.setKey({0, 1, motion::Interpolation::hold});
        weight.setKey({.5, 1, motion::Interpolation::linear});
        weight.setKey({1, .8, motion::Interpolation::linear});
        motion::PreparedComposition transition(project);
        // Cycle 29 touches the end of the constant interval and falls back;
        // cycle 28 is fully contained. Clip identity never changes here.
        constexpr std::int64_t switchIndex = 29 * 800;
        for (const auto index : {switchIndex - 1, switchIndex}) {
            expect(dark(transition.sampleAtClock(index / 48000.0, index, 48000)));
        }
        expect(!dark(legacyAtClock(transition, switchIndex, 48000)), "Transition blanking is additional to legacy ownership guards");
        expect(!dark(transition.sampleAtClock((switchIndex - 2) / 48000.0, switchIndex - 2, 48000)));
        expect(!dark(transition.sampleAtClock((switchIndex + 1) / 48000.0, switchIndex + 1, 48000)));

        beginTest("Ancestor weight modulation disables traversal even with static clip weights");
        project = vectorProject();
        motion::Group parent;
        parent.id = 20;
        parent.properties["weight"].modulation.enabled = true;
        parent.properties["weight"].modulation.amount = .25;
        parent.properties["weight"].modulation.rateHz = 60;
        motion::Group child;
        child.id = 21; child.parent = 20;
        project.groups = {parent, child};
        project.tracks.front().group = 21;
        motion::PreparedComposition modulated(project);
        for (std::int64_t index = 12000; index < 13600; ++index) {
            sameSignal(modulated.sampleAtClock(index / 48000.0, index, 48000), legacyAtClock(modulated, index, 48000));
        }

        beginTest("Mixed vector traversal leaves overlapping MIDI oscillator samples unchanged");
        project = vectorProject();
        other = project.tracks.front();
        other.id = 4; other.clips[0].id = 5;
        other.clips[0].midi = motion::MidiNotes::create({{1, 0, 2, 69, 127, 1}, {2, 0, 2, 81, 83, 1}}).source;
        project.tracks.push_back(other);
        motion::PreparedComposition mixed(project);
        expect(mixed.preparationError.isEmpty());
        int midiSamples = 0;
        for (std::int64_t index = 12000; index < 14400; ++index) {
            const auto phase = std::fmod(index * 60.0 / 48000, 1.0);
            const auto selected = mixed.selectBeam(index / 48000.0, phase, index / 48000.0);
            if (selected.note == 0) { continue; }
            ++midiSamples;
            sameSignal(mixed.sampleAtClock(index / 48000.0, index, 48000), legacyAtClock(mixed, index, 48000));
        }
        expect(midiSamples > 100);

        beginTest("Twenty-layer ancestor allocation sampling timing diagnostic");
        project = vectorProject();
        project.tracks.clear();
        parent.properties["weight"].modulation.enabled = false;
        project.groups = {parent, child};
        for (int layer = 0; layer < 20; ++layer) {
            motion::Track track;
            track.id = 100 + layer;
            track.group = 21;
            motion::Clip clip;
            clip.id = 200 + layer; clip.asset = 1; clip.duration = 1;
            clip.properties["position.x"] = motion::Curve(layer * .02);
            track.clips.push_back(clip);
            project.tracks.push_back(track);
        }
        motion::PreparedComposition dense(project);
        const auto measure = [&](bool complete) {
            double checksum = 0;
            const auto start = std::chrono::steady_clock::now();
            for (std::int64_t index = 12000; index < 16800; ++index) {
                const auto point = complete ? dense.sampleAtClock(index / 48000.0, index, 48000) : legacyAtClock(dense, index, 48000);
                checksum += point.x + point.y + point.r;
            }
            const auto milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            expect(std::isfinite(checksum));
            logMessage(juce::String(complete ? "Traversal" : "Legacy") + " 20 layers / 2 ancestors / 4800 samples: "
                + juce::String(milliseconds, 3) + " ms; checksum " + juce::String(checksum, 5));
        };
        measure(false);
        measure(true);
    }

    static bool dark(const osci::Point& point) { return point.r == 0 && point.g == 0 && point.b == 0; }
    static motion::Project makeProject() {
        std::vector<motion::PointSample> points(16, {0.25f, -0.25f, 0, 1, 1, 1});
        const auto frames = motion::PreparedPointFrames::create(60, 1, 16, std::move(points));
        auto asset = std::make_shared<motion::Asset>();
        asset->id = 1;
        asset->source = std::make_shared<motion::PreparedSource>(frames.source);
        motion::Clip clip;
        clip.id = 2;
        clip.asset = 1;
        clip.duration = 1;
        motion::Track track;
        track.id = 3;
        track.insert(clip);
        motion::Project project;
        project.duration = 1;
        project.assets.push_back(asset);
        project.tracks.push_back(track);
        return project;
    }
};
static MotionBeamContinuityTest motionBeamContinuityTest;
