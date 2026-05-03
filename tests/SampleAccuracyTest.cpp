#include <JuceHeader.h>
#include "TestCleanup.h"
#include "../Source/audio/modulation/LfoState.h"
#include "../Source/audio/modulation/ModulationEngine.h"
#include "../Source/audio/modulation/LfoParameters.h"

using namespace osci;

// ============================================================================
// Sample-Accuracy Tests — verify that modulated parameters produce distinct
// per-sample values within a block, not a single value for the whole buffer.
// ============================================================================

// Minimal Effect subclass for testing animated values
class TestEffect : public Effect {
public:
    const juce::String getName() const override { return "TestEffect"; }
    std::vector<EffectParameter*> initialiseParameters() const override { return {}; }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ~TestEffect() override {
        testutil::cleanupEffectParams(*this);
    }

    void addParam(const juce::String& name, float defaultVal, float min, float max) {
        auto* p = new EffectParameter(name, name, name, 1, defaultVal, min, max);
        p->lfo->setUnnormalisedValueNotifyingHost((int)LfoType::Static);
        parameters.push_back(p);
        actualValues = std::vector<std::atomic<float>>(parameters.size());
        for (size_t i = 0; i < parameters.size(); i++)
            actualValues[i] = parameters[i]->getValueUnnormalised();
    }

    void enableBuiltInLfo(int paramIndex, LfoType type, float rate) {
        auto* p = parameters[paramIndex];
        auto* ep = dynamic_cast<EffectParameter*>(p);
        ep->lfo->setUnnormalisedValueNotifyingHost((int)type);
        ep->lfoRate->setUnnormalisedValueNotifyingHost(rate);
        ep->lfoStartPercent->setUnnormalisedValueNotifyingHost(0.0f);
        ep->lfoEndPercent->setUnnormalisedValueNotifyingHost(100.0f);
    }
};

// ============================================================================
// Test 1: Built-in LFO produces per-sample variation
// ============================================================================
class BuiltInLfoSampleAccuracyTest : public juce::UnitTest {
public:
    BuiltInLfoSampleAccuracyTest() : juce::UnitTest("Built-in LFO Sample Accuracy", "LFO") {}

    void runTest() override {
        beginTest("Sine LFO produces distinct per-sample values");

        TestEffect effect;
        effect.addParam("testParam", 0.5f, 0.0f, 1.0f);
        effect.enableBuiltInLfo(0, LfoType::Sine, 100.0f); // 100 Hz LFO

        const int blockSize = 512;
        const double sampleRate = 48000.0;
        effect.prepareToPlay(sampleRate, blockSize);

        effect.animateValues(blockSize, nullptr);

        // Count how many sequential samples differ
        int distinctCount = 0;
        float prevVal = effect.getAnimatedValue(0, 0);
        for (int i = 1; i < blockSize; ++i) {
            float val = effect.getAnimatedValue(0, i);
            if (std::abs(val - prevVal) > 1e-7f)
                distinctCount++;
            prevVal = val;
        }

        // A 100 Hz sine at 48kHz has ~480 samples per cycle; nearly every
        // sample should differ from the previous one.
        expect(distinctCount > blockSize / 2,
               "Expected most samples to differ with 100Hz LFO; got " + juce::String(distinctCount) + " distinct transitions out of " + juce::String(blockSize - 1));

        beginTest("Sawtooth LFO ramps per-sample");

        TestEffect sawEffect;
        sawEffect.addParam("sawParam", 0.5f, 0.0f, 1.0f);
        sawEffect.enableBuiltInLfo(0, LfoType::Sawtooth, 50.0f);
        sawEffect.prepareToPlay(sampleRate, blockSize);
        sawEffect.animateValues(blockSize, nullptr);

        // Sawtooth should monotonically increase within one cycle
        // (50 Hz at 48kHz = 960 samples/cycle, so our 512-sample block is within one cycle)
        bool monotonic = true;
        for (int i = 1; i < blockSize; ++i) {
            float curr = sawEffect.getAnimatedValue(0, i);
            float prev = sawEffect.getAnimatedValue(0, i - 1);
            if (curr < prev - 1e-6f) {
                monotonic = false;
                break;
            }
        }
        expect(monotonic, "Sawtooth LFO should monotonically increase within a single cycle");
    }
};

static BuiltInLfoSampleAccuracyTest builtInLfoSampleAccuracyTest;

// ============================================================================
// Test 2: Global modulation (ModulationEngine) produces per-sample variation
// ============================================================================
class GlobalModulationSampleAccuracyTest : public juce::UnitTest {
public:
    GlobalModulationSampleAccuracyTest() : juce::UnitTest("Global Modulation Sample Accuracy", "LFO") {}

