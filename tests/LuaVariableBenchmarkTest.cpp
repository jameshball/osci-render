#include "LuaBenchmarkHelpers.h"

// ============================================================================
// Benchmark 2: Variable-setting strategies (raw Lua C API)
//
// Compares: many globals vs. single table vs. reusing a table
// ============================================================================

class LuaVariableBenchmarkTest : public juce::UnitTest {
public:
    LuaVariableBenchmarkTest() : juce::UnitTest("Lua Variable Strategy Benchmark", "Lua Benchmark") {}

    void initialise() override {
        initBenchmarkCallbacks();
    }

    void runTest() override {
        const int N = BENCH_ITERS;
        const int warmup = BENCH_WARMUP;
        const auto ALL_VAR_NAMES = getAllVarNames();
        const int NUM_ALL_VARS = getNumAllVars();

        // ---- Part A: C-side cost of setting variables ----
        logHeader("Variable Setting Cost (C-side only, no script execution)");
        juce::Logger::outputDebugString("  Measures raw cost of pushing values into Lua state");
        juce::Logger::outputDebugString("  Iterations: " + juce::String(N));
        logSeparator();

        beginTest("C-side: set 0 globals");
        auto g0 = benchmarkSetGlobals(0, N, warmup);
        logBenchmark("Set 0 globals", g0);

        beginTest("C-side: set 5 globals");
        auto g5 = benchmarkSetGlobals(5, N, warmup);
        logBenchmark("Set 5 globals (core vars)", g5);

        beginTest("C-side: set 31 globals");
        auto g31 = benchmarkSetGlobals(31, N, warmup);
        logBenchmark("Set 31 globals (core + sliders)", g31);

        beginTest("C-side: set all globals");
        auto gAll = benchmarkSetGlobals(NUM_ALL_VARS, N, warmup);
        logBenchmark("Set " + juce::String(NUM_ALL_VARS) + " globals (all vars)", gAll);

        beginTest("C-side: set table(5)");
        auto t5 = benchmarkSetTable(5, N, warmup);
        logBenchmark("Set table with 5 fields", t5);

        beginTest("C-side: set table(31)");
        auto t31 = benchmarkSetTable(31, N, warmup);
        logBenchmark("Set table with 31 fields", t31);

        beginTest("C-side: set table(all)");
        auto tAll = benchmarkSetTable(NUM_ALL_VARS, N, warmup);
        logBenchmark("Set table with " + juce::String(NUM_ALL_VARS) + " fields", tAll);

        beginTest("C-side: reuse table(all)");
        auto rtAll = benchmarkReuseTable(NUM_ALL_VARS, N, warmup);
        logBenchmark("Reuse table, update " + juce::String(NUM_ALL_VARS) + " fields", rtAll);

        logSeparator();
        logComparison("5 vars:  globals vs table", g5, t5);
        logComparison("31 vars: globals vs table", g31, t31);
        logComparison("All vars: globals vs table", gAll, tAll);
        logComparison("All vars: globals vs reuse-table", gAll, rtAll);

        // ---- Part B: Full cycle with script execution ----
        logHeader("Full Cycle: Set Variables + Execute Script + Read Result");
        juce::Logger::outputDebugString("  Compares global access vs table access in actual scripts");
        logSeparator();

        // Script that reads 5 variables
        beginTest("Full: 5 globals, script reads them");
        auto fg5 = benchmarkFullGlobals(5,
            "return {step + phase + sample_rate + frequency + cycle_count, 0}", N, warmup);
        logBenchmark("5 globals + script read", fg5);

        beginTest("Full: 5 in table, script reads via osci.*");
        auto ft5 = benchmarkFullTable(5,
            "return {osci.step + osci.phase + osci.sample_rate + osci.frequency + osci.cycle_count, 0}", N, warmup);
        logBenchmark("table(5) + script osci.* read", ft5);

        beginTest("Full: 5 in table, script caches table");
        auto ftc5 = benchmarkFullTable(5,
            "local o = osci\nreturn {o.step + o.phase + o.sample_rate + o.frequency + o.cycle_count, 0}", N, warmup);
        logBenchmark("table(5) + cached local read", ftc5);

        logSeparator();

        // Script that reads all 43 variables
        beginTest("Full: all globals, script reads all");
        juce::String globalsReadScript = buildGlobalsReadScript();
        auto fgAll = benchmarkFullGlobals(NUM_ALL_VARS, globalsReadScript, N, warmup);
        logBenchmark("All globals + script reads all", fgAll);

        beginTest("Full: all in table, script reads via osci.*");
        juce::String tableReadScript = buildTableReadScript(false);
        auto ftAll = benchmarkFullTable(NUM_ALL_VARS, tableReadScript, N, warmup);
        logBenchmark("table(all) + script osci.* read", ftAll);

        beginTest("Full: all in table, cached local");
        juce::String tableCachedScript = buildTableReadScript(true);
        auto ftcAll = benchmarkFullTable(NUM_ALL_VARS, tableCachedScript, N, warmup);
        logBenchmark("table(all) + cached local read", ftcAll);

        beginTest("Full: all in reused table, cached local");
        auto frtAll = benchmarkFullReuseTable(NUM_ALL_VARS, tableCachedScript, N, warmup);
        logBenchmark("reuse-table(all) + cached local", frtAll);

        logSeparator();
        logComparison("5 vars:  globals vs table", fg5, ft5);
        logComparison("5 vars:  globals vs cached-table", fg5, ftc5);
        logComparison("All vars: globals vs table", fgAll, ftAll);
        logComparison("All vars: globals vs cached-table", fgAll, ftcAll);
        logComparison("All vars: globals vs reuse+cached", fgAll, frtAll);

        // ---- Part C: Script-side read scaling ----
        logHeader("Script-side Variable Read Scaling");
        juce::Logger::outputDebugString("  How does reading more variables in script affect cost?");
        logSeparator();

        beginTest("Script reads 1 global");
        auto sr1 = benchmarkFullGlobals(NUM_ALL_VARS, "return {step, 0}", N, warmup);
        logBenchmark("Read 1 global (step)", sr1);

        beginTest("Script reads 5 globals");
        auto sr5 = benchmarkFullGlobals(NUM_ALL_VARS,
            "return {step + phase + sample_rate + frequency + cycle_count, 0}", N, warmup);
        logBenchmark("Read 5 globals", sr5);

        beginTest("Script reads 10 globals");
        auto sr10 = benchmarkFullGlobals(NUM_ALL_VARS,
            "return {step + phase + sample_rate + frequency + cycle_count"
            " + slider_a + slider_b + slider_c + slider_d + slider_e, 0}", N, warmup);
        logBenchmark("Read 10 globals", sr10);

        beginTest("Script reads 26 globals (all sliders)");
        auto sr26 = benchmarkFullGlobals(NUM_ALL_VARS,
            "return {slider_a + slider_b + slider_c + slider_d + slider_e"
            " + slider_f + slider_g + slider_h + slider_i + slider_j"
            " + slider_k + slider_l + slider_m + slider_n + slider_o"
            " + slider_p + slider_q + slider_r + slider_s + slider_t"
            " + slider_u + slider_v + slider_w + slider_x + slider_y + slider_z, 0}", N, warmup);
        logBenchmark("Read 26 globals (all sliders)", sr26);

        logSeparator();
        logOverheadVs("Cost per extra global read", sr1, sr26, 25);

        expectGreaterThan(g0.callsPerSecond(), 0.0);
    }

private:
    // Benchmark just setting N globals (no script execution)
    BenchmarkResult benchmarkSetGlobals(int numVars, int iterations, int warmup) {
        const auto ALL_VAR_NAMES = getAllVarNames();
        lua_State* L = luaL_newstate();

        for (int i = 0; i < warmup; i++) {
            for (int v = 0; v < numVars; v++) {
                lua_pushnumber(L, (double)v);
                lua_setglobal(L, ALL_VAR_NAMES[v]);
            }
        }

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) {
            for (int v = 0; v < numVars; v++) {
                lua_pushnumber(L, (double)v + i * 0.001);
                lua_setglobal(L, ALL_VAR_NAMES[v]);
            }
        }
        const auto end = juce::Time::getHighResolutionTicks();

