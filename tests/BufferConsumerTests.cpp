#include <juce_audio_basics/juce_audio_basics.h>
#include "../modules/osci_render_core/concurrency/osci_BufferConsumer.h"

#include <thread>

class BufferConsumerHandoffTest : public juce::UnitTest {
public:
    BufferConsumerHandoffTest() : juce::UnitTest("Buffer consumer handoff", "BufferConsumer") {}

    void runTest() override {
        beginTest("Live publication keeps its existing next-sample boundary and selects the latest batch");
        osci::BufferConsumer consumer(8);
        writeRange(consumer, 0, 25);
        expect(consumer.waitUntilFull() != nullptr);
        auto& held = consumer.getBuffer();
        expect(matches(held, 16));
        const auto* heldStorage = held.getReadPointer(0);

        beginTest("A stalled reader cannot block the live producer or lose ownership of its buffer");
        juce::WaitableEvent finished;
        std::thread producer([&] {
            writeRange(consumer, 25, 1000001);
            finished.signal();
        });
        const bool completedWithoutReading = finished.wait(10000);
        expect(completedWithoutReading, "Producer must finish even while the consumer retains a batch");
        if (!completedWithoutReading) {
            consumer.setBlockOnWrite(false);
            consumer.forceNotify();
        }
        producer.join();
        expect(held.getReadPointer(0) == heldStorage);
        expect(matches(held, 16), "Producer overwrote the consumer-owned batch");
        consumer.waitUntilFull();
        expect(matches(consumer.getBuffer(), 999992));

        beginTest("Replaced batches do not leave wake-ups that replay old data");
        juce::WaitableEvent entered, returned;
        std::thread waiter([&] {
            entered.signal();
            consumer.waitUntilFull();
            returned.signal();
        });
        expect(entered.wait(1000));
        expect(!returned.wait(100));
        consumer.forceNotify();
        expect(returned.wait(1000), "forceNotify must release a live waiter without fresh audio");
        waiter.join();

        for (int size : { 1, 7, 800, 4096 }) {
            beginTest("Concurrent live publication, all six channels, batch size " + juce::String(size));
            stressLive(size);
        }

        beginTest("A live reader waiting ahead of a slow producer receives every new publication");
        osci::BufferConsumer slow(7);
        juce::WaitableEvent consumed;
        std::thread slowProducer([&] {
            int sample = 0;
            for (int batch = 0; batch < 100; ++batch) {
                juce::Thread::sleep(2);
                const int end = (batch + 1) * 7 + 1;
                writeRange(slow, sample, end);
                sample = end;
                consumed.wait(1000);
            }
        });
        for (int batch = 0; batch < 100; ++batch) {
            slow.waitUntilFull();
            expect(matches(slow.getBuffer(), batch * 7));
            consumed.signal();
        }
        slowProducer.join();

        beginTest("Recording applies backpressure when its queue is full");
        osci::BufferConsumer recording(8);
        recording.setBlockOnWrite(true);
        writeRange(recording, 0, 16);
        juce::WaitableEvent attempting, written;
        std::thread blockedProducer([&] {
            attempting.signal();
            recording.write(point(16));
            written.signal();
        });
        expect(attempting.wait(1000));
        expect(!written.wait(100), "A full recording queue must not silently discard samples");
        recording.waitUntilFull();
        expect(matches(recording.getBuffer(), 0));
        expect(written.wait(10000), "Recording producer stayed blocked after space became available");
        blockedProducer.join();
        writeRange(recording, 17, 24);
        recording.waitUntilFull();
        expect(matches(recording.getBuffer(), 8));
        recording.waitUntilFull();
        expect(matches(recording.getBuffer(), 16));

        for (int size : { 1, 7, 800 }) {
            beginTest("Recording retains every sample under contention, batch size " + juce::String(size));
            osci::BufferConsumer buffer(size);
            buffer.setBlockOnWrite(true);
            std::thread writer([&] { writeRange(buffer, 0, 1000 * size); });
            bool intact = true;
            for (int batch = 0; batch < 1000; ++batch) {
                buffer.waitUntilFull();
                intact &= matches(buffer.getBuffer(), batch * size);
                if (batch % 31 == 0) {
                    std::this_thread::yield();
                }
            }
            writer.join();
            expect(intact, "Recording lost, duplicated or reordered samples");
        }

        beginTest("Repeated live/recording sessions retain the existing recording contract");
        osci::BufferConsumer switching(8);
        // Switch between completed sessions, with no producer or consumer accessing the buffers.
        // Live deliberately retains one lookahead sample under the existing publication rule.
        int liveSample = 0;
        for (int session = 0; session < 100; ++session) {
            const int count = session == 0 ? 9 : 8;
            writeRange(switching, liveSample, liveSample + count);
            liveSample += count;
            switching.waitUntilFull();
            expect(matches(switching.getBuffer(), session * 8));
            switching.setBlockOnWrite(true);
            writeRange(switching, 10000 + session * 8, 10008 + session * 8);
            switching.waitUntilFull();
            expect(matches(switching.getBuffer(), 10000 + session * 8));
            switching.setBlockOnWrite(false);
        }

        beginTest("Mode change and stop wake a waiting live consumer");
        osci::BufferConsumer idle(8);
        juce::WaitableEvent awake;
        std::thread modeWaiter([&] { idle.waitUntilFull(); awake.signal(); });
        expect(!awake.wait(100));
        idle.setBlockOnWrite(true);
        expect(awake.wait(1000));
        modeWaiter.join();
        idle.setBlockOnWrite(false);
        idle.forceNotify();
        idle.waitUntilFull();

        beginTest("forceNotify also wakes a recording consumer waiting for an incomplete frame");
        osci::BufferConsumer emptyRecording(8);
        emptyRecording.setBlockOnWrite(true);
        juce::WaitableEvent recordingAwake;
        juce::AudioBuffer<float>* stoppedBatch = nullptr;
        std::thread recordingWaiter([&] { stoppedBatch = emptyRecording.waitUntilFull(); recordingAwake.signal(); });
        expect(!recordingAwake.wait(100));
        emptyRecording.forceNotify();
        const bool recordingStopped = recordingAwake.wait(1000);
        expect(recordingStopped);
        if (!recordingStopped) {
            emptyRecording.setBlockOnWrite(false);
        }
        recordingWaiter.join();
        expect(stoppedBatch == nullptr, "An incomplete recording batch must not be rendered after a stop wake-up");

        beginTest("Leaving recording wakes a producer blocked on its full queue");
        osci::BufferConsumer full(8);
        full.setBlockOnWrite(true);
        writeRange(full, 0, 16);
        juce::WaitableEvent unblocked;
        std::thread stoppedWriter([&] { full.write(point(16)); unblocked.signal(); });
        expect(!unblocked.wait(100));
        full.setBlockOnWrite(false);
        expect(unblocked.wait(1000));
        stoppedWriter.join();
    }

private:
    static osci::Point point(int sample) {
        return { float(sample), float(sample + 1000000), float(sample + 2000000),
                 float(sample + 3000000), float(sample + 4000000), float(sample + 5000000) };
    }

