#include "LuaBenchmarkHelpers.h"

// ============================================================================
// Benchmark 3: Standard library function overhead
//
// Compares osci_* C functions vs equivalent pure-Lua implementations
// ============================================================================

class LuaStdlibBenchmarkTest : public juce::UnitTest {
public:
    LuaStdlibBenchmarkTest() : juce::UnitTest("Lua Stdlib Overhead Benchmark", "Lua Benchmark") {}

    void initialise() override {
        initBenchmarkCallbacks();
    }

    void runTest() override {
        const int N = BENCH_ITERS;
        const int warmup = BENCH_WARMUP;

        logHeader("Standard Library Function Overhead");
        juce::Logger::outputDebugString("  Compares osci_* (C bridge) vs equivalent pure-Lua code");
        juce::Logger::outputDebugString("  All tests set phase global before each call");
        logSeparator();

        // --- Circle ---
        beginTest("osci_circle vs pure Lua circle");
        auto cCircle = benchmarkScript(
            "return osci_circle(phase)", N, warmup);
        logBenchmark("osci_circle(phase)         [C]", cCircle);

        auto luaCircle = benchmarkScript(
            "local t = phase / (2 * math.pi)\n"
            "local angle = 2 * math.pi * t\n"
            "return {0.8 * math.cos(angle), 0.8 * math.sin(angle)}", N, warmup);
        logBenchmark("Pure Lua circle (cos/sin)  [Lua]", luaCircle);

        logSeparator();
        logComparison("Circle", cCircle, luaCircle);
        logSeparator();

        // --- Lerp ---
        beginTest("osci_lerp vs pure Lua lerp");
        auto cLerp = benchmarkScript(
            "return {osci_lerp(0, 1, phase / (2 * math.pi)), 0}", N, warmup);
        logBenchmark("osci_lerp(a,b,t)           [C]", cLerp);

        auto luaLerp = benchmarkScript(
            "local t = phase / (2 * math.pi)\n"
            "return {0 + (1 - 0) * t, 0}", N, warmup);
        logBenchmark("Pure Lua lerp (inline)     [Lua]", luaLerp);

        logSeparator();
        logComparison("Lerp", cLerp, luaLerp);
        logSeparator();

        // --- Rotate ---
        beginTest("osci_rotate vs pure Lua rotate");
        auto cRotate = benchmarkScript(
            "return osci_rotate({0.5, 0.3}, phase)", N, warmup);
        logBenchmark("osci_rotate(p, angle)      [C]", cRotate);

        auto luaRotate = benchmarkScript(
            "local c = math.cos(phase)\n"
            "local s = math.sin(phase)\n"
            "local x, y = 0.5, 0.3\n"
            "return {c*x - s*y, s*x + c*y}", N, warmup);
        logBenchmark("Pure Lua rotate (cos/sin)  [Lua]", luaRotate);

        logSeparator();
        logComparison("Rotate", cRotate, luaRotate);
        logSeparator();

        // --- Normalize ---
        beginTest("osci_normalize vs pure Lua normalize");
        auto cNorm = benchmarkScript(
            "return osci_normalize({3 + phase * 0.001, 0, 4})", N, warmup);
        logBenchmark("osci_normalize(v)          [C]", cNorm);

        auto luaNorm = benchmarkScript(
            "local x, y, z = 3 + phase * 0.001, 0, 4\n"
            "local len = math.sqrt(x*x + y*y + z*z)\n"
            "return {x/len, y/len, z/len}", N, warmup);
        logBenchmark("Pure Lua normalize         [Lua]", luaNorm);

        logSeparator();
        logComparison("Normalize", cNorm, luaNorm);
        logSeparator();

        // --- Table creation overhead ---
        beginTest("C function returning table vs Lua table creation");
        auto cTable = benchmarkScript(
            "return osci_line(phase)", N, warmup);
        logBenchmark("osci_line (returns 3-elem) [C]", cTable);

        auto luaTable = benchmarkScript(
            "local t = phase / (2 * math.pi)\n"
            "return {-1 + 2*t, -1 + 2*t, 0}", N, warmup);
        logBenchmark("Pure Lua {x, y, z} return  [Lua]", luaTable);

        logSeparator();
        logComparison("Table creation", cTable, luaTable);

        // --- Overall summary ---
        logHeader("Summary: C function call overhead");
        juce::Logger::outputDebugString("  The C function bridge adds overhead from:");
        juce::Logger::outputDebugString("  - Lua -> C function dispatch");
        juce::Logger::outputDebugString("  - Argument marshalling (tableToPoint)");
        juce::Logger::outputDebugString("  - Result marshalling (pointToTable)");
        juce::Logger::outputDebugString("  - But provides: reusable primitives, correct geometry, no user code errors");

        expectGreaterThan(cCircle.callsPerSecond(), 0.0);
    }

private:
    BenchmarkResult benchmarkScript(const juce::String& script, int iterations, int warmup) {
        lua_State* L = luaL_newstate();
        luaL_openlibs(L);
        luaopen_oscilibrary(L);

        if (luaL_loadstring(L, script.toUTF8()) != 0) {
            juce::Logger::outputDebugString("  ERROR: " + juce::String(lua_tostring(L, -1)));
            lua_close(L);
            return {0, 0};
        }
        int funcRef = luaL_ref(L, LUA_REGISTRYINDEX);

        double phase = 0.0;
        double phaseInc = 2.0 * juce::MathConstants<double>::pi * 440.0 / 44100.0;

        auto runOnce = [&]() {
            lua_pushnumber(L, phase);
            lua_setglobal(L, "phase");
            lua_rawgeti(L, LUA_REGISTRYINDEX, funcRef);
            lua_pcall(L, 0, LUA_MULTRET, 0);
            lua_settop(L, 0);
            phase += phaseInc;
        };

        for (int i = 0; i < warmup; i++) runOnce();

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) runOnce();
        const auto end = juce::Time::getHighResolutionTicks();

        lua_close(L);
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    void logComparison(const juce::String& label, const BenchmarkResult& c, const BenchmarkResult& lua) {
        double overheadNs = c.perCallNanoseconds() - lua.perCallNanoseconds();
        double ratio = c.perCallNanoseconds() / lua.perCallNanoseconds();
        juce::String verdict = overheadNs > 0 ? "C is slower" : "C is faster";
        juce::Logger::outputDebugString(juce::String::formatted(
            "  %-50s  C-Lua diff: %+.1f ns  ratio: %.2fx  (%s)",
            label.toRawUTF8(), overheadNs, ratio, verdict.toRawUTF8()));
    }
};

static LuaStdlibBenchmarkTest luaStdlibBenchmarkTest;