        lua_close(L);
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    // Benchmark setting a table with N fields (new table each time)
    BenchmarkResult benchmarkSetTable(int numVars, int iterations, int warmup) {
        const auto ALL_VAR_NAMES = getAllVarNames();
        lua_State* L = luaL_newstate();

        for (int i = 0; i < warmup; i++) {
            lua_createtable(L, 0, numVars);
            for (int v = 0; v < numVars; v++) {
                lua_pushnumber(L, (double)v);
                lua_setfield(L, -2, ALL_VAR_NAMES[v]);
            }
            lua_setglobal(L, "osci");
        }

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) {
            lua_createtable(L, 0, numVars);
            for (int v = 0; v < numVars; v++) {
                lua_pushnumber(L, (double)v + i * 0.001);
                lua_setfield(L, -2, ALL_VAR_NAMES[v]);
            }
            lua_setglobal(L, "osci");
        }
        const auto end = juce::Time::getHighResolutionTicks();

        lua_close(L);
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    // Benchmark reusing an existing table (update fields in-place)
    BenchmarkResult benchmarkReuseTable(int numVars, int iterations, int warmup) {
        const auto ALL_VAR_NAMES = getAllVarNames();
        lua_State* L = luaL_newstate();

        // Create the table once
        lua_createtable(L, 0, numVars);
        for (int v = 0; v < numVars; v++) {
            lua_pushnumber(L, 0.0);
            lua_setfield(L, -2, ALL_VAR_NAMES[v]);
        }
        lua_setglobal(L, "osci");

        for (int i = 0; i < warmup; i++) {
            lua_getglobal(L, "osci");
            for (int v = 0; v < numVars; v++) {
                lua_pushnumber(L, (double)v);
                lua_setfield(L, -2, ALL_VAR_NAMES[v]);
            }
            lua_pop(L, 1);
        }

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) {
            lua_getglobal(L, "osci");
            for (int v = 0; v < numVars; v++) {
                lua_pushnumber(L, (double)v + i * 0.001);
                lua_setfield(L, -2, ALL_VAR_NAMES[v]);
            }
            lua_pop(L, 1);
        }
        const auto end = juce::Time::getHighResolutionTicks();

