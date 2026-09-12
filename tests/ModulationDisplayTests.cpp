#include <JuceHeader.h>
#include "TestCleanup.h"
#include "../Source/audio/modulation/ModulationDisplayBuffer.h"
#include "../Source/audio/modulation/RandomState.h"
#include "../Source/audio/modulation/SidechainState.h"
#include <thread>

class ModulationDisplayTests : public juce::UnitTest {
public:
    ModulationDisplayTests() : juce::UnitTest("Modulation display buffering", "ModulationDisplay") {}

    void runTest() override {
        beginTest("Fixed sample cadence and paced delivery for small and large audio blocks");
        for (double sampleRate : { 44100.0, 48000.0, 96000.0 }) {
            for (int blockSize : { 32, 64, 511, 4096, 8192 }) {
                ModulationDisplayBuffer buffer;
                buffer.prepare(sampleRate, blockSize);
                const int stride = juce::roundToInt(sampleRate / 120.0);
                int processed = 0, received = 0, emptyTicks = 0, largestBurst = 0;
                float last = 0.0f;
                double nextBlock = 0.0;
                bool samplesCorrect = true;
                for (int tick = 0; tick < 600; ++tick) {
                    const double now = tick / 60.0;
                    while (nextBlock <= now) {
                        buffer.process(blockSize, [&](int, int count) {
                            processed += count;
                            return ModulationDisplayBuffer::Sample { (float)processed, (float)-processed, true };
                        });
                        nextBlock += blockSize / sampleRate;
                    }
                    int burst = 0;
                    buffer.consume(now, [&](auto sample) {
                        samplesCorrect &= sample.value > last && (int)sample.value % stride == 0;
                        samplesCorrect &= sample.position == -sample.value && sample.active;
                        last = sample.value;
                        ++burst;
                        ++received;
                    });
                    largestBurst = juce::jmax(largestBurst, burst);
                    if (tick > 60 && burst == 0) {
                        ++emptyTicks;
                    }
                }
                const auto label = juce::String(sampleRate) + " Hz / " + juce::String(blockSize);
                expect(samplesCorrect, label);
                expect(largestBurst <= 3, "No block-sized display bursts: " + label);
                expect(emptyTicks <= 2, "No periodic freezes: " + label + ": " + juce::String(emptyTicks));
                expect(received > 1170 && received < 1210, "Approximately 120 snapshots per second: " + label);
            }
        }

        beginTest("A full display buffer never prevents audio processing; reopening discards stale data");
        ModulationDisplayBuffer buffer;
        buffer.prepare(48000.0, 8192);
        int processed = 0;
        auto generate = [&](int, int count) {
            processed += count;
            return ModulationDisplayBuffer::Sample { (float)processed };
        };
        for (int i = 0; i < 1000; ++i) {
            buffer.process(8192, generate);
        }
        expectEquals(processed, 8192000);
        buffer.discard();
        const int beforeReopen = processed;
        buffer.process(8192, generate);
        buffer.consume(100.0, [](auto) {});
        int received = 0;
        buffer.consume(100.18, [&](auto sample) {
            expect(sample.value > beforeReopen);
            ++received;
        });
        expect(received >= 19 && received <= 21);
        buffer.consume(101.0, [&](auto) { expect(false, "Empty streams must not invent samples"); });

        beginTest("Retrigger survives partial audio blocks and is emitted only once");
        buffer.discard();
        buffer.prepare(48000.0, 800);
        buffer.process(400, generate);
        buffer.consume(0.0, [](auto) {});
        buffer.consume(0.01, [&](auto sample) { expect(sample.reset); });
        buffer.process(100, generate);
        buffer.resetMarker();
        buffer.process(700, generate);
        std::vector<bool> resets;
        buffer.consume(0.03, [&](auto sample) { resets.push_back(sample.reset); });
        expect(resets == std::vector<bool> { true, false });

        beginTest("Concurrent publication, overflow and UI reopen preserve snapshot integrity");
        buffer.discard();
        buffer.prepare(48000.0, 8192);
        std::atomic<bool> done { false };
        std::thread producer([&] {
            int position = 0;
            for (int i = 0; i < 10000; ++i) {
                buffer.process(8192, [&](int, int count) {
                    position += count;
                    return ModulationDisplayBuffer::Sample { (float)position, (float)-position, true };
                });
            }
            done.store(true);
        });
        bool intact = true;
        int ticks = 0;
        while (!done.load()) {
            buffer.consume(++ticks / 60.0, [&](auto sample) {
                intact &= sample.value == -sample.position && sample.active;
            });
            if (ticks % 37 == 0) {
                buffer.discard();
            }
        }
        producer.join();
        expect(intact);

        beginTest("Display chunking leaves every LFO mode's audio samples unchanged");
        const auto waveform = createLfoPreset(LfoPreset::Triangle);
        std::vector<float> expected(8192), actual(8192);
        for (const auto& mode : getAllLfoModePairs()) {
            const auto lfoMode = static_cast<LfoMode>(mode.first);
            LfoAudioState whole, chunked;
            whole.noteOn(lfoMode, 0.35f);
            chunked = whole;
            buffer.prepare(48000.0, 8192);
            for (int block = 0; block < 8; ++block) {
                if (block == 3) {
                    whole.noteOff(lfoMode);
                    chunked.noteOff(lfoMode);
                }
                whole.advanceBlock(expected.data(), 8192, 17.0f, 48000.0f, waveform, lfoMode, 0.35f);
                buffer.process(8192, [&](int offset, int count) {
                    chunked.advanceBlock(actual.data() + offset, count, 17.0f, 48000.0f, waveform, lfoMode, 0.35f);
                    return ModulationDisplayBuffer::Sample {};
                });
                expect(actual == expected, mode.second);
                expectEquals(chunked.phase, whole.phase);
            }
        }

        beginTest("Random and sidechain audio samples are identical with display chunking");
        for (auto style : { RandomStyle::Perlin, RandomStyle::SampleAndHold, RandomStyle::SineInterpolate }) {
            RandomAudioState whole, chunked;
            whole.style = style;
            whole.noteOn();
            chunked = whole;
            for (int block = 0; block < 8; ++block) {
                whole.advanceBlock(expected.data(), 8192, 17.0f, 48000.0f);
                buffer.process(8192, [&](int offset, int count) {
                    chunked.advanceBlock(actual.data() + offset, count, 17.0f, 48000.0f);
                    return ModulationDisplayBuffer::Sample {};
                });
                expect(actual == expected, randomStyleToString(style));
            }
        }
        SidechainAudioState whole, chunked;
        auto curve = sidechain::defaultTransferCurve();
        std::vector<float> input(8192);
        for (int i = 0; i < 8192; ++i) {
            input[i] = std::abs(std::sin(i * 0.002f));
        }
        whole.advanceBlock(expected.data(), input.data(), 8192, 0.03f, 0.2f, 48000.0f, curve);
        buffer.process(8192, [&](int offset, int count) {
            chunked.advanceBlock(actual.data() + offset, input.data() + offset, count, 0.03f, 0.2f, 48000.0f, curve);
            return ModulationDisplayBuffer::Sample {};
        });
        expect(actual == expected);

        beginTest("LFO parameter processing preserves smoothing, delay and host-sync output");
        LfoParameters lfo;
        lfo.prepareToPlay(48000.0, 8192);
        std::atomic<bool> voices[1] { true };
        osci::DawPosition daw;
        juce::MidiBuffer midi;
        for (auto mode : { LfoMode::Free, LfoMode::Sync }) {
            for (float smooth : { 0.0f, 0.03f }) {
                for (float delay : { 0.0f, 0.007f, 1.0f }) {
                    for (bool playing : { false, true }) {
                        lfo.setMode(0, mode);
                        lfo.setSmoothAmount(0, smooth);
                        lfo.setDelayAmount(0, delay);
                        lfo.audioStates[0].reset();
                        lfo.delayElapsed[0] = 0.0f;
                        lfo.smoothedOutput[0] = 0.0f;
                        daw.hasSyncPosition.store(mode == LfoMode::Sync);
                        daw.isPlaying.store(playing);
                        daw.syncSeconds.store(1.25);
                        daw.syncSecondsPerSample.store(playing ? 1.0 / 48000.0 : 0.0);
                        const int skip = juce::jmin(8192, (int)std::ceil(lfo.getDelayAmount(0) * 48000.0f));
                        std::fill(expected.begin(), expected.begin() + skip, waveform.evaluate(0.0f));
                        LfoAudioState reference;
                        if (mode == LfoMode::Sync) {
                            for (int i = skip; i < 8192; ++i) {
                                double phase = 1.25 + i * daw.syncSecondsPerSample.load();
                                expected[i] = waveform.evaluate((float)(phase - std::floor(phase)));
                            }
                        } else {
                            reference.advanceBlock(expected.data() + skip, 8192 - skip, 1.0f, 48000.0f, waveform, mode, 0.0f);
                        }
                        if (smooth > 0.0f && !(mode == LfoMode::Sync && !playing)) {
                            const float alpha = 1.0f - std::exp(-1.0f / (lfo.getSmoothAmount(0) * 48000.0f));
                            float previous = 0.0f;
                            for (auto& value : expected) {
                                previous += alpha * (value - previous);
                                value = previous;
                            }
                        }
                        lfo.fillBlockBuffers(8192, 48000.0, midi, daw, voices);
                        expect(lfo.blockBuffer[0] == expected, lfoModeToString(mode) + ": " + juce::String(smooth) + "/" + juce::String(delay));
                    }
                }
            }
        }
        testutil::cleanupLfoParams(lfo);
    }
};

static ModulationDisplayTests modulationDisplayTests;
