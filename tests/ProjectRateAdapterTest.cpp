#include <JuceHeader.h>
#include "../Source/audio/platform/ProjectRateAdapter.h"
#include "../Source/motion/BeamOutput.h"

// A project drawn at its own rate reaches the device as the same signal: a
// tone made at the project's rate is the same tone at the device's.
class ProjectRateAdapterTest : public juce::UnitTest {
public:
    ProjectRateAdapterTest() : juce::UnitTest("Project rate adapter", "Motion") {}

    void runTest() override {
        for (const auto [device, rate] : {std::pair {44100.0, 192000.0}, std::pair {48000.0, 192000.0}, std::pair {48000.0, 44100.0}, std::pair {44100.0, 768000.0}}) {
            beginTest("A tone made at " + juce::String(rate) + " Hz plays as the same tone at " + juce::String(device) + " Hz");
            ProjectRateAdapter adapter;
            adapter.prepare(device, rate, 512, 2);
            expectEquals(adapter.getProcessingSampleRate(), rate);
            std::int64_t made = 0;
            std::vector<float> heard;
            juce::AudioBuffer<float> block(2, 512);
            juce::MidiBuffer midi;
            const int sizes[] {1, 64, 512, 37, 256, 480, 13, 512};
            for (int round = 0; heard.size() < static_cast<std::size_t>(device); ++round) {
                const auto size = sizes[round % 8];
                juce::AudioBuffer<float> view(block.getArrayOfWritePointers(), 2, size);
                adapter.process(view, midi, [&](juce::AudioBuffer<float>& internal, juce::MidiBuffer&) {
                    for (int sample = 0; sample < internal.getNumSamples(); ++sample) {
                        const auto value = static_cast<float>(.5 * std::sin(2 * juce::MathConstants<double>::pi * 1000.0 * static_cast<double>(made++) / rate));
                        internal.setSample(0, sample, value);
                        internal.setSample(1, sample, -value);
                    }
                });
                for (int sample = 0; sample < size; ++sample) { heard.push_back(view.getSample(0, sample)); }
            }
            // Past the filters' start: its frequency by its crossings, and its size.
            const std::size_t from = 4000;
            int crossings = 0;
            double peak = 0;
            for (std::size_t index = from + 1; index < heard.size(); ++index) {
                if ((heard[index - 1] < 0) != (heard[index] < 0)) { ++crossings; }
                peak = std::max(peak, static_cast<double>(std::abs(heard[index])));
            }
            const auto seconds = static_cast<double>(heard.size() - from - 1) / device;
            expectWithinAbsoluteError(crossings / 2.0 / seconds, 1000.0, 10.0);
            expectWithinAbsoluteError(peak, .5, .03);
            // It makes the project's rate's worth of samples for the time played.
            expectWithinAbsoluteError(static_cast<double>(made) / static_cast<double>(heard.size()), rate / device, rate / device * .01);
        }

        beginTest("Brightness brought down to the device stays from dark to full");
        {
            // A beam blanked and lit in turn, as it is between strokes.
            ProjectRateAdapter adapter;
            adapter.prepare(48000.0, 192000.0, 512, 5);
            std::int64_t made = 0;
            juce::AudioBuffer<float> block(5, 512);
            juce::MidiBuffer midi;
            float lowest = 0, highest = 1, clampedLowest = 0, clampedHighest = 1, xLowest = 0;
            for (int round = 0; round < 40; ++round) {
                adapter.process(block, midi, [&](juce::AudioBuffer<float>& internal, juce::MidiBuffer&) {
                    for (int sample = 0; sample < internal.getNumSamples(); ++sample) {
                        const auto lit = (made++ / 37) % 2 == 0 ? 1.0f : 0.0f;
                        for (int channel = 0; channel < 5; ++channel) { internal.setSample(channel, sample, lit); }
                    }
                });
                lowest = std::min(lowest, block.findMinMax(2, 0, 512).getStart());
                highest = std::max(highest, block.findMinMax(2, 0, 512).getEnd());
                motion::clampBrightness(motion::OutputMode::xyrgb, block, 512);
                for (int channel = 2; channel < 5; ++channel) {
                    clampedLowest = std::min(clampedLowest, block.findMinMax(channel, 0, 512).getStart());
                    clampedHighest = std::max(clampedHighest, block.findMinMax(channel, 0, 512).getEnd());
                }
                xLowest = std::min(xLowest, block.findMinMax(0, 0, 512).getStart());
            }
            expect(lowest < -.01f && highest > 1.01f, "the filter rings past dark and full");
            expectEquals(clampedLowest, 0.0f);
            expectEquals(clampedHighest, 1.0f);
            expect(xLowest < -.01f, "X and Y, positions, are left as they are");
        }
    }
};

static ProjectRateAdapterTest projectRateAdapterTest;
