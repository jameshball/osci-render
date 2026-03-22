#include "LuaBenchmarkHelpers.h"

// ============================================================================
// Benchmark 1: Full LuaParser::run() pipeline
// ============================================================================

class LuaRunBenchmarkTest : public juce::UnitTest {
public:
    LuaRunBenchmarkTest() : juce::UnitTest("Lua Run Pipeline Benchmark", "Lua Benchmark") {}

    void initialise() override {
        initBenchmarkCallbacks();
    }

    void runTest() override {
        const int N = BENCH_ITERS_FULL;
        const int warmup = BENCH_WARMUP;

        logHeader("Full LuaParser::run() Pipeline");
        juce::Logger::outputDebugString("  Each call = setGlobalVariables + lua_pcall + readTable + incrementVars");
        juce::Logger::outputDebugString("  Iterations: " + juce::String(N));
        logSeparator();

        beginTest("Baseline: return {0, 0}");
        auto r1 = benchmarkRun("return {0, 0}", N, warmup);
        logBenchmark("return {0, 0}", r1);

        beginTest("Variable access: return {step, phase}");
        auto r2 = benchmarkRun("return {step, phase}", N, warmup);
        logBenchmark("return {step, phase}", r2);

        beginTest("Read all sliders");
        auto r3 = benchmarkRun(
            "return {slider_a + slider_b + slider_c + slider_d + slider_e +"
            " slider_f + slider_g + slider_h + slider_i + slider_j +"
            " slider_k + slider_l + slider_m + slider_n + slider_o +"
            " slider_p + slider_q + slider_r + slider_s + slider_t +"
            " slider_u + slider_v + slider_w + slider_x + slider_y + slider_z, 0}",
            N, warmup);
        logBenchmark("Sum all 26 sliders", r3);

        beginTest("osci_circle(phase)");
        auto r4 = benchmarkRun("return osci_circle(phase)", N, warmup);
        logBenchmark("osci_circle(phase)", r4);

        beginTest("Complex: circle + rotate + scale");
        auto r5 = benchmarkRun(
            "local p = osci_circle(phase)\n"
            "p = osci_rotate(p, phase * 0.1)\n"
            "p = osci_scale(p, {slider_a + 0.5, slider_b + 0.5, 1})\n"
            "return p",
            N, warmup);
        logBenchmark("circle + rotate + scale", r5);

        beginTest("Heavy: chain of 4 shapes");
        auto r6 = benchmarkRun(
            "return osci_chain(phase, {\n"
            "  function(p) return osci_circle(p) end,\n"
            "  function(p) return osci_line(p) end,\n"
            "  function(p) return osci_polygon(p, 5) end,\n"
            "  function(p) return osci_circle(p) end\n"
            "})",
            N, warmup);
        logBenchmark("osci_chain with 4 shapes", r6);

        logSeparator();
        logOverhead("Variable access overhead", r1, r2);
        logOverhead("All sliders read overhead", r1, r3);
        logOverhead("osci_circle overhead", r1, r4);
        logOverhead("Complex script overhead", r1, r5);
        logOverhead("Heavy chain overhead", r1, r6);

        expectGreaterThan(r1.callsPerSecond(), 0.0);
    }

private:
    BenchmarkResult benchmarkRun(const juce::String& script, int iterations, int warmup) {
        LuaParser parser("bench.lua", script, [](int, juce::String, juce::String) {});
        lua_State* L = nullptr;

        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        vars.step = 1;
        vars.phase = 0;
        for (int i = 0; i < NUM_SLIDERS; i++)
            vars.sliders[i] = 0.5;

        // Warmup
        for (int i = 0; i < warmup; i++)
            parser.run(L, vars);

        // Reset phase for consistent benchmark
        vars.step = 1;
        vars.phase = 0;

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++)
            parser.run(L, vars);
        const auto end = juce::Time::getHighResolutionTicks();

        parser.close(L);
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    void logOverhead(const juce::String& label, const BenchmarkResult& baseline, const BenchmarkResult& test) {
        double overheadNs = test.perCallNanoseconds() - baseline.perCallNanoseconds();
        double pct = (overheadNs / baseline.perCallNanoseconds()) * 100.0;
        juce::Logger::outputDebugString(juce::String::formatted(
            "  %-50s  %+10.1f ns  (%+.1f%%)",
            label.toRawUTF8(), overheadNs, pct));
    }
};

static LuaRunBenchmarkTest luaRunBenchmarkTest;
