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
        actualValues = std::vector<std::atomic<double>>(parameters.size());
    }

    // Public wrapper to call protected animateValues
    void benchAnimate(double volume) { this->Effect::animateValues(volume); }
};

static double runBenchmarkOnce(int numParams, int numIterations, bool enableLfo)
{
    NullEffect effect;
    effect.initParams(numParams, enableLfo);

    // Prepare sample rate
    effect.prepareToPlay(48000.0, 512);

    // Warm up
    for (int i = 0; i < 1000; ++i) effect.benchAnimate(0.2);

    // Benchmark
    const auto start = juce::Time::getHighResolutionTicks();
    for (int it = 0; it < numIterations; ++it) {
        effect.benchAnimate(0.2);
    }
    const auto end = juce::Time::getHighResolutionTicks();
    return juce::Time::highResolutionTicksToSeconds(end - start);
}

static double runBenchmarkMixedStaticSaw(int numParams, int numIterations)
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
    effect.prepareToPlay(48000.0, 512);

    // Warm up
    for (int i = 0; i < 1000; ++i) effect.benchAnimate(0.2);

    // Benchmark
    const auto start = juce::Time::getHighResolutionTicks();
    for (int it = 0; it < numIterations; ++it) {
        effect.benchAnimate(0.2);
    }
    const auto end = juce::Time::getHighResolutionTicks();
    return juce::Time::highResolutionTicksToSeconds(end - start);
}

class AnimateValuesBenchmarkTest : public juce::UnitTest {
public:
    AnimateValuesBenchmarkTest() : juce::UnitTest("AnimateValues Benchmark") {}

    void runTest() override {
        const int numParams = 3;
        const int iterations = 192000 * 5; // 5 seconds at 192kHz

        beginTest("Static path");
        {
            const double seconds = runBenchmarkOnce(numParams, iterations, false);
            juce::Logger::outputDebugString(juce::String::formatted(
                "animateValues [Static]: params=%d, Iters=%d, total=%.3fms, per-iter=%.3fus",
                numParams, iterations, seconds * 1000.0, seconds * 1e6 / iterations));
            expectGreaterThan(seconds, 0.0, "Benchmark duration should be > 0");
        }

        beginTest("LFO Sine path");
        {
            const double seconds = runBenchmarkOnce(numParams, iterations, true);
            juce::Logger::outputDebugString(juce::String::formatted(
                "animateValues [LFO Sine]: params=%d, Iters=%d, total=%.3fms, per-iter=%.3fus",
                numParams, iterations, seconds * 1000.0, seconds * 1e6 / iterations));
            expectGreaterThan(seconds, 0.0, "Benchmark duration should be > 0");
        }

        beginTest("Mixed Static + Sawtooth LFOs");
        {
            const double seconds = runBenchmarkMixedStaticSaw(numParams, iterations);
            juce::Logger::outputDebugString(juce::String::formatted(
                "animateValues [Mixed Static+Saw]: params=%d, Iters=%d, total=%.3fms, per-iter=%.3fus",
                numParams, iterations, seconds * 1000.0, seconds * 1e6 / iterations));
            expectGreaterThan(seconds, 0.0, "Benchmark duration should be > 0");
        }
    }
};

static AnimateValuesBenchmarkTest animateValuesBenchmarkTest;
