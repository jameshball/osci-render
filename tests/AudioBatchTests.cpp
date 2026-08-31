#include <JuceHeader.h>
#include "../modules/osci_gui/visualiser/osci_VisualiserRenderer.h"

#include <thread>

namespace {

// Exercise the real visualiser sizing and background worker without needing a GPU.
// Only the existing per-frame rendering callback is replaced with an observer.
class BatchProbe final : public VisualiserRenderer {
public:
    BatchProbe(VisualiserParameters& parameters, osci::AudioBackgroundThreadManager& manager, double fps, int lastSample)
        : VisualiserRenderer(parameters, manager, {128, 128}, fps), lastSample(lastSample) {
        frames.reserve(512);
    }

    ~BatchProbe() override {
        releaseFirst.signal();
        setShouldBeRunning(false);
    }

    struct Frame {
        int first, size;
        double time;
        const float* data;
        bool intact;
    };
    std::vector<Frame> frames; // Inspected only after the worker stops.
    juce::WaitableEvent firstFrame, completed, releaseFirst;
    bool holdFirst = false; // Configured before starting the worker.

    void runTask(const juce::AudioBuffer<float>& buffer) override {
        const int first = int(buffer.getSample(0, 0));
        bool intact = true;
        for (int ch = 0; ch < 6; ++ch) {
            for (int i = 0; i < buffer.getNumSamples(); ++i) {
                intact &= buffer.getSample(ch, i) == float(first + i + ch * 1000000);
            }
        }
        frames.push_back({first, buffer.getNumSamples(), juce::Time::getMillisecondCounterHiRes(), buffer.getReadPointer(0), intact});
        if (frames.size() == 1) {
            firstFrame.signal();
            if (holdFirst) {
                releaseFirst.wait(10000);
            }
        }
        if (first >= lastSample) {
            completed.signal();
        }
    }

    void stopTask() override { releaseFirst.signal(); }

private:
    const int lastSample;
};

// Observe the real meter's tasks; sizing and pacing are entirely inherited.
class MeterProbe final : public osci::VolumeComponent {
public:
    using VolumeComponent::VolumeComponent;

    ~MeterProbe() override {
        setShouldBeRunning(false);
    }

    void runTask(const juce::AudioBuffer<float>& buffer) override {
        VolumeComponent::runTask(buffer);
        frames.push_back({int(buffer.getSample(0, 0)), buffer.getNumSamples(), juce::Time::getMillisecondCounterHiRes(), nullptr, true});
        if (int(frames.size()) == expectedFrames) {
            completed.signal();
        }
    }

    int expectedFrames = 0; // Configured before any audio is written.
    std::vector<BatchProbe::Frame> frames;
    juce::WaitableEvent completed;
};

juce::AudioBuffer<float> input(int first, int count) {
    juce::AudioBuffer<float> buffer(6, count);
    for (int ch = 0; ch < 6; ++ch) {
        for (int i = 0; i < count; ++i) {
            buffer.setSample(ch, i, float(first + i + ch * 1000000));
        }
    }
    return buffer;
}

class AudioBatchTest final : public juce::UnitTest {
public:
    AudioBatchTest() : juce::UnitTest("Audio batch pacing", "BufferConsumer") {}

