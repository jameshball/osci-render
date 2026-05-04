#include <JuceHeader.h>

namespace
{
using Adapter = osci::IntegerRatioSampleRateAdapter;

void fillRamp (juce::AudioBuffer<float>& buffer, int64_t& cursor)
{
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto value = (float) ((double) cursor++ * 0.0001);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample (channel, sample, value);
    }
}

std::vector<int> makeVariableBlocks (int maxBlock, int count)
{
    const int pattern[] { 1, 2, 3, 5, 8, 13, 21, 32, 47, 64, 89, 127, 181, 251, 379, 512 };
    std::vector<int> blocks;
    blocks.reserve ((size_t) count);

    for (int i = 0; i < count; ++i)
        blocks.push_back (juce::jlimit (1, maxBlock, pattern[(size_t) i % std::size (pattern)]));

    return blocks;
}
} // namespace

class IntegerRatioSampleRateAdapterTest : public juce::UnitTest
{
public:
    IntegerRatioSampleRateAdapterTest() : juce::UnitTest ("Integer Ratio Sample Rate Adapter", "DSP") {}

    void runTest() override
    {
        testRatioValidation();
        testBypass();
        testOversamplingRatios();
        testUndersamplingRatios();
        testMidiMapping();
        testOversizeBlockFailsSafely();
    }

private:
    void testRatioValidation()
    {
        beginTest ("Ratio validation");

        expectEquals ((int) Adapter::getSupportedRatios().size(), 6);
        expect (Adapter::isRatioAllowed (48000.0, 0.5));
        expect (Adapter::isRatioAllowed (48000.0, 8.0));
        expect (! Adapter::isRatioAllowed (192000.0, 8.0), "Should disable ratios above 1 MHz");
        expect (! Adapter::isRatioAllowed (44100.0, 0.25), "Should disable ratios below 20 kHz");
    }

    void testBypass()
    {
        beginTest ("Bypass ratio");

        Adapter adapter;
        adapter.prepare ({ 48000.0, 1.0, 128, 2 });

        expect (! adapter.isActive());
        expectEquals (adapter.getProcessingSampleRate(), 48000.0);
        expectEquals (adapter.getMaxProcessingBlockSize(), 128);
        expectEquals (adapter.getLatencySamples(), 0);

        juce::AudioBuffer<float> buffer (2, 32);
        juce::MidiBuffer midi;
        buffer.clear();

        auto called = false;
        const auto* channel0 = buffer.getReadPointer (0);
        const auto* channel1 = buffer.getReadPointer (1);
        const auto result = adapter.process (buffer, midi, [&] (auto& inner, auto&)
        {
            called = true;
            expectEquals (inner.getNumSamples(), 32);
            expect (inner.getReadPointer (0) == channel0);
            expect (inner.getReadPointer (1) == channel1);
            inner.setSample (0, 0, 0.25f);
        });

        expect (called);
        expect (! result.active);
        expectEquals (result.underflowSamples, 0);
        expectEquals (result.overflowSamples, 0);
        expectWithinAbsoluteError (buffer.getSample (0, 0), 0.25f, 1.0e-7f);
    }

    void testOversamplingRatios()
    {
        beginTest ("2x, 4x, and 8x oversampling");

        for (const auto ratio : { 2.0, 4.0, 8.0 })
        {
            Adapter adapter;
            constexpr int maxBlock = 128;
            adapter.prepare ({ 48000.0, ratio, maxBlock, 2 });

            expect (adapter.isActive());
            expectEquals (adapter.getProcessingSampleRate(), 48000.0 * ratio);
            expectEquals (adapter.getMaxProcessingBlockSize(), maxBlock * (int) ratio);
            expect (adapter.getLatencySamples() >= 0);

            juce::AudioBuffer<float> buffer (2, maxBlock);
            juce::MidiBuffer midi;
            int64_t cursor = 0;

            for (const auto blockSize : makeVariableBlocks (maxBlock, 96))
            {
                juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), 2, blockSize);
                fillRamp (block, cursor);

                const auto result = adapter.process (block, midi, [this] (auto& inner, auto&)
                {
                    expect (inner.getNumSamples() > 0);
                    inner.clear();
                });

                expectEquals (result.underflowSamples, 0);
                expectEquals (result.overflowSamples, 0);
                expectEquals (result.internalSamplesProcessed, blockSize * (int) ratio);
            }
        }
    }

    void testUndersamplingRatios()
    {
        beginTest ("1/2x and 1/4x undersampling");

        for (const auto ratio : { 0.5, 0.25 })
        {
            Adapter adapter;
            constexpr int maxBlock = 127;
            const auto factor = (int) std::round (1.0 / ratio);
            adapter.prepare ({ 96000.0, ratio, maxBlock, 2 });

            expect (adapter.isActive());
            expectEquals (adapter.getProcessingSampleRate(), 96000.0 * ratio);
            expect (adapter.getMaxProcessingBlockSize() >= 1);
            expect (adapter.getLatencySamples() > 0);

            juce::AudioBuffer<float> buffer (2, maxBlock);
            juce::MidiBuffer midi;
            int64_t cursor = 0;

            for (const auto blockSize : makeVariableBlocks (maxBlock, 128))
            {
                juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), 2, blockSize);
                fillRamp (block, cursor);

                const auto result = adapter.process (block, midi, [this] (auto& inner, auto&)
                {
                    expect (inner.getNumSamples() > 0);
                    inner.clear();
                });

                expectEquals (result.underflowSamples, 0);
                expectEquals (result.overflowSamples, 0);
                expect (result.internalSamplesProcessed >= blockSize / factor);
            }
        }
    }

    void testMidiMapping()
    {
        beginTest ("MIDI maps once and monotonically");

        for (const auto ratio : { 0.5, 2.0 })
        {
            Adapter adapter;
            constexpr int maxBlock = 64;
            adapter.prepare ({ 48000.0, ratio, maxBlock, 2 });

            juce::AudioBuffer<float> buffer (2, maxBlock);
            juce::MidiBuffer midi;
            std::vector<int64_t> delivered;
            auto internalCursor = (int64_t) 0;

            for (int blockIndex = 0; blockIndex < 80; ++blockIndex)
            {
                const auto blockSize = makeVariableBlocks (maxBlock, 80)[(size_t) blockIndex];
                juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), 2, blockSize);
                block.clear();
                midi.clear();

                if (blockIndex % 3 == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), blockSize / 2);

                const auto before = internalCursor;
                const auto result = adapter.process (block, midi, [&] (auto& inner, auto& internalMidi)
                {
                    for (const auto metadata : internalMidi)
                        delivered.push_back (before + metadata.samplePosition);

                    inner.clear();
                });

                internalCursor += result.internalSamplesProcessed;
            }

            expect (! delivered.empty());
            for (size_t i = 1; i < delivered.size(); ++i)
                expect (delivered[i] >= delivered[i - 1]);
        }
    }

    void testOversizeBlockFailsSafely()
    {
        beginTest ("Oversize block fails safely");

        Adapter adapter;
        adapter.prepare ({ 48000.0, 2.0, 32, 2 });

        juce::AudioBuffer<float> buffer (2, 33);
        juce::MidiBuffer midi;
        buffer.setSample (0, 0, 1.0f);

        const auto result = adapter.process (buffer, midi, [] (auto&, auto&) {});
        expectEquals (result.underflowSamples, 33);
        expect (buffer.hasBeenCleared());
    }
};

static IntegerRatioSampleRateAdapterTest integerRatioSampleRateAdapterTest;
