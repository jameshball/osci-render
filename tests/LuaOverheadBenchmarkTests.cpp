#include "LuaBenchmarkHelpers.h"

// ============================================================================
// Benchmark 4: LuaParser::run() overhead vs raw Lua C API
//
// Measures the cost added by the LuaParser wrapper itself
// ============================================================================

class LuaParserOverheadBenchmarkTest : public juce::UnitTest {
public:
    LuaParserOverheadBenchmarkTest() : juce::UnitTest("Lua Parser Overhead Benchmark", "Lua Benchmark") {}

    void initialise() override {
        initBenchmarkCallbacks();
    }

    void runTest() override {
        const int N = BENCH_ITERS_FULL;
        const int warmup = BENCH_WARMUP;
        const juce::String script = "return {step + phase, 0}";

        logHeader("LuaParser::run() Overhead vs Raw Lua C API");
        juce::Logger::outputDebugString("  Both execute the same script: " + script);
        logSeparator();

        beginTest("Raw Lua C API (minimal wrapper)");
        auto rawResult = benchmarkRawApi(script, N, warmup);
        logBenchmark("Raw Lua C API", rawResult);

        beginTest("LuaParser::run() (full pipeline)");
        auto parserResult = benchmarkLuaParser(script, N, warmup);
        logBenchmark("LuaParser::run()", parserResult);

        logSeparator();
        double overheadNs = parserResult.perCallNanoseconds() - rawResult.perCallNanoseconds();
        double overheadPct = (overheadNs / rawResult.perCallNanoseconds()) * 100.0;
        juce::Logger::outputDebugString(juce::String::formatted(
            "  LuaParser wrapper overhead: %.1f ns/call (%.1f%%)", overheadNs, overheadPct));
        juce::Logger::outputDebugString("  Sources of overhead:");
        juce::Logger::outputDebugString("    - lastSeenState check + seenStates fallback lookup");
        juce::Logger::outputDebugString("    - setMaximumInstructions (hook setup, no reset)");
        juce::Logger::outputDebugString("    - usedVarMask-gated global variable setting");
        juce::Logger::outputDebugString("    - incrementVars (phase calculation)");
        juce::Logger::outputDebugString("    - Error callback infrastructure");

        expectGreaterThan(rawResult.callsPerSecond(), 0.0);
    }

private:
    BenchmarkResult benchmarkRawApi(const juce::String& script, int iterations, int warmup) {
        lua_State* L = luaL_newstate();
        luaL_openlibs(L);
        luaopen_oscilibrary(L);

        luaL_loadstring(L, script.toUTF8());
        int funcRef = luaL_ref(L, LUA_REGISTRYINDEX);

        double step = 1, phase = 0;
        double phaseInc = 2.0 * juce::MathConstants<double>::pi * 440.0 / 44100.0;

        auto runOnce = [&]() {
            lua_pushnumber(L, step);
            lua_setglobal(L, "step");
            lua_pushnumber(L, phase);
            lua_setglobal(L, "phase");

            lua_rawgeti(L, LUA_REGISTRYINDEX, funcRef);
            lua_pcall(L, 0, LUA_MULTRET, 0);
            lua_settop(L, 0);

            step++;
            phase += phaseInc;
        };

        for (int i = 0; i < warmup; i++) runOnce();

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) runOnce();
        const auto end = juce::Time::getHighResolutionTicks();

        lua_close(L);
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    BenchmarkResult benchmarkLuaParser(const juce::String& script, int iterations, int warmup) {
        LuaParser parser("bench.lua", script, [](int, juce::String, juce::String) {});
        lua_State* L = nullptr;

        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        vars.step = 1;
        vars.phase = 0;
        for (int i = 0; i < NUM_SLIDERS; i++)
            vars.sliders[i] = 0.5;

        for (int i = 0; i < warmup; i++)
            parser.run(L, vars);

        vars.step = 1;
        vars.phase = 0;

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++)
            parser.run(L, vars);
        const auto end = juce::Time::getHighResolutionTicks();

        parser.close(L);
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }
};

static LuaParserOverheadBenchmarkTest luaParserOverheadBenchmarkTest;

// ============================================================================
// Benchmark 5: Isolating run() overhead components
//
// Measures each source of overhead within LuaParser::run() individually:
//   - lastSeenState check + seenStates fallback lookup
//   - setMaximumInstructions (hook setup)
//   - readTable (table -> LuaResult marshalling)
//   - incrementVars (phase calculation)
//   - setGlobalVariables (current implementation with all vars)
//   - tableToPoint / pointToTable marshalling in C functions
// ============================================================================