    void runTest() override {
        beginTest("LFO modulation via ModulationEngine varies per-sample");

        const int blockSize = 512;
        const double sampleRate = 48000.0;

        // Create target effect with a static parameter
        TestEffect targetEffect;
        targetEffect.addParam("target", 0.5f, 0.0f, 1.0f);
        targetEffect.prepareToPlay(sampleRate, blockSize);

        // Build param location map
        std::unordered_map<juce::String, ParamLocation> paramMap;
        paramMap["target"] = { &targetEffect, 0 };

        // Create LFO source with a fast triangle LFO
        LfoParameters lfoParams;
        lfoParams.prepareToPlay(sampleRate, blockSize);
        lfoParams.rate[0]->setUnnormalisedValueNotifyingHost(200.0f); // 200 Hz
        lfoParams.setPreset(0, LfoPreset::Triangle);
        lfoParams.setMode(0, LfoMode::Free);

        // Assign LFO 0 → target param with depth 1.0
        ModAssignment assignment;
        assignment.sourceIndex = 0;
        assignment.paramId = "target";
        assignment.depth = 1.0f;
        assignment.bipolar = false;
        lfoParams.addAssignment(assignment);

        ModulationEngine engine(paramMap);
        engine.addSource(&lfoParams);
        engine.prepareToPlay(sampleRate, blockSize);

        // Step 1: Animate base values (static in this case)
        targetEffect.animateValues(blockSize, nullptr);

        // Step 2: Fill LFO block buffers
        juce::MidiBuffer emptyMidi;
        std::atomic<bool> voiceActive[16] = {};
        lfoParams.fillBlockBuffers<16>(blockSize, sampleRate, emptyMidi, 120.0, voiceActive);

        // Step 3: Apply modulation to animated buffers
        engine.applyAllModulation(blockSize);

        // Verify per-sample variation
        int distinctCount = 0;
        float prevVal = targetEffect.getAnimatedValue(0, 0);
        for (int i = 1; i < blockSize; ++i) {
            float val = targetEffect.getAnimatedValue(0, i);
            if (std::abs(val - prevVal) > 1e-7f)
                distinctCount++;
            prevVal = val;
        }

        expect(distinctCount > blockSize / 2,
               "Expected per-sample variation from 200Hz LFO modulation; got " + juce::String(distinctCount) + " transitions out of " + juce::String(blockSize - 1));

        // Verify values are within parameter range
        for (int i = 0; i < blockSize; ++i) {
            float val = targetEffect.getAnimatedValue(0, i);
            expect(val >= 0.0f && val <= 1.0f,
                   "Modulated value at sample " + juce::String(i) + " out of range: " + juce::String(val));
        }

        testutil::cleanupLfoParams(lfoParams);
    }
};

static GlobalModulationSampleAccuracyTest globalModulationSampleAccuracyTest;

// ============================================================================
// Test 3: getAnimatedValuesReadPointer returns per-sample data
// ============================================================================
class AnimatedBufferReadPointerTest : public juce::UnitTest {
public:
    AnimatedBufferReadPointerTest() : juce::UnitTest("Animated Buffer Read Pointer", "LFO") {}

    void runTest() override {
        beginTest("getAnimatedValuesReadPointer returns valid per-sample buffer");

        TestEffect effect;
        effect.addParam("p", 0.5f, 0.0f, 1.0f);
        effect.enableBuiltInLfo(0, LfoType::Sine, 440.0f);

        const int blockSize = 256;
        effect.prepareToPlay(48000.0, blockSize);
        effect.animateValues(blockSize, nullptr);

        // Read pointer should be valid
        const float* buf = effect.getAnimatedValuesReadPointer(0, blockSize);
        expect(buf != nullptr, "Read pointer should not be null after animateValues");

        // Buffer values should match getAnimatedValue
        if (buf != nullptr) {
            for (int i = 0; i < blockSize; ++i) {
                expectEquals(buf[i], effect.getAnimatedValue(0, (size_t)i),
                             "Read pointer and getAnimatedValue should return identical values");
            }
        }

        // Invalid param index returns nullptr
        const float* nullBuf = effect.getAnimatedValuesReadPointer(999, blockSize);
        expect(nullBuf == nullptr, "Invalid param index should return nullptr");
    }
};

static AnimatedBufferReadPointerTest animatedBufferReadPointerTest;

// ============================================================================
// Test 4: Static parameter produces constant buffer (no false positives)
// ============================================================================
class StaticParameterBufferTest : public juce::UnitTest {
public:
    StaticParameterBufferTest() : juce::UnitTest("Static Parameter Buffer Consistency", "LFO") {}

