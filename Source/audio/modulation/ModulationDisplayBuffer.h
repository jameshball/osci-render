#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

// One audio-thread writer, one UI-thread reader. Display data may be dropped;
// the audio callback never waits for a graph or allocates storage.
class ModulationDisplayBuffer {
public:
    struct Sample {
        float value = 0.0f;
        float position = 0.0f;
        bool active = false;
        bool reset = false;
    };

    void prepare(double sampleRate, int blockSize) {
        samplesPerPoint = juce::jmax(1, juce::roundToInt(sampleRate / 120.0));
        remaining = samplesPerPoint;
        interval.store(samplesPerPoint / sampleRate);
        pointsPerBlock.store(juce::jmax(1, (blockSize + samplesPerPoint - 1) / samplesPerPoint));
        resetMarker();
    }

    void resetMarker() { resetPending = true; } // Audio thread only.

    // Generate audio in chunks ending at display-sample boundaries. The callback
    // returns the actual state at each chunk's end; no phase prediction is needed.
    template <typename ProcessChunk>
    void process(int numSamples, ProcessChunk&& processChunk) {
        for (int offset = 0; offset < numSamples;) {
            const int count = juce::jmin(remaining, numSamples - offset);
            auto sample = processChunk(offset, count);
            offset += count;
            remaining -= count;
            if (remaining == 0) {
                auto write = fifo.write(1);
                if (write.blockSize1 != 0) {
                    sample.reset = resetPending;
                    samples[write.startIndex1] = sample;
                    resetPending = false;
                }
                remaining = samplesPerPoint;
            }
        }
    }

    // Called by the existing UI timer. Graphs and indicators share this reader.
    template <typename Receive>
    void consume(double now, Receive&& receive) {
        int ready = fifo.getNumReady();
        if (ready == 0) {
            return;
        }
        const double step = interval.load();
        const int batch = pointsPerBlock.load();
        const bool restart = nextPointTime == 0.0 || now - nextPointTime > 0.25 || ready > 2 * batch + 4;
        if (restart) {
            const int skip = juce::jmax(0, ready - batch);
            fifo.finishedRead(skip);
            ready -= skip;
            nextPointTime = now + step;
        }
        while (ready > 0 && now >= nextPointTime) {
            Sample sample;
            {
                auto read = fifo.read(1);
                sample = samples[read.startIndex1];
            }
            receive(sample);
            --ready;
            nextPointTime += step;
        }
    }

    // A newly created editor must not replay data left by its predecessor.
    void discard() {
        fifo.finishedRead(fifo.getNumReady());
        nextPointTime = 0.0;
    }

private:
    std::array<Sample, 256> samples;
    juce::AbstractFifo fifo { static_cast<int>(samples.size()) };
    int samplesPerPoint = 400;
    int remaining = 400;
    bool resetPending = true;
    std::atomic<double> interval { 1.0 / 120.0 };
    std::atomic<int> pointsPerBlock { 1 };
    double nextPointTime = 0.0; // UI thread only.
};
