#include <JuceHeader.h>
#include "../Source/audio/synth/ShapeVoice.h"
#include "TestCleanup.h"

namespace {
class ConstantPointSource : public PointSource {
public:
    osci::Point nextSample(LuaState&, LuaVariables&) override { return { 0.25f, 0.5f, 0.75f, 0.2f, 0.4f, 0.8f }; }
    std::vector<std::unique_ptr<osci::Shape>> nextFrame() override { return {}; }
    bool isSample() override { return true; }
    bool isActive() override { return true; }
    void disable() override {}
    void enable() override {}
};

class TestVoiceContext : public VoiceContext {
public:
    TestVoiceContext() : sound(std::make_shared<ConstantPointSource>()) {}
    ~TestVoiceContext() override { testutil::cleanupEffectParams(*frequency); }
    VoiceParameters getVoiceParameters() override {
        return { &midi, frequency.get(), &velocity
#if OSCI_PREMIUM
            , &bend, &glide, &slope, &always, &octave
#endif
        };
    }
    VoiceTelemetry& getVoiceTelemetry() override { return telemetry; }
    VoiceEffectMap cloneVoiceEffectInstances() override { return {}; }
    ShapeSound* getActiveShapeSound() const override { return const_cast<ShapeSound*>(&sound); }
    std::shared_ptr<osci::SimpleEffect> getCachedPreviewEffect() override { return {}; }
    DahdsrParams getCurrentDahdsrParams(int) const override { return {}; }
    double getVoiceSampleRate() override { return 48000; }
    double noteToFrequency(int note, int) override { return juce::MidiMessage::getMidiNoteInHertz(note); }
    int getNumPressedNotes() const override { return 1; }
    const osci::DawPosition& getVoiceTransport() const override { return transport; }
    const std::vector<std::shared_ptr<osci::Effect>>& getVoiceScriptParameters() const override { return script; }
    void processVoiceEffects(juce::AudioBuffer<float>&, juce::AudioBuffer<float>&, juce::AudioBuffer<float>&,
        juce::AudioBuffer<float>&, const VoiceEffectMap&, const std::shared_ptr<osci::SimpleEffect>&) override {}

    osci::BooleanParameter midi { "MIDI", "testMidi", 2, false, "" };
    osci::FloatParameter velocity { "Velocity", "testVelocity", 2, 0, -1, 1 };
    osci::IntParameter bend { "Bend", "testBend", 2, 2, 0, 48 };
    osci::FloatParameter glide { "Glide", "testGlide", 2, 0, 0, 16 };
    osci::FloatParameter slope { "Slope", "testSlope", 2, 0, -8, 8 };
    osci::BooleanParameter always { "Always", "testAlways", 2, false, "" };
    osci::BooleanParameter octave { "Octave", "testOctave", 2, false, "" };
    std::shared_ptr<osci::Effect> frequency = std::make_shared<osci::SimpleEffect>(
        new osci::EffectParameter("Frequency", "", "testFrequency", 2, 60, 1, 20000));
    VoiceTelemetry telemetry;
    osci::DawPosition transport;
    std::vector<std::shared_ptr<osci::Effect>> script;
    ShapeSound sound;
};

class SharedVoiceEngineTest : public juce::UnitTest {
public:
    SharedVoiceEngineTest() : juce::UnitTest("Product-independent voice engine", "VoiceManager") {}
    void runTest() override {
        beginTest("Procedural XYRGB rendering requires no Render processor or file controller");
        TestVoiceContext context;
        juce::AudioBuffer<float> input(2, 64);
        input.clear();
        ShapeVoice voice(context, input, 0);
        voice.prepareToPlay(48000, 64);
        VoiceState note;
        note.midiNote = 60;
        note.velocity = 1;
        voice.voiceActivated(note, false);
        juce::AudioBuffer<float> output(6, 64);
        output.clear();
        voice.renderNextBlock(output, 7, 25);
        const float expected[] { 0.25f, 0.5f, 0.75f, 0.2f, 0.4f, 0.8f };
        for (int channel = 0; channel < 6; ++channel) {
            expectWithinAbsoluteError(output.getSample(channel, 7), expected[channel], 0.00001f);
            expectWithinAbsoluteError(output.getSample(channel, 31), expected[channel], 0.00001f);
            expectEquals(output.getSample(channel, 6), 0.0f);
            expectEquals(output.getSample(channel, 32), 0.0f);
        }
        expect(context.telemetry.uiVoiceActive[0].load());
        beginTest("Stopping a non-MIDI voice silences it and clears telemetry");
        voice.stopNote(0, false);
        output.clear();
        voice.renderNextBlock(output, 0, 64);
        expectEquals(output.getMagnitude(0, 64), 0.0f);
        expect(!context.telemetry.uiVoiceActive[0].load());
    }
};
static SharedVoiceEngineTest sharedVoiceEngineTest;
}
