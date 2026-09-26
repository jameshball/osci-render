#include <JuceHeader.h>
#include "../Source/motion/export/SignalExporter.h"

class MotionBeamContinuityTest : public juce::UnitTest {
public:
    MotionBeamContinuityTest() : juce::UnitTest("Motion beam transition blanking", "Motion") {}
    void runTest() override {
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
