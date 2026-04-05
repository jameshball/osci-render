#include <JuceHeader.h>
#include "TestCleanup.h"

using namespace osci;

// ---------------------------------------------------------------------------
// Synthetic EffectApplications that mirror the real patterns
// ---------------------------------------------------------------------------

// Stateless effect (like BitCrush, Bulge, Rotate, Translate, ...)
class BenchStatelessEffect : public EffectApplication {
public:
    Point apply(int index, Point input, Point externalInput,
                const std::vector<std::atomic<float>>& values,
                float sampleRate, float frequency) override {
        return input;
    }
    std::shared_ptr<Effect> build() const override {
        auto eff = std::make_shared<SimpleEffect>(
            std::make_shared<BenchStatelessEffect>(),
            new EffectParameter("Stateless", "bench", "stateless", 1, 0.5f, 0.0f, 1.0f));
        return eff;
    }
    std::shared_ptr<EffectApplication> clone() const override {
        return std::make_shared<BenchStatelessEffect>();
    }
};

// Small-state effect (like AutoGainControl, KaleidoscopeEffect, etc.)
class BenchSmallStateEffect : public EffectApplication {
public:
    Point apply(int index, Point input, Point externalInput,
                const std::vector<std::atomic<float>>& values,
                float sampleRate, float frequency) override {
        smoothedLevel += (input.x - smoothedLevel) * 0.01;
        return input;
    }
    std::shared_ptr<Effect> build() const override {
        auto eff = std::make_shared<SimpleEffect>(
            std::make_shared<BenchSmallStateEffect>(),
            new EffectParameter("SmallState", "bench", "smallstate", 1, 0.5f, 0.0f, 1.0f));
        return eff;
    }
    std::shared_ptr<EffectApplication> clone() const override {
        return std::make_shared<BenchSmallStateEffect>();
    }
private:
    double smoothedLevel = 0.01;
};

// Large-buffer effect (mirrors DelayEffect: 1,920,000 Points = ~46 MB)
class BenchDelayLikeEffect : public EffectApplication {
public:
    Point apply(int index, Point input, Point externalInput,
                const std::vector<std::atomic<float>>& values,
                float sampleRate, float frequency) override {
        delayBuffer[head] = input;
        head = (head + 1) % MAX_DELAY;
        return input;
    }
    std::shared_ptr<Effect> build() const override {
        auto eff = std::make_shared<SimpleEffect>(
            std::make_shared<BenchDelayLikeEffect>(),
            new EffectParameter("DelayLike", "bench", "delaylike", 1, 0.5f, 0.0f, 1.0f));
        return eff;
    }
    std::shared_ptr<EffectApplication> clone() const override {
        return std::make_shared<BenchDelayLikeEffect>();
    }
private:
    static const int MAX_DELAY = 192000 * 10; // 1,920,000 Points
    std::vector<Point> delayBuffer = std::vector<Point>(MAX_DELAY);
    int head = 0;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a realistic set of ~23 global effects matching the real plugin layout
static std::vector<std::shared_ptr<Effect>> buildGlobalEffects() {
    std::vector<std::shared_ptr<Effect>> effects;

    // 12 stateless-ish effects (BitCrush, Bulge, VectorCancelling, Ripple, Rotate,
    // Translate, Swirl, Smooth, DashedLine, Trace, Wobble, Duplicator)
    for (int i = 0; i < 12; ++i) {
        auto e = BenchStatelessEffect().build();
        e->setPrecedence(static_cast<int>(effects.size()));
        effects.push_back(e);
    }

    // 1 delay effect (large buffer)
    {
        auto e = BenchDelayLikeEffect().build();
        e->setPrecedence(static_cast<int>(effects.size()));
        effects.push_back(e);
    }

    // 10 premium small-state effects (Multiplex, Unfold, Bounce, Twist, Skew,
    // Polygonizer, Kaleidoscope, Vortex, GodRay, SpiralBitCrush)
    for (int i = 0; i < 10; ++i) {
        auto e = BenchSmallStateEffect().build();
        e->setPrecedence(static_cast<int>(effects.size()));
        effects.push_back(e);
    }

    return effects;
}

// Simulate initializeEffectsFromGlobal(): clone all effects into a map
static void cloneAllEffects(
    const std::vector<std::shared_ptr<Effect>>& globalEffects,
    std::unordered_map<juce::String, std::shared_ptr<SimpleEffect>>& voiceMap,
    double sampleRate)
{
    voiceMap.clear();
    for (auto& globalEffect : globalEffects) {
        auto simpleEffect = std::dynamic_pointer_cast<SimpleEffect>(globalEffect);
        if (simpleEffect) {
            auto cloned = simpleEffect->cloneWithSharedParameters();
            if (sampleRate > 0) {
                cloned->prepareToPlay(sampleRate, 512);
            }
            voiceMap[globalEffect->getId()] = cloned;
        }
    }
}

// Vector-based alternative to unordered_map (same indices as global list)
static void cloneAllEffectsVector(
    const std::vector<std::shared_ptr<Effect>>& globalEffects,
    std::vector<std::shared_ptr<SimpleEffect>>& voiceVec,
    double sampleRate)
{
    voiceVec.clear();
    voiceVec.reserve(globalEffects.size());
    for (auto& globalEffect : globalEffects) {
        auto simpleEffect = std::dynamic_pointer_cast<SimpleEffect>(globalEffect);
        if (simpleEffect) {
            auto cloned = simpleEffect->cloneWithSharedParameters();
            if (sampleRate > 0) {
                cloned->prepareToPlay(sampleRate, 512);
            }
            voiceVec.push_back(cloned);
        } else {
            voiceVec.push_back(nullptr);
        }
    }
}

// ---------------------------------------------------------------------------
// Benchmark test
// ---------------------------------------------------------------------------

class VoiceCloningBenchmark : public juce::UnitTest {
public:
    VoiceCloningBenchmark() : juce::UnitTest("Voice Cloning Benchmark", "VoiceCloning") {}

