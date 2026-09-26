#include <JuceHeader.h>
#include "../Source/audio/synth/PreparedNoteVoice.h"

class PreparedNoteVoiceTest : public juce::UnitTest {
public:
    PreparedNoteVoiceTest() : juce::UnitTest("Shared prepared note voice", "VoiceKernel") {}
    void runTest() override {
        beginTest("Live envelope keeps its established sample sequence");
        DahdsrParams reference;
        reference.delaySeconds = .02; reference.attackSeconds = .03;
        reference.holdSeconds = .02; reference.decaySeconds = .02;
        reference.sustainLevel = .5; reference.releaseSeconds = .03;
        DahdsrState liveReference; liveReference.reset(reference);
        const float heldValues[] {0, 0, 0, 1.0f / 3, 2.0f / 3, 1, 1, 1, .75f, .5f};
        for (const auto expected : heldValues) { expectWithinAbsoluteError(liveReference.advance(.01), expected, 1e-6f); }
        liveReference.beginRelease();
        for (const auto expected : {.5f, 1.0f / 3, 0.0f, 0.0f}) { expectWithinAbsoluteError(liveReference.advance(.01), expected, 1e-6f); }

        beginTest("Prepared envelopes match live stage boundaries and segment values");
        for (const auto rate : {100.0, 44100.0, 48000.0, 96000.0}) {
            for (const auto shape : {0, 1, 2}) {
                DahdsrParams params;
                params.attackLevel = .85; params.sustainLevel = .4;
                if (shape != 0) {
                    params.delaySeconds = .0017; params.attackSeconds = .0093;
                    params.holdSeconds = .0021; params.decaySeconds = .0137; params.releaseSeconds = .0079;
                    params.attackCurve = shape == 1 ? -7 : 12;
                    params.decayCurve = shape == 1 ? 5 : -9;
                    params.releaseCurve = shape == 1 ? -4 : 14;
                }
                const auto prepared = osci_audio::PreparedVoiceEnvelope::prepare(params, rate);
                expect(prepared.has_value());
                if (!prepared) { continue; }
                for (const auto holdSeconds : {0.0, .0005, .003, .012, .02, .04}) {
                    const auto held = static_cast<std::uint64_t>(holdSeconds * rate);
                    const auto count = held + prepared->releaseSamples() + 3;
                    DahdsrState live; live.reset(params);
                    std::vector<float> expected;
                    expected.reserve(static_cast<std::size_t>(count));
                    bool stagesMatch = true;
                    float largestError = 0;
                    for (std::uint64_t sample = 0; sample < count; ++sample) {
                        if (sample == held) { live.beginRelease(); }
                        const auto value = live.advance(1 / rate);
                        expected.push_back(value);
                        const auto actual = prepared->at(sample, held);
                        largestError = std::max(largestError, std::abs(value - actual.gain));
                        stagesMatch = stagesMatch && actual.stage == live.getStage();
                    }
                    expect(stagesMatch, "Stage boundaries differ at " + juce::String(rate) + " Hz.");
                    expect(largestError < 0.000002f, "Prepared/live maximum gain error: " + juce::String(largestError, 9));
                    for (auto sample = count; sample-- > 0;) {
                        expectWithinAbsoluteError(prepared->at(sample, held).gain, expected[static_cast<std::size_t>(sample)], .000002f);
                    }
                }
            }
        }

        beginTest("Pitch phase and velocity are independent of query order and block boundaries");
        DahdsrParams sustained; sustained.sustainLevel = 1; sustained.releaseSeconds = .005;
        const auto envelope = osci_audio::PreparedVoiceEnvelope::prepare(sustained, 48000);
        expect(envelope.has_value());
        if (!envelope) { return; }
        const auto voice = osci_audio::PreparedNoteVoice::prepare(*envelope, 440, .25f, 1);
        expect(voice.has_value());
        if (!voice) { return; }
        for (const auto blockSize : {1, 17, 64, 511, 1024}) {
            for (int start = 0; start < 5000; start += blockSize) {
                for (int i = start; i < std::min(5000, start + blockSize); ++i) {
                    const auto value = voice->at(static_cast<std::uint64_t>(i), 4800);
                    const auto cycles = i * (440.0 / 48000);
                    expectWithinAbsoluteError(value.phase, cycles - std::floor(cycles), 1e-12);
                    expectEquals(value.velocityGain, .25f);
                }
            }
        }
        expect(!voice->at(4800 + envelope->releaseSamples(), 4800).active());
        const auto retrigger = voice->at(0, 4800);
        expectEquals(retrigger.phase, 0.0);
        const auto inverted = osci_audio::PreparedNoteVoice::prepare(*envelope, 440, .25f, -1);
        expect(inverted.has_value());
        if (inverted) { expectEquals(inverted->at(10, 4800).velocityGain, 1.75f); }
        const auto exactPhase = osci_audio::PreparedNoteVoice::prepare(*envelope, 6000, 1, 1);
        expect(exactPhase.has_value());
        if (exactPhase) {
            const auto late = (std::uint64_t(1) << 63) + 3;
            expectEquals(exactPhase->at(late, std::numeric_limits<std::uint64_t>::max()).phase, .375);
            expectEquals(exactPhase->at(late + 1, std::numeric_limits<std::uint64_t>::max()).phase, .5);
            expectEquals(exactPhase->at(std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max()).phase, .875);
        }

        beginTest("Maximum-rate long attack and release retain live envelope parity");
        DahdsrParams longParams;
        longParams.attackSeconds = 30; longParams.releaseSeconds = 30;
        longParams.sustainLevel = .4; longParams.attackCurve = 20; longParams.releaseCurve = -20;
        constexpr double highRate = 768000;
        const auto longEnvelope = osci_audio::PreparedVoiceEnvelope::prepare(longParams, highRate);
        expect(longEnvelope.has_value());
        if (longEnvelope) {
            DahdsrState live; live.reset(longParams);
            const auto held = static_cast<std::uint64_t>(29.999 * highRate);
            const auto end = held + longEnvelope->releaseSamples() + 2;
            float error = 0;
            bool stagesMatch = true;
            for (std::uint64_t sample = 0; sample < end; ++sample) {
                if (sample == held) { live.beginRelease(); }
                const auto value = live.advance(1 / highRate);
                if (sample % 100000 == 0 || (sample + 4 >= held && sample <= held + 4) || sample + 4 >= end) {
                    const auto prepared = longEnvelope->at(sample, held);
                    error = std::max(error, std::abs(value - prepared.gain));
                    stagesMatch = stagesMatch && live.getStage() == prepared.stage;
                }
            }
            expect(stagesMatch);
            expect(error < .000002f, "Long envelope maximum gain error: " + juce::String(error, 9));
        }

        beginTest("Preparation rejects invalid and cancelled settings");
        expect(!osci_audio::PreparedVoiceEnvelope::prepare(sustained, 0));
        expect(!osci_audio::PreparedVoiceEnvelope::prepare(sustained, 1000000));
        std::atomic<bool> cancelled {true};
        expect(!osci_audio::PreparedVoiceEnvelope::prepare(sustained, 48000, &cancelled));
        auto bad = sustained; bad.attackSeconds = 31;
        expect(!osci_audio::PreparedVoiceEnvelope::prepare(bad, 48000));
        bad = sustained; bad.attackCurve = std::numeric_limits<float>::infinity();
        expect(!osci_audio::PreparedVoiceEnvelope::prepare(bad, 48000));
        expect(!osci_audio::PreparedNoteVoice::prepare(*envelope, -1, 1, 1));
        expect(!osci_audio::PreparedNoteVoice::prepare(*envelope, 30000, 1, 1));
        expect(!osci_audio::PreparedNoteVoice::prepare(*envelope, 440, 2, 1));
    }
};
static PreparedNoteVoiceTest preparedNoteVoiceTest;
