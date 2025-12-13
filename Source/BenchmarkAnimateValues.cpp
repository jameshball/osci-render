#include <JuceHeader.h>

using namespace osci;

// A minimal concrete Effect to allow calling animateValues
class NullEffect : public Effect {
public:
    const juce::String getName() const override { return "NullEffect"; }
    std::vector<EffectParameter*> initialiseParameters() const override { return {}; }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ~NullEffect() override {
        // Clean up parameters we created in initParams
        for (auto* p : parameters) delete p;
        parameters.clear();
    }

    void initParams(int numParams, bool enableLfo) {
        parameters.clear();
        // Avoid calling resize() on vector<atomic<double>> because it requires MoveInsertable.
        // Recreate the vector at the desired size instead.
        parameters.reserve(numParams);
        for (int i = 0; i < numParams; ++i) {
            auto* p = new EffectParameter("P" + juce::String(i), "bench", "p" + juce::String(i), 1, 0.5f, 0.0f, 1.0f);
            if (enableLfo) {
                p->lfo->setUnnormalisedValueNotifyingHost((int)LfoType::Sine);
                p->lfoRate->setUnnormalisedValueNotifyingHost(2.0f + 0.01f * i);
                p->lfoStartPercent->setUnnormalisedValueNotifyingHost(0.0f);
                p->lfoEndPercent->setUnnormalisedValueNotifyingHost(100.0f);
            } else {
                p->lfo->setUnnormalisedValueNotifyingHost((int)LfoType::Static);
            }
            parameters.push_back(p);
        }
        actualValues = std::vector<std::atomic<float>>(parameters.size());
    }

    // Public wrapper to call block-based animateValues
    void benchAnimate(int blockSize) { this->Effect::animateValues(blockSize, nullptr); }
};

static double runBenchmarkOnce(int numParams, int numBlocks, int blockSize, bool enableLfo)
{
    NullEffect effect;
    effect.initParams(numParams, enableLfo);

    // Prepare sample rate
    effect.prepareToPlay(48000.0, blockSize);

    // Warm up
    for (int i = 0; i < 100; ++i) effect.benchAnimate(blockSize);

    // Benchmark
    const auto start = juce::Time::getHighResolutionTicks();
    for (int it = 0; it < numBlocks; ++it) {
        effect.benchAnimate(blockSize);
    }
    const auto end = juce::Time::getHighResolutionTicks();
    return juce::Time::highResolutionTicksToSeconds(end - start);
}

static double runBenchmarkMixedStaticSaw(int numParams, int numBlocks, int blockSize)
{
    NullEffect effect;
    // Start with all static
    effect.initParams(numParams, false);

    // Configure half the parameters (even indices) as Sawtooth LFOs
    for (int i = 0; i < numParams; ++i) {
        if ((i % 2) == 0) {
            auto* p = effect.parameters[i];
            p->lfo->setUnnormalisedValueNotifyingHost((int)LfoType::Sawtooth);
            p->lfoRate->setUnnormalisedValueNotifyingHost(2.0f + 0.01f * i);
            p->lfoStartPercent->setUnnormalisedValueNotifyingHost(0.0f);
            p->lfoEndPercent->setUnnormalisedValueNotifyingHost(100.0f);
        }
    }

    // Prepare sample rate
    effect.prepareToPlay(48000.0, blockSize);

    // Warm up
    for (int i = 0; i < 100; ++i) effect.benchAnimate(blockSize);

    // Benchmark
    const auto start = juce::Time::getHighResolutionTicks();
    for (int it = 0; it < numBlocks; ++it) {
        effect.benchAnimate(blockSize);
    }
    const auto end = juce::Time::getHighResolutionTicks();
    return juce::Time::highResolutionTicksToSeconds(end - start);
}

class AnimateValuesBenchmarkTest : public juce::UnitTest {
public:
    AnimateValuesBenchmarkTest() : juce::UnitTest("AnimateValues Benchmark") {}

    void runTest() override {
        const int numParams = 3;
        const int blockSize = 512;
        const int numBlocks = (192000 * 5) / blockSize; // ~5 seconds at 192kHz

        beginTest("Static path");
        {
            const double seconds = runBenchmarkOnce(numParams, numBlocks, blockSize, false);
            const int totalSamples = numBlocks * blockSize;
            juce::Logger::outputDebugString(juce::String::formatted(
                "animateValues [Static]: params=%d, blocks=%d, blockSize=%d, total=%.3fms, per-block=%.3fus",
                numParams, numBlocks, blockSize, seconds * 1000.0, seconds * 1e6 / numBlocks));
            expectGreaterThan(seconds, 0.0, "Benchmark duration should be > 0");
        }

        beginTest("LFO Sine path");
        {
            const double seconds = runBenchmarkOnce(numParams, numBlocks, blockSize, true);
            juce::Logger::outputDebugString(juce::String::formatted(
                "animateValues [LFO Sine]: params=%d, blocks=%d, blockSize=%d, total=%.3fms, per-block=%.3fus",
                numParams, numBlocks, blockSize, seconds * 1000.0, seconds * 1e6 / numBlocks));
            expectGreaterThan(seconds, 0.0, "Benchmark duration should be > 0");
        }

        beginTest("Mixed Static + Sawtooth LFOs");
        {
            const double seconds = runBenchmarkMixedStaticSaw(numParams, numBlocks, blockSize);
            juce::Logger::outputDebugString(juce::String::formatted(
                "animateValues [Mixed Static+Saw]: params=%d, blocks=%d, blockSize=%d, total=%.3fms, per-block=%.3fus",
                numParams, numBlocks, blockSize, seconds * 1000.0, seconds * 1e6 / numBlocks));
            expectGreaterThan(seconds, 0.0, "Benchmark duration should be > 0");
        }
    }
};

static AnimateValuesBenchmarkTest animateValuesBenchmarkTest;