class LuaOverheadBreakdownTest : public juce::UnitTest {
public:
    LuaOverheadBreakdownTest() : juce::UnitTest("Lua Overhead Breakdown", "Lua Benchmark") {}

    void initialise() override {
        initBenchmarkCallbacks();
    }

    void runTest() override {
        const int N = BENCH_ITERS;
        const int warmup = BENCH_WARMUP;
        const auto ALL_VAR_NAMES = getAllVarNames();

        logHeader("Overhead Breakdown: Isolating run() Components");
        juce::Logger::outputDebugString("  Measures each source of overhead in LuaParser::run()");
        logSeparator();

        // 1. Instruction hook setup/teardown (lua_sethook twice per call)
        beginTest("Hook setup + teardown (lua_sethook x2)");
        auto hookResult = benchmarkHookSetup(N, warmup);
        logBenchmark("lua_sethook setup + teardown", hookResult);

        // 2. readTable: extract vector<float> from Lua table
        beginTest("readTable: {x, y} -> vector<float>");
        auto read2 = benchmarkReadTable(2, N, warmup);
        logBenchmark("readTable (2 elements)", read2);

        beginTest("readTable: {x,y,z,r,g,b} -> vector<float>");
        auto read6 = benchmarkReadTable(6, N, warmup);
        logBenchmark("readTable (6 elements)", read6);

        // 3. vector<float> allocation vs pre-sized
        beginTest("vector<float> default alloc + 2 push_back");
        auto vecAlloc = benchmarkVectorAlloc(2, N, warmup);
        logBenchmark("vector alloc + 2 push_back", vecAlloc);

        beginTest("vector<float> reserve(6) + 6 push_back");
        auto vecReserve = benchmarkVectorReserve(6, N, warmup);
        logBenchmark("vector reserve(6) + 6 push_back", vecReserve);

        // 4. std::array<float,6> vs vector<float> for return
        beginTest("std::array<float,6> (stack-allocated)");
        auto arrResult = benchmarkArrayReturn(6, N, warmup);
        logBenchmark("std::array<float,6> fill", arrResult);

        // 5. incrementVars
        beginTest("incrementVars (phase calc)");
        auto incResult = benchmarkIncrementVars(N, warmup);
        logBenchmark("incrementVars", incResult);

        // 6. setGlobalVariables (current full impl)
        beginTest("setGlobalVariables (all 43)");
        auto sgvResult = benchmarkSetGlobalVariables(N, warmup);
        logBenchmark("setGlobalVariables (all)", sgvResult);

        // 7. seenStates lookup (1 state in vector)
        beginTest("seenStates std::find (1 element)");
        auto seen1 = benchmarkSeenStatesLookup(1, N, warmup);
        logBenchmark("seenStates find (1 elem)", seen1);

        beginTest("seenStates std::find (16 elements)");
        auto seen16 = benchmarkSeenStatesLookup(16, N, warmup);
        logBenchmark("seenStates find (16 elem)", seen16);

        // 8. tableToPoint + pointToTable round trip (C function marshalling)
        beginTest("tableToPoint + pointToTable round trip");
        auto marshal = benchmarkMarshalRoundTrip(N, warmup);
        logBenchmark("marshal round trip (2D)", marshal);

        beginTest("tableToPoint + pointToTable (3D+color)");
        auto marshal6 = benchmarkMarshalRoundTrip6(N, warmup);
        logBenchmark("marshal round trip (3D+color)", marshal6);

        // 9. Conditional variable setting (skip unchanged)
        beginTest("setGlobalVariables: only core 5 vars");
        auto sgvCore = benchmarkSetGlobalVariablesPartial(5, N, warmup);
        logBenchmark("setGlobalVars (core 5 only)", sgvCore);

        beginTest("setGlobalVariables: core + sliders (31)");
        auto sgv31 = benchmarkSetGlobalVariablesPartial(31, N, warmup);
        logBenchmark("setGlobalVars (31 vars)", sgv31);

        logSeparator();

        // Summary
        double totalOverhead = hookResult.perCallNanoseconds()
            + sgvResult.perCallNanoseconds()
            + read2.perCallNanoseconds()
            + incResult.perCallNanoseconds()
            + seen1.perCallNanoseconds();

        juce::Logger::outputDebugString("");
        juce::Logger::outputDebugString("  == Estimated run() overhead breakdown (2-element return) ==");
        juce::Logger::outputDebugString(juce::String::formatted(
            "    Hook setup/teardown:    %7.1f ns", hookResult.perCallNanoseconds()));
        juce::Logger::outputDebugString(juce::String::formatted(
            "    setGlobalVariables:     %7.1f ns", sgvResult.perCallNanoseconds()));
        juce::Logger::outputDebugString(juce::String::formatted(
            "    readTable(2):           %7.1f ns", read2.perCallNanoseconds()));
        juce::Logger::outputDebugString(juce::String::formatted(
            "    incrementVars:          %7.1f ns", incResult.perCallNanoseconds()));
        juce::Logger::outputDebugString(juce::String::formatted(
            "    seenStates lookup:      %7.1f ns", seen1.perCallNanoseconds()));
        juce::Logger::outputDebugString(juce::String::formatted(
            "    --------------------------------"));
        juce::Logger::outputDebugString(juce::String::formatted(
            "    Estimated total:        %7.1f ns", totalOverhead));
        juce::Logger::outputDebugString("");
        juce::Logger::outputDebugString(juce::String::formatted(
            "    Potential savings from reserve(6) vs default alloc: %.1f ns",
            vecAlloc.perCallNanoseconds() - vecReserve.perCallNanoseconds()));
        juce::Logger::outputDebugString(juce::String::formatted(
            "    Potential savings from std::array vs vector:        %.1f ns",
            vecAlloc.perCallNanoseconds() - arrResult.perCallNanoseconds()));
        juce::Logger::outputDebugString(juce::String::formatted(
            "    Potential savings from skipping non-core vars:      %.1f ns",
            sgvResult.perCallNanoseconds() - sgvCore.perCallNanoseconds()));

        expectGreaterThan(hookResult.callsPerSecond(), 0.0);
    }

private:
    BenchmarkResult benchmarkHookSetup(int iterations, int warmup) {
        lua_State* L = luaL_newstate();

        // Dummy hook callback matching the signature LuaParser uses
        auto dummyHook = [](lua_State*, lua_Debug*) {};

        for (int i = 0; i < warmup; i++) {
            lua_sethook(L, dummyHook, LUA_MASKCOUNT, 5000000);
            lua_sethook(L, dummyHook, 0, 0);
        }

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) {
            lua_sethook(L, dummyHook, LUA_MASKCOUNT, 5000000);
            lua_sethook(L, dummyHook, 0, 0);
        }
        const auto end = juce::Time::getHighResolutionTicks();