        lua_close(L);
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    // Full cycle: set N globals + call script + read result
    BenchmarkResult benchmarkFullGlobals(int numVars, const juce::String& script, int iterations, int warmup) {
        const auto ALL_VAR_NAMES = getAllVarNames();
        lua_State* L = luaL_newstate();
        luaL_openlibs(L);
        luaopen_oscilibrary(L);

        if (luaL_loadstring(L, script.toUTF8()) != 0) {
            juce::Logger::outputDebugString("  ERROR compiling script: " + juce::String(lua_tostring(L, -1)));
            lua_close(L);
            return {0, 0};
        }
        int funcRef = luaL_ref(L, LUA_REGISTRYINDEX);

        auto runOnce = [&](int i) {
            for (int v = 0; v < numVars; v++) {
                lua_pushnumber(L, (double)v + i * 0.001);
                lua_setglobal(L, ALL_VAR_NAMES[v]);
            }
            lua_rawgeti(L, LUA_REGISTRYINDEX, funcRef);
            lua_pcall(L, 0, LUA_MULTRET, 0);
            lua_settop(L, 0);
        };

        for (int i = 0; i < warmup; i++) runOnce(i);

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) runOnce(i);
        const auto end = juce::Time::getHighResolutionTicks();

        lua_close(L);
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    // Full cycle: set table + call script + read result
    BenchmarkResult benchmarkFullTable(int numVars, const juce::String& script, int iterations, int warmup) {
        const auto ALL_VAR_NAMES = getAllVarNames();
        lua_State* L = luaL_newstate();
        luaL_openlibs(L);
        luaopen_oscilibrary(L);

        if (luaL_loadstring(L, script.toUTF8()) != 0) {
            juce::Logger::outputDebugString("  ERROR compiling script: " + juce::String(lua_tostring(L, -1)));
            lua_close(L);
            return {0, 0};
        }
        int funcRef = luaL_ref(L, LUA_REGISTRYINDEX);

        auto runOnce = [&](int i) {
            lua_createtable(L, 0, numVars);
            for (int v = 0; v < numVars; v++) {
                lua_pushnumber(L, (double)v + i * 0.001);
                lua_setfield(L, -2, ALL_VAR_NAMES[v]);
            }
            lua_setglobal(L, "osci");

            lua_rawgeti(L, LUA_REGISTRYINDEX, funcRef);
            lua_pcall(L, 0, LUA_MULTRET, 0);
            lua_settop(L, 0);
        };

        for (int i = 0; i < warmup; i++) runOnce(i);

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) runOnce(i);
        const auto end = juce::Time::getHighResolutionTicks();

