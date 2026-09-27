#include <JuceHeader.h>
#include "../Source/motion/export/SignalExporter.h"
#include <thread>

class MotionSignalExportTest : public juce::UnitTest {
public:
    MotionSignalExportTest() : juce::UnitTest("Motion offline XYRGB signal export", "Motion") {}

    void runTest() override {
        juce::TemporaryFile directory(".motion-signal-export-tests");
        const auto created = directory.getFile().createDirectory();
        beginTest("The exporter writes exact-length five-channel floating-point XYRGB samples");
        expect(created.wasOk(), created.getErrorMessage());
        if (created.failed()) {
            return;
        }
        constexpr double sampleRate = 48000;
        constexpr int frames = 8193;
        auto project = makeProject((frames - 0.4) / sampleRate);
        std::atomic<bool> cancel { false };
        std::atomic<double> progress { 0.0 };
        const auto firstFile = directory.getFile().getChildFile("first.wav");
        const auto exported = motion::SignalExporter::write(project, firstFile, sampleRate, cancel, &progress);
        expect(exported.wasOk(), exported.getErrorMessage());
        if (exported.failed()) {
            return;
        }
        expectEquals(progress.load(), 1.0);
        auto reader = read(firstFile);
        expect(reader != nullptr);
        if (reader == nullptr) {
            return;
        }
        expectEquals(static_cast<int>(reader->numChannels), 5);
        expectEquals(static_cast<int>(reader->bitsPerSample), 32);
        expect(reader->usesFloatingPointData);
        expectEquals(reader->sampleRate, sampleRate);
        expectEquals(reader->lengthInSamples, static_cast<juce::int64>(frames));
        juce::AudioBuffer<float> samples(5, frames);
        expect(reader->read(samples.getArrayOfWritePointers(), 5, 0, frames));
        for (const auto index : { 0, 1, 799, 800, 4095, 4096, frames - 1 }) {
            // Interior complete cycles reserve dark endpoints and sample both
            // lit ends of the line. Boundary cycles retain continuous sampling.
            const auto phase = index == 4095 || index == 4096 ? static_cast<double>(index % 800 - 1) / 797
                : std::fmod(index * 60.0 / sampleRate, 1.0);
            expectWithinAbsoluteError(samples.getSample(0, index), 2.0f + static_cast<float>(phase), 0.000001f);
            expectWithinAbsoluteError(samples.getSample(1, index), -3.0f + static_cast<float>(phase), 0.000001f);
            // This open line jumps from (3,-2) back to (2,-3) each cycle.
            // Its adjacent output samples must be dark.
            // The final sample also borders silence at the composition end.
            const bool travel = index == 0 || index == 1 || index == 799 || index == 800 || index == frames - 1;
            expectWithinAbsoluteError(samples.getSample(2, index), travel ? 0.0f : 0.2f, 0.000001f);
            expectWithinAbsoluteError(samples.getSample(3, index), travel ? 0.0f : 0.4f, 0.000001f);
            expectWithinAbsoluteError(samples.getSample(4, index), travel ? 0.0f : 0.8f, 0.000001f);
        }
        reader.reset();

        beginTest("Repeated exports of the same snapshot are byte-for-byte deterministic");
        const auto secondFile = directory.getFile().getChildFile("second.wav");
        motion::PreparedComposition prepared(project);
        const auto repeated = motion::SignalExporter::write(prepared, secondFile, sampleRate, cancel);
        expect(repeated.wasOk(), repeated.getErrorMessage());
        juce::MemoryBlock firstBytes, secondBytes;
        expect(firstFile.loadFileAsData(firstBytes));
        expect(secondFile.loadFileAsData(secondBytes));
        expect(firstBytes == secondBytes);

        beginTest("Wrapping a visual in a neutral reusable instance preserves every exported sample");
        auto nestedProject = project;
        auto definition = std::make_shared<motion::CompositionDefinition>();
        definition->id = 900; definition->duration = project.duration;
        definition->tracks = project.tracks;
        nestedProject.definitions = {definition};
        motion::Clip instance; instance.id = 901; instance.composition = 900; instance.duration = project.duration;
        motion::Track instanceTrack; instanceTrack.id = 902; instanceTrack.clips = {instance};
        nestedProject.tracks = {instanceTrack};
        const auto nestedFile = directory.getFile().getChildFile("nested.wav");
        const motion::PreparedComposition nested(nestedProject);
        expect(nested.preparationError.isEmpty(), nested.preparationError);
        const auto nestedExport = motion::SignalExporter::write(nested, nestedFile, sampleRate, cancel);
        expect(nestedExport.wasOk(), nestedExport.getErrorMessage());
        juce::MemoryBlock nestedBytes;
        expect(nestedFile.loadFileAsData(nestedBytes));
        expect(nestedBytes == firstBytes, "Nested stage preparation must preserve source signal and traversal blanking");
        expect(nestedFile.deleteFile());

        beginTest("Subsampled point-source travel is blanked in live composition sampling and XYRGB export");
        std::vector<motion::PointSample> points(4096, {0.25f, -0.25f, 0, 1, 1, 1});
        points[103].r = points[103].g = points[103].b = 0;
        const auto pointFrames = motion::PreparedPointFrames::create(60, 1, 4096, std::move(points));
        expect(static_cast<bool>(pointFrames), juce::String(pointFrames.error));
        if (pointFrames) {
            auto guardedProject = makeProject(1.0 / 60);
            auto pointAsset = std::make_shared<motion::Asset>(*guardedProject.assets.front());
            pointAsset->source = std::make_shared<motion::PreparedSource>(pointFrames.source);
            guardedProject.assets.front() = pointAsset;
            motion::PreparedComposition guarded(guardedProject);
            // Raw samples at 20/800 and 21/800 miss source sample 103.
            // Both neighbours must be dark, while distant drawing stays lit.
            expect(guarded.sample(0, 20.0 / 800).r > 0);
            for (const int index : {20, 21}) {
                const auto point = guarded.sample(0, index / 800.0, 1.0 / 800);
                expectEquals(point.r, 0.0f);
                expectEquals(point.g, 0.0f);
                expectEquals(point.b, 0.0f);
                expectEquals(point.x, 0.25f);
            }
            const auto guardFile = directory.getFile().getChildFile("guard.wav");
            const auto guardExport = motion::SignalExporter::write(guarded, guardFile, sampleRate, cancel);
            expect(guardExport.wasOk(), guardExport.getErrorMessage());
            auto guardReader = read(guardFile);
            expect(guardReader != nullptr);
            if (guardReader != nullptr) {
                juce::AudioBuffer<float> guardSamples(5, 800);
                expect(guardReader->read(guardSamples.getArrayOfWritePointers(), 5, 0, 800));
                for (const int index : {20, 21}) {
                    for (int channel = 2; channel < 5; ++channel) { expectEquals(guardSamples.getSample(channel, index), 0.0f); }
                }
                expectWithinAbsoluteError(guardSamples.getSample(2, 100), 0.2f, 0.000001f);
            }
            guardReader.reset();
            expect(guardFile.deleteFile());
            // A second visible track halves each object's phase allocation.
            auto secondTrack = guardedProject.tracks.front();
            secondTrack.id = 4;
            secondTrack.clips.front().id = 5;
            guardedProject.tracks.push_back(std::move(secondTrack));
            motion::PreparedComposition shared(guardedProject);
            expectEquals(shared.sample(0, 10.0 / 800, 1.0 / 800).r, 0.0f);
            expectEquals(shared.sample(0, 11.0 / 800, 1.0 / 800).r, 0.0f);
        }

        beginTest("Cancellation before export preserves an existing destination");
        cancel.store(true);
        const auto cancelled = motion::SignalExporter::write(project, firstFile, sampleRate, cancel, &progress);
        expect(cancelled.failed());
        expectEquals(progress.load(), 0.0);
        expectUnchanged(firstFile, firstBytes);

        beginTest("Cancellation after writing begins preserves the existing destination");
        cancel.store(false);
        progress.store(0.0);
        std::atomic<bool> finished { false };
        project.duration = 600;
        std::thread cancelAfterFirstBlock([&] {
            while (!finished.load(std::memory_order_acquire)) {
                if (progress.load(std::memory_order_relaxed) > 0.0) {
                    cancel.store(true, std::memory_order_relaxed);
                    return;
                }
                std::this_thread::yield();
            }
        });
        const auto interrupted = motion::SignalExporter::write(project, firstFile, sampleRate, cancel, &progress);
        finished.store(true, std::memory_order_release);
        cancelAfterFirstBlock.join();
        expect(interrupted.failed());
        expect(progress.load() < 1.0);
        expectUnchanged(firstFile, firstBytes);

        beginTest("Invalid timing fails before replacing a destination");
        cancel.store(false);
        project.duration = 1;
        for (const auto rate : { 0.0, -48000.0, 48000.5, std::numeric_limits<double>::infinity() }) {
            const auto invalid = motion::SignalExporter::write(project, firstFile, rate, cancel);
            expect(invalid.failed());
            expectUnchanged(firstFile, firstBytes);
        }
        for (const auto duration : { 0.0, -1.0, 1.0e-10, std::numeric_limits<double>::quiet_NaN() }) {
            project.duration = duration;
            const auto invalid = motion::SignalExporter::write(project, firstFile, sampleRate, cancel);
            expect(invalid.failed());
            expectUnchanged(firstFile, firstBytes);
        }
        expectEquals(directory.getFile().findChildFiles(juce::File::findFiles, false).size(), 2);
    }

private:
    static motion::Project makeProject(double duration) {
        auto asset = std::make_shared<motion::Asset>();
        asset->id = 1;
        asset->name = "Export line";
        std::vector<std::unique_ptr<osci::Shape>> shapes;
        shapes.push_back(std::make_unique<osci::Line>(osci::Point(2, -3, 0, 1, 1, 1), osci::Point(3, -2, 0, 1, 1, 1)));
        asset->drawing = std::make_shared<osci::PreparedDrawing>(std::move(shapes));
        motion::Clip clip;
        clip.id = 2;
        clip.asset = asset->id;
        clip.duration = 1.0;
        clip.properties["red"] = motion::Curve(0.2);
        clip.properties["green"] = motion::Curve(0.4);
        clip.properties["blue"] = motion::Curve(0.8);
        motion::Track track;
        track.id = 3;
        track.insert(std::move(clip));
        motion::Project project;
        project.duration = duration;
        project.assets.push_back(std::move(asset));
        project.tracks.push_back(std::move(track));
        return project;
    }

    static std::unique_ptr<juce::AudioFormatReader> read(const juce::File& file) {
        auto input = file.createInputStream();
        if (input == nullptr) {
            return {};
        }
        juce::WavAudioFormat format;
        return std::unique_ptr<juce::AudioFormatReader>(format.createReaderFor(input.release(), true));
    }

    void expectUnchanged(const juce::File& file, const juce::MemoryBlock& expected) {
        juce::MemoryBlock actual;
        expect(file.loadFileAsData(actual));
        expect(actual == expected, "Export replaced the existing destination after cancellation or failure.");
    }
};
static MotionSignalExportTest motionSignalExportTest;