        lua_close(L);
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    BenchmarkResult benchmarkReadTable(int numElements, int iterations, int warmup) {
        lua_State* L = luaL_newstate();

        // Pre-create a table on the stack
        auto pushTable = [&]() {
            lua_createtable(L, numElements, 0);
            for (int i = 1; i <= numElements; i++) {
                lua_pushnumber(L, (double)i * 0.5);
                lua_rawseti(L, -2, i);
            }
        };

        for (int i = 0; i < warmup; i++) {
            pushTable();
            std::vector<float> values;
            auto length = lua_objlen(L, -1);
            for (int j = 1; j <= (int)length; j++) {
                lua_pushinteger(L, j);
                lua_gettable(L, -2);
                values.push_back((float)lua_tonumber(L, -1));
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) {
            pushTable();
            std::vector<float> values;
            auto length = lua_objlen(L, -1);
            for (int j = 1; j <= (int)length; j++) {
                lua_pushinteger(L, j);
                lua_gettable(L, -2);
                values.push_back((float)lua_tonumber(L, -1));
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }
        const auto end = juce::Time::getHighResolutionTicks();

        lua_close(L);
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    BenchmarkResult benchmarkVectorAlloc(int numElements, int iterations, int warmup) {
        volatile float sink = 0;
        for (int i = 0; i < warmup; i++) {
            std::vector<float> v;
            for (int j = 0; j < numElements; j++) v.push_back((float)j);
            sink = v[0];
        }

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) {
            std::vector<float> v;
            for (int j = 0; j < numElements; j++) v.push_back((float)j + i * 0.001f);
            sink = v[0];
        }
        const auto end = juce::Time::getHighResolutionTicks();

        (void)sink;
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    BenchmarkResult benchmarkVectorReserve(int numElements, int iterations, int warmup) {
        volatile float sink = 0;
        for (int i = 0; i < warmup; i++) {
            std::vector<float> v;
            v.reserve(numElements);
            for (int j = 0; j < numElements; j++) v.push_back((float)j);
            sink = v[0];
        }

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) {
            std::vector<float> v;
            v.reserve(numElements);
            for (int j = 0; j < numElements; j++) v.push_back((float)j + i * 0.001f);
            sink = v[0];
        }
        const auto end = juce::Time::getHighResolutionTicks();

        (void)sink;
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    BenchmarkResult benchmarkArrayReturn(int numElements, int iterations, int warmup) {
        volatile float sink = 0;
        for (int i = 0; i < warmup; i++) {
            std::array<float, 6> a = {};
            for (int j = 0; j < numElements; j++) a[j] = (float)j;
            sink = a[0];
        }

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) {
            std::array<float, 6> a = {};
            for (int j = 0; j < numElements; j++) a[j] = (float)j + i * 0.001f;
            sink = a[0];
        }
        const auto end = juce::Time::getHighResolutionTicks();

        (void)sink;
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    BenchmarkResult benchmarkIncrementVars(int iterations, int warmup) {
        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        vars.step = 1;
        vars.phase = 0;

        for (int i = 0; i < warmup; i++) {
            vars.step++;
            vars.phase += 2 * std::numbers::pi * vars.frequency / vars.sampleRate;
            if (vars.phase > 2 * std::numbers::pi) {
                vars.phase -= 2 * std::numbers::pi;
                vars.cycle += 1;
            }
        }

        vars.step = 1;
        vars.phase = 0;
        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) {
            vars.step++;
            vars.phase += 2 * std::numbers::pi * vars.frequency / vars.sampleRate;
            if (vars.phase > 2 * std::numbers::pi) {
                vars.phase -= 2 * std::numbers::pi;
                vars.cycle += 1;
            }
        }
        const auto end = juce::Time::getHighResolutionTicks();

        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    BenchmarkResult benchmarkSetGlobalVariables(int iterations, int warmup) {
        lua_State* L = luaL_newstate();
        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        vars.step = 1;
        vars.phase = 0;
        vars.isEffect = true;
        for (int i = 0; i < NUM_SLIDERS; i++) vars.sliders[i] = 0.5;

        auto setVars = [&]() {
            lua_pushnumber(L, vars.step); lua_setglobal(L, "step");
            lua_pushnumber(L, vars.sampleRate); lua_setglobal(L, "sample_rate");
            lua_pushnumber(L, vars.frequency); lua_setglobal(L, "frequency");
            lua_pushnumber(L, vars.phase); lua_setglobal(L, "phase");
            lua_pushnumber(L, vars.cycle); lua_setglobal(L, "cycle_count");
            for (int i = 0; i < NUM_SLIDERS; i++) {
                lua_pushnumber(L, vars.sliders[i]);
                lua_setglobal(L, SLIDER_NAMES[i]);
            }
            lua_pushnumber(L, vars.x); lua_setglobal(L, "x");
            lua_pushnumber(L, vars.y); lua_setglobal(L, "y");
            lua_pushnumber(L, vars.z); lua_setglobal(L, "z");
            lua_pushnumber(L, vars.ext_x); lua_setglobal(L, "ext_x");
            lua_pushnumber(L, vars.ext_y); lua_setglobal(L, "ext_y");
            lua_pushnumber(L, vars.midiNote); lua_setglobal(L, "midi_note");
            lua_pushnumber(L, vars.velocity); lua_setglobal(L, "velocity");
            lua_pushnumber(L, vars.voiceIndex); lua_setglobal(L, "voice_index");
            lua_pushboolean(L, vars.noteOn ? 1 : 0); lua_setglobal(L, "note_on");
            lua_pushnumber(L, vars.bpm); lua_setglobal(L, "bpm");
            lua_pushnumber(L, vars.playTime); lua_setglobal(L, "play_time");
            lua_pushnumber(L, vars.playTimeBeats); lua_setglobal(L, "play_time_beats");
            lua_pushboolean(L, vars.isPlaying ? 1 : 0); lua_setglobal(L, "is_playing");
            lua_pushnumber(L, vars.timeSigNumerator); lua_setglobal(L, "time_sig_num");
            lua_pushnumber(L, vars.timeSigDenominator); lua_setglobal(L, "time_sig_den");
            lua_pushnumber(L, vars.envelope); lua_setglobal(L, "envelope");
            lua_pushnumber(L, vars.envelopeStage); lua_setglobal(L, "envelope_stage");
        };

        for (int i = 0; i < warmup; i++) setVars();

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) setVars();
        const auto end = juce::Time::getHighResolutionTicks();

        lua_close(L);
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    BenchmarkResult benchmarkSetGlobalVariablesPartial(int numVars, int iterations, int warmup) {
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

    BenchmarkResult benchmarkSeenStatesLookup(int numStates, int iterations, int warmup) {
        std::vector<lua_State*> seenStates;
        // Create fake pointers for the search
        std::vector<std::unique_ptr<int>> fakePointers;
        for (int i = 0; i < numStates; i++) {
            fakePointers.push_back(std::make_unique<int>(i));
            seenStates.push_back(reinterpret_cast<lua_State*>(fakePointers.back().get()));
        }
        lua_State* target = seenStates.back(); // Search for last element (worst case)

        volatile int sink = 0;
        for (int i = 0; i < warmup; i++) {
            auto it = std::find(seenStates.begin(), seenStates.end(), target);
            sink = (int)(it - seenStates.begin());
        }

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) {
            auto it = std::find(seenStates.begin(), seenStates.end(), target);
            sink = (int)(it - seenStates.begin());
        }
        const auto end = juce::Time::getHighResolutionTicks();

        (void)sink;
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    BenchmarkResult benchmarkMarshalRoundTrip(int iterations, int warmup) {
        lua_State* L = luaL_newstate();

        // Register a C function that does tableToPoint -> pointToTable
        lua_pushcfunction(L, [](lua_State* L) -> int {
            osci::Point p;
            // tableToPoint inline (2D)
            lua_pushinteger(L, 1);
            lua_gettable(L, 1);
            p.x = lua_tonumber(L, -1);
            lua_pop(L, 1);
            lua_pushinteger(L, 2);
            lua_gettable(L, 1);
            p.y = lua_tonumber(L, -1);
            lua_pop(L, 1);
            // pointToTable inline (2D)
            lua_newtable(L);
            lua_pushnumber(L, p.x);
            lua_rawseti(L, -2, 1);
            lua_pushnumber(L, p.y);
            lua_rawseti(L, -2, 2);
            return 1;
        });
        lua_setglobal(L, "roundtrip");

        luaL_loadstring(L, "return roundtrip({1.5, 2.5})");
        int funcRef = luaL_ref(L, LUA_REGISTRYINDEX);

        for (int i = 0; i < warmup; i++) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, funcRef);
            lua_pcall(L, 0, 1, 0);
            lua_pop(L, 1);
        }

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, funcRef);
            lua_pcall(L, 0, 1, 0);
            lua_pop(L, 1);
        }
        const auto end = juce::Time::getHighResolutionTicks();

        lua_close(L);
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }

    BenchmarkResult benchmarkMarshalRoundTrip6(int iterations, int warmup) {
        lua_State* L = luaL_newstate();

        // Register a C function that does full 6-element tableToPoint -> pointToTable
        lua_pushcfunction(L, [](lua_State* L) -> int {
            osci::Point p;
            lua_pushinteger(L, 1); lua_gettable(L, 1); p.x = lua_tonumber(L, -1); lua_pop(L, 1);
            lua_pushinteger(L, 2); lua_gettable(L, 1); p.y = lua_tonumber(L, -1); lua_pop(L, 1);
            lua_pushinteger(L, 3); lua_gettable(L, 1); p.z = lua_tonumber(L, -1); lua_pop(L, 1);
            lua_pushinteger(L, 4); lua_gettable(L, 1);
            if (!lua_isnil(L, -1)) {
                p.r = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_pushinteger(L, 5); lua_gettable(L, 1); p.g = lua_tonumber(L, -1); lua_pop(L, 1);
                lua_pushinteger(L, 6); lua_gettable(L, 1); p.b = lua_tonumber(L, -1); lua_pop(L, 1);
            } else { lua_pop(L, 1); }

            lua_newtable(L);
            lua_pushnumber(L, p.x); lua_rawseti(L, -2, 1);
            lua_pushnumber(L, p.y); lua_rawseti(L, -2, 2);
            lua_pushnumber(L, p.z); lua_rawseti(L, -2, 3);
            if (p.r >= 0.0f) {
                lua_pushnumber(L, p.r); lua_rawseti(L, -2, 4);
                lua_pushnumber(L, p.g); lua_rawseti(L, -2, 5);
                lua_pushnumber(L, p.b); lua_rawseti(L, -2, 6);
            }
            return 1;
        });
        lua_setglobal(L, "roundtrip6");

        luaL_loadstring(L, "return roundtrip6({1.5, 2.5, 0.5, 1.0, 0.8, 0.6})");
        int funcRef = luaL_ref(L, LUA_REGISTRYINDEX);

        for (int i = 0; i < warmup; i++) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, funcRef);
            lua_pcall(L, 0, 1, 0);
            lua_pop(L, 1);
        }

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, funcRef);
            lua_pcall(L, 0, 1, 0);
            lua_pop(L, 1);
        }
        const auto end = juce::Time::getHighResolutionTicks();

        lua_close(L);
        return {juce::Time::highResolutionTicksToSeconds(end - start), iterations};
    }
};

static LuaOverheadBreakdownTest luaOverheadBreakdownTest;
