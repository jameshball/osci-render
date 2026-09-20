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
        testMidiMapping();
        testZeroSampleBlocks();
        testOversizeBlockFailsSafely();
    }

private:
    void testRatioValidation()
    {
        beginTest ("Ratio validation");

        expectEquals ((int) Adapter::getSupportedRatios().size(), 4);
        expect (! Adapter::isRatioAllowed (48000.0, 0.5));
        expect (Adapter::isRatioAllowed (48000.0, 8.0));
        expect (! Adapter::isRatioAllowed (192000.0, 8.0), "Should disable ratios above 1 MHz");
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
        expectEquals (result.internalSamplesProcessed, 32);
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

                expect (result.active);
                expectEquals (result.internalSamplesProcessed, blockSize * (int) ratio);
            }
        }
    }

    void testMidiMapping() {
        beginTest("MIDI preserves exact positions, ordering and SysEx across variable blocks");

        for (const auto ratio : { 2.0, 4.0, 8.0 }) {
            Adapter adapter;
            constexpr int maxBlock = 64;
            adapter.prepare({ 48000.0, ratio, maxBlock, 2 });
            juce::AudioBuffer<float> buffer(2, maxBlock);
            juce::MidiBuffer midi;
            const std::array<juce::uint8, 5> payload { 0x7d, 1, 2, 3, 4 };
            const auto sysEx = juce::MidiMessage::createSysExMessage(payload.data(), (int)payload.size());
            const auto blocks = makeVariableBlocks(maxBlock, 80);

            for (int blockIndex = 0; blockIndex < (int)blocks.size(); ++blockIndex) {
                const int blockSize = blocks[(size_t)blockIndex];
                juce::AudioBuffer<float> block(buffer.getArrayOfWritePointers(), 2, blockSize);
                block.clear();
                midi.clear();
                if (blockIndex % 3 == 0) {
                    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), -1);
                    midi.addEvent(sysEx, blockSize / 2);
                    midi.addEvent(juce::MidiMessage::noteOff(1, 60), blockSize / 2);
                    midi.addEvent(juce::MidiMessage::controllerEvent(2, 1, 127), blockSize + 5);
                }
                const juce::MidiBuffer expected(midi);
                adapter.process(block, midi, [&](auto& inner, auto& internalMidi) {
                    expectEquals(internalMidi.getNumEvents(), expected.getNumEvents());
                    auto original = expected.cbegin();
                    for (const auto event : internalMidi) {
                        if (original == expected.cend()) {
                            break;
                        }
                        const auto source = *original++;
                        expectEquals(event.samplePosition, juce::jlimit(0, blockSize - 1, source.samplePosition) * (int)ratio);
                        expectEquals(event.numBytes, source.numBytes);
                        expect(std::equal(event.data, event.data + event.numBytes, source.data, source.data + source.numBytes));
                    }
                    inner.clear();
                });
                expect(midi.isEmpty());
            }
        }
    }

    void testZeroSampleBlocks() {
        beginTest("Zero-sample blocks defer MIDI and processing until audio resumes");
        const std::array<juce::uint8, 4> payload { 0x7d, 1, 2, 3 };
        const std::array<juce::MidiMessage, 5> messages {
            juce::MidiMessage::noteOn(2, 64, (juce::uint8)100),
            juce::MidiMessage::createSysExMessage(payload.data(), (int)payload.size()),
            juce::MidiMessage::noteOff(2, 64),
            juce::MidiMessage::controllerEvent(3, 1, 127),
            juce::MidiMessage::pitchWheel(3, 9000)
        };
        for (const auto ratio : { 1.0, 2.0, 4.0, 8.0 }) {
            Adapter adapter;
            adapter.prepare({ 48000.0, ratio, 8, 2 });
            juce::AudioBuffer<float> empty(2, 0);
            juce::AudioBuffer<float> audio(2, 8);
            audio.clear();
            juce::MidiBuffer midi;
            midi.addEvent(messages[0], 17);
            midi.addEvent(messages[1], 17);
            const auto result = adapter.process(empty, midi, [this](auto&, auto&) {
                expect(false, "An empty block must not advance processor state");
            });
            expectEquals(result.internalSamplesProcessed, 0);
            expect(midi.isEmpty());
            midi.addEvent(messages[2], 0);
            adapter.process(empty, midi, [this](auto&, auto&) {
                expect(false, "Successive empty blocks must remain deferred");
            });
            midi.addEvent(messages[3], 0);
            midi.addEvent(messages[4], 2);
            adapter.process(audio, midi, [&](auto& block, auto& events) {
                expectEquals(block.getNumSamples(), 8 * (int)ratio);
                expectEquals(events.getNumEvents(), (int)messages.size());
                size_t index = 0;
                for (const auto event : events) {
                    if (index >= messages.size()) {
                        break;
                    }
                    const auto& expected = messages[index];
                    expectEquals(event.samplePosition, index == 4 ? 2 * (int)ratio : 0);
                    expectEquals(event.numBytes, expected.getRawDataSize());
                    expect(std::equal(event.data, event.data + event.numBytes,
                                      expected.getRawData(), expected.getRawData() + expected.getRawDataSize()));
                    ++index;
                }
                block.clear();
            });
            midi.clear();
            adapter.process(audio, midi, [this](auto& block, auto& events) {
                expect(events.isEmpty(), "Deferred MIDI must be delivered only once");
                block.clear();
            });
            midi.addEvent(messages[0], 0);
            adapter.process(empty, midi, [this](auto&, auto&) { expect(false); });
            adapter.reset();
            adapter.process(audio, midi, [this](auto& block, auto& events) {
                expect(events.isEmpty(), "Reset must discard deferred MIDI");
                block.clear();
            });
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
        expect (result.active);
        expectEquals (result.internalSamplesProcessed, 0);
        expect (buffer.hasBeenCleared());
    }
};

static IntegerRatioSampleRateAdapterTest integerRatioSampleRateAdapterTest;