    void runTest() override {
        juce::ScopedJuceInitialiser_GUI gui;
        // These parameters are normally owned by the product's AudioProcessor.
        juce::OwnedArray<juce::AudioProcessorParameter> parameterOwner;
        VisualiserParameters parameters;
        for (auto* parameter : parameters.booleans) {
            parameterOwner.add(parameter);
        }
        for (auto* parameter : parameters.integers) {
            parameterOwner.add(parameter);
        }
        for (const auto* effects : {&parameters.effects, &parameters.audioEffects}) {
            for (const auto& effect : *effects) {
                for (auto* parameter : effect->parameters) {
                    for (auto* nested : parameter->getParameters()) {
                        parameterOwner.add(nested);
                    }
                }
            }
        }

        beginTest("Visualiser batches round up to complete frames, including awkward callback sizes");
        {
            for (int block : {32, 64, 799, 800, 801, 1024, 2048, 4096, 8192}) {
                osci::AudioBackgroundThreadManager manager;
                const int batch = ((block + 799) / 800) * 800;
                BatchProbe probe(parameters, manager, 60.0, batch - 800);
                manager.prepare(48000.0, block);
                probe.setShouldBeRunning(true);
                auto partial = input(0, batch - 1);
                probe.write(partial);
                expect(!probe.firstFrame.wait(30), "A partial batch must not be published");
                auto remainder = input(batch - 1, 2);
                probe.write(remainder);
                expect(probe.completed.wait(2000));
                probe.setShouldBeRunning(false);
                expectEquals(int(probe.frames.size()), batch / 800);
                for (int i = 0; i < int(probe.frames.size()); ++i) {
                    expectEquals(probe.frames[i].first, i * 800);
                    expectEquals(probe.frames[i].size, 800);
                    expect(probe.frames[i].intact);
                }
            }
        }

        for (int block : {64, 4096, 8192}) {
            beginTest("Volume meter inherits complete batches and 60 Hz pacing: " + juce::String(block));
            osci::AudioBackgroundThreadManager manager;
            manager.prepare(48000.0, block);
            auto& controls = parameters.effects.front()->parameters;
            MeterProbe meter(manager, *controls.front(), *controls.back(), *parameters.visualiserPaused, {}, {}, 1.0f, 0.0f);
            const int batch = ((block + 799) / 800) * 800;
            meter.expectedFrames = batch / 800;
            auto audio = input(0, batch + 1);
            meter.write(audio);
            expect(meter.completed.wait(2000));
            meter.setShouldBeRunning(false);
            expectEquals(int(meter.frames.size()), meter.expectedFrames);
            for (int i = 0; i < int(meter.frames.size()); ++i) {
                expectEquals(meter.frames[i].first, i * 800);
                expectEquals(meter.frames[i].size, 800);
                if (i > 0) {
                    const double gap = meter.frames[i].time - meter.frames[i - 1].time;
                    expect(gap >= 8.0 && gap < 100.0, "Meter updates must be spread out, not delivered in a burst");
                }
            }
        }

        beginTest("Live batches produce paced, contiguous frame views with no additional sample copy");
        {
            osci::AudioBackgroundThreadManager manager;
            BatchProbe probe(parameters, manager, 20.0, 12000);
            manager.prepare(48000.0, 7000); // 7200-sample batches, three 50 ms frames.
            probe.setShouldBeRunning(true);
            auto first = input(0, 7201);
            probe.write(first);
            expect(probe.firstFrame.wait(2000));
            auto second = input(7201, 7200);
            probe.write(second);
            expect(probe.completed.wait(3000));
            probe.setShouldBeRunning(false);
            checkFrames(probe, {0, 2400, 4800, 7200, 9600, 12000}, 2400);
            if (probe.frames.size() == 6) {
                for (int i = 1; i < 6; ++i) {
                    const double gap = probe.frames[i].time - probe.frames[i - 1].time;
                    expect(gap >= 25.0 && gap < 150.0, "Frames arrived in a burst or stalled");
                }
                expect(probe.frames[1].data == probe.frames[0].data + 2400);
                expect(probe.frames[2].data == probe.frames[0].data + 4800);
            }
        }

        for (int block : {1024, 4096, 8192}) {
            beginTest("Bursty callbacks with jitter remain smoothly paced: " + juce::String(block));
            osci::AudioBackgroundThreadManager manager;
            const int batch = ((block + 799) / 800) * 800;
            const int completeSamples = ((block * 16 - 1) / batch) * batch;
            BatchProbe probe(parameters, manager, 60.0, completeSamples - 800);
            manager.prepare(48000.0, block);
            probe.setShouldBeRunning(true);
            std::thread writer([&] {
                const double start = juce::Time::getMillisecondCounterHiRes();
                for (int callback = 0; callback < 16; ++callback) {
                    const double deadline = start + (callback + 1) * block / 48.0 + (callback % 3 - 1) * 2.0;
                    const double remaining = deadline - juce::Time::getMillisecondCounterHiRes();
                    if (remaining > 0) {
                        juce::Thread::sleep(int(std::ceil(remaining)));
                    }
                    auto audio = input(callback * block, block);
                    probe.write(audio);
                }
            });
            expect(probe.completed.wait(6000));
            writer.join();
            probe.setShouldBeRunning(false);
            expect(int(probe.frames.size()) >= (completeSamples / 800) * 0.8);
            double maxGap = 0.0;
            for (int i = 0; i < int(probe.frames.size()); ++i) {
                expect(probe.frames[i].intact);
                expectEquals(probe.frames[i].size, 800);
                if (i > 0) {
                    expect(probe.frames[i].first > probe.frames[i - 1].first);
                    maxGap = juce::jmax(maxGap, probe.frames[i].time - probe.frames[i - 1].time);
                }
            }
            expect(maxGap < 75.0, "Delivery followed the large audio callback interval instead of the frame interval");
            logMessage("Frames: " + juce::String(int(probe.frames.size())) + ", maximum gap: " + juce::String(maxGap, 2) + " ms");
        }

        beginTest("Recording uses the same large batch but renders all frames without pacing or loss");
        {
            osci::AudioBackgroundThreadManager manager;
            BatchProbe probe(parameters, manager, 1.0, 240000);
            manager.prepare(48000.0, 96000); // Two one-second frames per batch.
            probe.setShouldBeRunning(true);
            probe.setBlockOnAudioThread(true);
            auto audio = input(0, 288000); // Three batches exceed the queue capacity.
            std::thread writer([&] { probe.write(audio); });
            const bool completed = probe.completed.wait(5000);
            expect(completed, "Recording must not be paced to the six-second audio duration");
            if (!completed) {
                probe.setBlockOnAudioThread(false);
            }
            writer.join();
            probe.setShouldBeRunning(false);
            checkFrames(probe, {0, 48000, 96000, 144000, 192000, 240000}, 48000);
            if (probe.frames.size() == 6) {
                expect(probe.frames.back().time - probe.frames.front().time < 2000.0);
            }
        }

        beginTest("Stopping the worker releases an audio producer blocked by recording backpressure");
        {
            osci::AudioBackgroundThreadManager manager;
            BatchProbe probe(parameters, manager, 60.0, 0);
            probe.holdFirst = true;
            manager.prepare(48000.0, 64);
            probe.setShouldBeRunning(true);
            probe.setBlockOnAudioThread(true);
            auto first = input(0, 800);
            manager.write(first);
            expect(probe.firstFrame.wait(2000));

            auto queued = input(800, 1601); // Fill the two-batch queue, then block on one more sample.
            juce::WaitableEvent entered, returned;
            std::thread writer([&] {
                entered.signal();
                manager.write(queued);
                returned.signal();
            });
            expect(entered.wait(1000));
            expect(!returned.wait(100), "Recording should apply backpressure while the worker holds its frame");
            probe.setShouldBeRunning(false);
            const bool stopped = returned.wait(1000);
            expect(stopped, "Stopping must release the writer and the manager lock without a separate recording toggle");
            if (!stopped) {
                probe.setBlockOnAudioThread(false);
            }
            writer.join();
            checkFrames(probe, {0}, 800);
        }

        beginTest("Small callbacks retain the unpaced single-frame path");
        {
            osci::AudioBackgroundThreadManager manager;
            BatchProbe probe(parameters, manager, 1.0, 0);
            manager.prepare(48000.0, 64);
            probe.setShouldBeRunning(true);
            auto audio = input(0, 48001);
            probe.write(audio);
            expect(probe.completed.wait(500), "A one-frame batch should not wait for a pacing deadline");
            probe.setShouldBeRunning(false);
            checkFrames(probe, {0}, 48000);
        }

        beginTest("A stalled live renderer skips stale frames and recovers to the newest frame");
        {
            osci::AudioBackgroundThreadManager manager;
            BatchProbe probe(parameters, manager, 20.0, 12000);
            probe.holdFirst = true;
            manager.prepare(48000.0, 14400);
            probe.setShouldBeRunning(true);
            auto audio = input(0, 14401);
            probe.write(audio);
            expect(probe.firstFrame.wait(2000));
            juce::Thread::sleep(350);
            probe.releaseFirst.signal();
            expect(probe.completed.wait(2000));
            probe.setShouldBeRunning(false);
            checkFrames(probe, {0, 12000}, 2400);
        }

        beginTest("Stopping interrupts a long pacing wait without draining the batch");
        {
            osci::AudioBackgroundThreadManager manager;
            BatchProbe probe(parameters, manager, 1.0, 0);
            manager.prepare(48000.0, 96000);
            probe.setShouldBeRunning(true);
            auto audio = input(0, 96001);
            probe.write(audio);
            juce::Thread::sleep(30);
            const auto started = juce::Time::getMillisecondCounterHiRes();
            probe.setShouldBeRunning(false);
            expect(juce::Time::getMillisecondCounterHiRes() - started < 300.0);
            expect(probe.frames.empty());

            // Restart the same worker/consumer; an interrupted deadline must not survive.
            probe.setShouldBeRunning(true);
            auto resumed = input(96001, 96000);
            const auto resumedAt = juce::Time::getMillisecondCounterHiRes();
            probe.write(resumed);
            expect(probe.completed.wait(2000));
            probe.setShouldBeRunning(false);
            checkFrames(probe, {96000}, 48000);
            if (!probe.frames.empty()) {
                expect(probe.frames.front().time - resumedAt >= 600.0);
            }
        }

        for (int mode = 0; mode < 3; ++mode) {
            beginTest(mode == 0 ? "Entering recording discards remaining live slices"
                               : mode == 1 ? "Stopping recording discards remaining recorded slices"
                                           : "Rapid recording toggles interrupt the old batch even when mode ends unchanged");
            osci::AudioBackgroundThreadManager manager;
            const int next = mode == 2 ? 7200 : 100000;
            BatchProbe probe(parameters, manager, 20.0, next + 4800);
            probe.holdFirst = true;
            manager.prepare(48000.0, 7000);
            probe.setShouldBeRunning(true);
            if (mode == 1) {
                probe.setBlockOnAudioThread(true);
            }
            auto first = input(0, mode == 1 ? 7200 : 7201);
            probe.write(first);
            expect(probe.firstFrame.wait(2000));
            probe.setBlockOnAudioThread(mode != 1);
            if (mode == 2) {
                probe.setBlockOnAudioThread(false);
            }
            probe.releaseFirst.signal();
            auto second = input(mode == 2 ? 7201 : next, mode == 1 ? 7201 : 7200);
            probe.write(second);
            expect(probe.completed.wait(3000));
            probe.setShouldBeRunning(false);
            checkFrames(probe, {0, next, next + 2400, next + 4800}, 2400);
        }

        beginTest("Re-preparing with the FPS-change sentinel retains the real callback size");
        {
            osci::AudioBackgroundThreadManager manager;
            BatchProbe probe(parameters, manager, 30.0, 3200);
            manager.prepare(48000.0, 4096);
            probe.prepare(48000.0, -1); // The same preparation used by setFrameRate().
            probe.setShouldBeRunning(true);
            auto audio = input(0, 4801);
            probe.write(audio);
            expect(probe.completed.wait(2000));
            probe.setShouldBeRunning(false);
            checkFrames(probe, {0, 1600, 3200}, 1600);
        }

        beginTest("Direct offline frame calls bypass batching and timing");
        {
            osci::AudioBackgroundThreadManager manager;
            BatchProbe probe(parameters, manager, 1.0, 48000);
            const int frameSize = probe.prepareTask(48000.0, 48000);
            auto first = input(0, frameSize);
            auto second = input(frameSize, frameSize);
            probe.runTask(first);
            probe.runTask(second);
            checkFrames(probe, {0, 48000}, 48000);
            expect(probe.frames.back().time - probe.frames.front().time < 500.0);
        }
    }

private:
    void checkFrames(const BatchProbe& probe, std::initializer_list<int> firstSamples, int size) {
        expectEquals(int(probe.frames.size()), int(firstSamples.size()));
        int index = 0;
        for (int first : firstSamples) {
            if (index >= int(probe.frames.size())) {
                break;
            }
            const auto& frame = probe.frames[index++];
            expectEquals(frame.first, first);
            expectEquals(frame.size, size);
            expect(frame.intact, "A frame view contained corrupted or discontinuous samples");
        }
    }
};

static AudioBatchTest audioBatchTest;

}