    static void writeRange(osci::BufferConsumer& buffer, int first, int end) {
        for (int i = first; i < end; ++i) {
            buffer.write(point(i));
        }
    }

    static bool matches(const juce::AudioBuffer<float>& buffer, int first) {
        for (int channel = 0; channel < 6; ++channel) {
            for (int i = 0; i < buffer.getNumSamples(); ++i) {
                if (buffer.getSample(channel, i) != float(first + i + channel * 1000000)) {
                    return false;
                }
            }
        }
        return true;
    }

    void stressLive(int size) {
        osci::BufferConsumer buffer(size);
        const int batches = juce::jmax(1000, 100000 / size);
        const int last = (batches - 1) * size;
        std::thread writer([&] {
            for (int i = 0; i <= batches * size; ++i) {
                buffer.write(point(i));
                if (i % 127 == 0) {
                    std::this_thread::yield();
                }
            }
        });
        int previous = -1;
        bool intact = true;
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 30000;
        while (previous != last && juce::Time::getMillisecondCounterHiRes() < deadline) {
            buffer.waitUntilFull();
            const auto& frame = buffer.getBuffer();
            const int first = int(frame.getSample(0, 0));
            intact &= first > previous && first % size == 0 && matches(frame, first);
            std::this_thread::yield();
            intact &= matches(frame, first); // Retain ownership while the producer publishes again.
            previous = first;
        }
        writer.join();
        expectEquals(previous, last, "The latest completed frame must eventually be delivered");
        expect(intact, "Torn, duplicated or out-of-order live frame");
    }
};

static BufferConsumerHandoffTest bufferConsumerHandoffTest;