    void runTest() override {
        const double sampleRate = 48000.0;

        // Build global effects once (like the processor constructor does)
        auto globalEffects = buildGlobalEffects();
        const int numEffects = static_cast<int>(globalEffects.size());
        juce::Logger::outputDebugString("Global effects count: " + juce::String(numEffects));

        // ---------------------------------------------------------------
        // Benchmark 1: Single voice clone (full initializeEffectsFromGlobal)
        // ---------------------------------------------------------------
        beginTest("Single voice clone (map-based, with prepareToPlay)");
        {
            // Warm up
            for (int i = 0; i < 3; ++i) {
                std::unordered_map<juce::String, std::shared_ptr<SimpleEffect>> map;
                cloneAllEffects(globalEffects, map, sampleRate);
            }

            const int iterations = 100;
            const auto start = juce::Time::getHighResolutionTicks();
            for (int i = 0; i < iterations; ++i) {
                std::unordered_map<juce::String, std::shared_ptr<SimpleEffect>> map;
                cloneAllEffects(globalEffects, map, sampleRate);
            }
            const auto end = juce::Time::getHighResolutionTicks();
            const double totalMs = juce::Time::highResolutionTicksToSeconds(end - start) * 1000.0;
            const double perCloneMs = totalMs / iterations;
            juce::Logger::outputDebugString(juce::String::formatted(
                "  Single voice clone (map): %.3f ms avg (%.1f ms total / %d iterations)",
                perCloneMs, totalMs, iterations));
            expectGreaterThan(totalMs, 0.0);
        }

        // ---------------------------------------------------------------
        // Benchmark 2: Single voice clone (vector-based alternative)
        // ---------------------------------------------------------------
        beginTest("Single voice clone (vector-based, with prepareToPlay)");
        {
            for (int i = 0; i < 3; ++i) {
                std::vector<std::shared_ptr<SimpleEffect>> vec;
                cloneAllEffectsVector(globalEffects, vec, sampleRate);
            }

            const int iterations = 100;
            const auto start = juce::Time::getHighResolutionTicks();
            for (int i = 0; i < iterations; ++i) {
                std::vector<std::shared_ptr<SimpleEffect>> vec;
                cloneAllEffectsVector(globalEffects, vec, sampleRate);
            }
            const auto end = juce::Time::getHighResolutionTicks();
            const double totalMs = juce::Time::highResolutionTicksToSeconds(end - start) * 1000.0;
            const double perCloneMs = totalMs / iterations;
            juce::Logger::outputDebugString(juce::String::formatted(
                "  Single voice clone (vec): %.3f ms avg (%.1f ms total / %d iterations)",
                perCloneMs, totalMs, iterations));
            expectGreaterThan(totalMs, 0.0);
        }

        // ---------------------------------------------------------------
        // Benchmark 3: Breakdown — clone only (no prepareToPlay)
        // ---------------------------------------------------------------
        beginTest("Clone only (no prepareToPlay)");
        {
            const int iterations = 100;
            const auto start = juce::Time::getHighResolutionTicks();
            for (int i = 0; i < iterations; ++i) {
                std::vector<std::shared_ptr<SimpleEffect>> clones;
                clones.reserve(globalEffects.size());
                for (auto& ge : globalEffects) {
                    auto se = std::dynamic_pointer_cast<SimpleEffect>(ge);
                    if (se) clones.push_back(se->cloneWithSharedParameters());
                }
            }
            const auto end = juce::Time::getHighResolutionTicks();
            const double totalMs = juce::Time::highResolutionTicksToSeconds(end - start) * 1000.0;
            const double perIterMs = totalMs / iterations;
            juce::Logger::outputDebugString(juce::String::formatted(
                "  Clone only (no prepare): %.3f ms avg", perIterMs));
            expectGreaterThan(totalMs, 0.0);
        }

        // ---------------------------------------------------------------
        // Benchmark 4: Breakdown — DelayEffect clone only
        // ---------------------------------------------------------------
        beginTest("DelayEffect clone isolation (46 MB alloc)");
        {
            auto delayEffect = BenchDelayLikeEffect().build();
            auto delaySE = std::dynamic_pointer_cast<SimpleEffect>(delayEffect);

            const int iterations = 50;
            const auto start = juce::Time::getHighResolutionTicks();
            for (int i = 0; i < iterations; ++i) {
                auto cloned = delaySE->cloneWithSharedParameters();
                cloned->prepareToPlay(sampleRate, 512);
            }
            const auto end = juce::Time::getHighResolutionTicks();
            const double totalMs = juce::Time::highResolutionTicksToSeconds(end - start) * 1000.0;
            const double perCloneMs = totalMs / iterations;
            juce::Logger::outputDebugString(juce::String::formatted(
                "  DelayEffect clone: %.3f ms avg (%.1f ms total / %d iters)",
                perCloneMs, totalMs, iterations));
            expectGreaterThan(totalMs, 0.0);
            testutil::cleanupEffectParams(*delayEffect);
        }

        // ---------------------------------------------------------------
        // Benchmark 5: Breakdown — stateless effect clone only
        // ---------------------------------------------------------------
        beginTest("Stateless effect clone isolation");
        {
            auto effect = BenchStatelessEffect().build();
            auto se = std::dynamic_pointer_cast<SimpleEffect>(effect);

            const int iterations = 1000;
            const auto start = juce::Time::getHighResolutionTicks();
            for (int i = 0; i < iterations; ++i) {
                auto cloned = se->cloneWithSharedParameters();
                cloned->prepareToPlay(sampleRate, 512);
            }
            const auto end = juce::Time::getHighResolutionTicks();
            const double totalMs = juce::Time::highResolutionTicksToSeconds(end - start) * 1000.0;
            const double perCloneUs = (totalMs / iterations) * 1000.0;
            juce::Logger::outputDebugString(juce::String::formatted(
                "  Stateless clone: %.1f us avg", perCloneUs));
            expectGreaterThan(totalMs, 0.0);
            testutil::cleanupEffectParams(*effect);
        }

        // ---------------------------------------------------------------
        // Benchmark 6: Worst case — add max voices (1 -> 16)
        // ---------------------------------------------------------------
        beginTest("Worst case: clone effects for 15 new voices (1->16)");
        {
            const int newVoices = 15;

            const int iterations = 10;
            const auto start = juce::Time::getHighResolutionTicks();
            for (int i = 0; i < iterations; ++i) {
                std::vector<std::unordered_map<juce::String, std::shared_ptr<SimpleEffect>>> voiceMaps(newVoices);
                for (int v = 0; v < newVoices; ++v) {
                    cloneAllEffects(globalEffects, voiceMaps[v], sampleRate);
                }
            }
            const auto end = juce::Time::getHighResolutionTicks();
            const double totalMs = juce::Time::highResolutionTicksToSeconds(end - start) * 1000.0;
            const double perIterMs = totalMs / iterations;
            juce::Logger::outputDebugString(juce::String::formatted(
                "  15 new voices (map): %.1f ms avg (%.1f ms total / %d iters)",
                perIterMs, totalMs, iterations));
            expectGreaterThan(totalMs, 0.0);
        }

        // ---------------------------------------------------------------
        // Benchmark 7: Effect lookup during render (map vs vector)
        // ---------------------------------------------------------------
        beginTest("Effect lookup: map vs vector (render hot path)");
        {
            // Set up map-based and vector-based voice storage
            std::unordered_map<juce::String, std::shared_ptr<SimpleEffect>> voiceMap;
            cloneAllEffects(globalEffects, voiceMap, sampleRate);

            std::vector<std::shared_ptr<SimpleEffect>> voiceVec;
            cloneAllEffectsVector(globalEffects, voiceVec, sampleRate);

            // Collect IDs in order (for map lookup)
            std::vector<juce::String> effectIds;
            for (auto& ge : globalEffects) effectIds.push_back(ge->getId());

            const int lookups = 100000;

            // Map lookup
            {
                volatile SimpleEffect* sink = nullptr;
                const auto start = juce::Time::getHighResolutionTicks();
                for (int i = 0; i < lookups; ++i) {
                    for (auto& id : effectIds) {
                        auto it = voiceMap.find(id);
                        if (it != voiceMap.end()) sink = it->second.get();
                    }
                }
                const auto end = juce::Time::getHighResolutionTicks();
                const double totalUs = juce::Time::highResolutionTicksToSeconds(end - start) * 1e6;
                const double perLookupNs = totalUs * 1000.0 / (lookups * numEffects);
                juce::Logger::outputDebugString(juce::String::formatted(
                    "  Map lookup: %.1f ns/lookup (%d effects x %d rounds)",
                    perLookupNs, numEffects, lookups));
            }

            // Vector lookup (by index)
            {
                volatile SimpleEffect* sink = nullptr;
                const auto start = juce::Time::getHighResolutionTicks();
                for (int i = 0; i < lookups; ++i) {
                    for (int j = 0; j < numEffects; ++j) {
                        if (voiceVec[j]) sink = voiceVec[j].get();
                    }
                }
                const auto end = juce::Time::getHighResolutionTicks();
                const double totalUs = juce::Time::highResolutionTicksToSeconds(end - start) * 1e6;
                const double perLookupNs = totalUs * 1000.0 / (lookups * numEffects);
                juce::Logger::outputDebugString(juce::String::formatted(
                    "  Vector lookup: %.1f ns/lookup (%d effects x %d rounds)",
                    perLookupNs, numEffects, lookups));
            }

            expectGreaterThan(1.0, 0.0);
        }

        // ---------------------------------------------------------------
        // Benchmark 8: Memory footprint estimate
        // ---------------------------------------------------------------
        beginTest("Memory footprint estimates");
        {
            const size_t pointSize = sizeof(Point);
            const size_t delayBufferSize = 1920000 * pointSize;
            const size_t perVoiceDelayMB = delayBufferSize / (1024 * 1024);
            const size_t maxVoiceDelayMB = 16 * perVoiceDelayMB;

            juce::Logger::outputDebugString("  sizeof(Point) = " + juce::String(pointSize) + " bytes");
            juce::Logger::outputDebugString("  DelayEffect buffer per voice: " + juce::String(perVoiceDelayMB) + " MB");
            juce::Logger::outputDebugString("  DelayEffect buffer at 16 voices: " + juce::String(maxVoiceDelayMB) + " MB");
            juce::Logger::outputDebugString("  Effects per voice: " + juce::String(numEffects));

            expectGreaterThan(static_cast<double>(pointSize), 0.0);
        }

        // Clean up parameters owned by globalEffects (not owned by any AudioProcessor in tests)
        for (auto& effect : globalEffects)
            testutil::cleanupEffectParams(*effect);
    }
};

static VoiceCloningBenchmark voiceCloningBenchmark;