    void runTest() override {
        beginTest("Static parameter converges to a constant value across samples");

        TestEffect effect;
        effect.addParam("static", 0.75f, 0.0f, 1.0f);

        const int blockSize = 512;
        effect.prepareToPlay(48000.0, blockSize);

        // Run several blocks to let smoothing settle
        for (int b = 0; b < 20; ++b)
            effect.animateValues(blockSize, nullptr);

        // After settling, all samples should be approximately the same value
        const float* buf = effect.getAnimatedValuesReadPointer(0, blockSize);
        expect(buf != nullptr);
        if (buf != nullptr) {
            float maxDiff = 0.0f;
            for (int i = 1; i < blockSize; ++i)
                maxDiff = std::max(maxDiff, std::abs(buf[i] - buf[0]));
            expect(maxDiff < 1e-4f,
                   "Static parameter should be constant after smoothing settles; max diff = " + juce::String(maxDiff));
        }
    }
};

static StaticParameterBufferTest staticParameterBufferTest;

// ============================================================================
// Test 5: Multiple blocks advance LFO phase continuously
// ============================================================================
class LfoPhaseContinuityTest : public juce::UnitTest {
public:
    LfoPhaseContinuityTest() : juce::UnitTest("LFO Phase Continuity Across Blocks", "LFO") {}

    void runTest() override {
        beginTest("LFO phase advances continuously across block boundaries");

        TestEffect effect;
        effect.addParam("continuous", 0.5f, 0.0f, 1.0f);
        effect.enableBuiltInLfo(0, LfoType::Sawtooth, 10.0f);

        const int blockSize = 256;
        effect.prepareToPlay(48000.0, blockSize);

        // Process first block
        effect.animateValues(blockSize, nullptr);
        float lastValBlock1 = effect.getAnimatedValue(0, blockSize - 1);

        // Process second block
        effect.animateValues(blockSize, nullptr);
        float firstValBlock2 = effect.getAnimatedValue(0, 0);

        // The first sample of block 2 should be close to (and just past) the last
        // sample of block 1, not reset to the beginning.
        float gap = std::abs(firstValBlock2 - lastValBlock1);
        // Allow for small phase increment and potential wrap-around at cycle boundary
        bool continuous = gap < 0.05f || gap > 0.95f; // wrap-around would show ~1.0 gap
        expect(continuous,
               "LFO should be continuous across blocks; gap = " + juce::String(gap));
    }
};

static LfoPhaseContinuityTest lfoPhaseContinuityTest;

// ============================================================================
// Test 6: Synced global LFO derives phase from host timeline seconds
// ============================================================================
class LfoSyncTimelineAnchorTest : public juce::UnitTest {
public:
    LfoSyncTimelineAnchorTest() : juce::UnitTest("LFO Sync Timeline Anchor", "LFO") {}

    void runTest() override {
        beginTest("Sync mode uses host timeline seconds instead of stored phase");

        const int blockSize = 4;
        const double sampleRate = 100.0;
        juce::MidiBuffer emptyMidi;
        std::atomic<bool> voiceActive[1] = {};
        voiceActive[0].store(true, std::memory_order_relaxed);

        LfoParameters lfoParams;
        lfoParams.prepareToPlay(sampleRate, blockSize);
        lfoParams.setMode(0, LfoMode::Sync);
        lfoParams.setRateMode(0, LfoRateMode::Tempo);
        lfoParams.setTempoDivision(0, 8); // 1/4 at 120 BPM = 2 Hz
        lfoParams.setPhaseOffset(0, 0.0f);
        lfoParams.setSmoothAmount(0, 0.0f);
        lfoParams.setDelayAmount(0, 0.0f);

        LfoWaveform ramp;
        ramp.smooth = false;
        ramp.nodes = { { 0.0, 0.0, 0.0f }, { 1.0, 1.0, 0.0f } };
        lfoParams.waveformChanged(0, ramp);

        auto renderFirstSampleAt = [&](double syncStartSeconds) {
            lfoParams.audioStates[0].phase = 0.73f;
            lfoParams.fillBlockBuffers<1>(blockSize, sampleRate, emptyMidi, 120.0,
                                          voiceActive, syncStartSeconds, true);
            return lfoParams.blockBuffer[0][0];
        };

        expectWithinAbsoluteError(renderFirstSampleAt(0.25), 0.5f, 0.01f);
        expectWithinAbsoluteError(renderFirstSampleAt(1.25), 0.5f, 0.01f);
        expectWithinAbsoluteError(renderFirstSampleAt(0.125), 0.25f, 0.01f);

        testutil::cleanupLfoParams(lfoParams);
    }
};

static LfoSyncTimelineAnchorTest lfoSyncTimelineAnchorTest;