        lua_close(L);
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    // Full cycle: reuse existing table + call script + read result
    BenchmarkResult benchmarkFullReuseTable(int numVars, const juce::String& script, int iterations, int warmup) {
        const auto ALL_VAR_NAMES = getAllVarNames();
        lua_State* L = luaL_newstate();
        luaL_openlibs(L);
        luaopen_oscilibrary(L);

        // Create the persistent table
        lua_createtable(L, 0, numVars);
        for (int v = 0; v < numVars; v++) {
            lua_pushnumber(L, 0.0);
            lua_setfield(L, -2, ALL_VAR_NAMES[v]);
        }
        lua_setglobal(L, "osci");

        if (luaL_loadstring(L, script.toUTF8()) != 0) {
            juce::Logger::outputDebugString("  ERROR compiling script: " + juce::String(lua_tostring(L, -1)));
            lua_close(L);
            return {0, 0};
        }
        int funcRef = luaL_ref(L, LUA_REGISTRYINDEX);

        auto runOnce = [&](int i) {
            lua_getglobal(L, "osci");
            for (int v = 0; v < numVars; v++) {
                lua_pushnumber(L, (double)v + i * 0.001);
                lua_setfield(L, -2, ALL_VAR_NAMES[v]);
            }
            lua_pop(L, 1);

            lua_rawgeti(L, LUA_REGISTRYINDEX, funcRef);
            lua_pcall(L, 0, LUA_MULTRET, 0);
            lua_settop(L, 0);
        };

        for (int i = 0; i < warmup; i++) runOnce(i);

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) runOnce(i);
        const auto end = juce::Time::getHighResolutionTicks();

        lua_close(L);
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    // Build scripts that read all variables
    juce::String buildGlobalsReadScript() {
        const auto ALL_VAR_NAMES = getAllVarNames();
        const int NUM_ALL_VARS = getNumAllVars();
        juce::String s = "return {";
        for (int i = 0; i < NUM_ALL_VARS; i++) {
            if (i > 0) s += " + ";
            s += ALL_VAR_NAMES[i];
        }
        s += ", 0}";
        return s;
    }

    juce::String buildTableReadScript(bool cached) {
        const auto ALL_VAR_NAMES = getAllVarNames();
        const int NUM_ALL_VARS = getNumAllVars();
        juce::String s;
        if (cached)
            s = "local o = osci\nreturn {";
        else
            s = "return {";

        const char* prefix = cached ? "o." : "osci.";
        for (int i = 0; i < NUM_ALL_VARS; i++) {
            if (i > 0) s += " + ";
            s += prefix;
            s += ALL_VAR_NAMES[i];
        }
        s += ", 0}";
        return s;
    }

    void logComparison(const juce::String& label, const BenchmarkResult& a, const BenchmarkResult& b) {
        double speedup = a.perCallNanoseconds() / b.perCallNanoseconds();
        double diffNs = b.perCallNanoseconds() - a.perCallNanoseconds();
        juce::String faster = speedup > 1.0 ? "table FASTER" : "globals FASTER";
        juce::Logger::outputDebugString(juce::String::formatted(
            "  %-50s  diff: %+.1f ns  ratio: %.2fx  (%s)",
            label.toRawUTF8(), diffNs, speedup, faster.toRawUTF8()));
    }

    void logOverheadVs(const juce::String& label, const BenchmarkResult& base, const BenchmarkResult& more, int extraCount) {
        double perExtra = (more.perCallNanoseconds() - base.perCallNanoseconds()) / extraCount;
        juce::Logger::outputDebugString(juce::String::formatted(
            "  %-50s  %.1f ns per extra variable read",
            label.toRawUTF8(), perExtra));
    }
};

static LuaVariableBenchmarkTest luaVariableBenchmarkTest;
