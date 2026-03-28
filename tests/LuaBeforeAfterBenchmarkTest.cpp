#include "LuaBenchmarkHelpers.h"

// ============================================================================
// Benchmark 7: Before vs After Optimization — End-to-End Comparison
//
// Compares the pre-optimization run() logic (all globals, std::find,
// sethook x2, vector<float> return) against the current optimized
// LuaParser::run() (mask-gated globals, ptr cache, sethook once,
// LuaResult return) across multiple scripts.
// ============================================================================

class LuaBeforeAfterComparisonTest : public juce::UnitTest {
public:
    LuaBeforeAfterComparisonTest() : juce::UnitTest("Lua Before/After Optimization Comparison", "Lua Benchmark") {}

    void initialise() override {
        initBenchmarkCallbacks();
    }

    void runTest() override {
        const int N = BENCH_ITERS_FULL;
        const int warmup = BENCH_WARMUP;

        struct Script { const char* label; const char* code; };
        Script scripts[] = {
            { "return {0, 0}             (0 vars used)",        "return {0, 0}" },
            { "return {step, phase}      (2 vars used)",        "return {step, phase}" },
            { "return {slider_a, slider_b} (2 sliders used)",   "return {slider_a, slider_b}" },
            { "return {slider_a..slider_z, step} (27 vars)",
              "return {slider_a+slider_b+slider_c+slider_d+slider_e+"
              "slider_f+slider_g+slider_h+slider_i+slider_j+"
              "slider_k+slider_l+slider_m+slider_n+slider_o+"
              "slider_p+slider_q+slider_r+slider_s+slider_t+"
              "slider_u+slider_v+slider_w+slider_x+slider_y+slider_z+step, 0}" },
        };

        logHeader("Before vs After Optimization — End-to-End");
        juce::Logger::outputDebugString(
            "  Before: all 43 globals set + std::find + sethook x2 + vector<float> return");
        juce::Logger::outputDebugString(
            "  After:  mask-gated globals + ptr cache + sethook once + LuaResult return");
        juce::Logger::outputDebugString("  Iterations: " + juce::String(N));

        for (auto& s : scripts) {
            logSeparator();
            juce::Logger::outputDebugString("  Script: " + juce::String(s.label));

            beginTest(juce::String("Before: ") + s.label);
            auto before = benchmarkBefore(s.code, N, warmup);
            logBenchmark("  before", before);

            beginTest(juce::String("After: ") + s.label);
            auto after = benchmarkAfter(s.code, N, warmup);
            logBenchmark("  after ", after);

            double savedNs  = before.perCallNanoseconds() - after.perCallNanoseconds();
            double savedPct = savedNs / before.perCallNanoseconds() * 100.0;
            juce::Logger::outputDebugString(juce::String::formatted(
                "  speedup: %+.1f ns saved  (%.1f%% faster)", savedNs, savedPct));

            expectGreaterThan(after.callsPerSecond(), 0.0);
        }
        logSeparator();
    }

private:
    // -----------------------------------------------------------------------
    // "Before" runner: replicates the pre-optimization LuaParser::run() logic
    // -----------------------------------------------------------------------
    BenchmarkResult benchmarkBefore(const juce::String& script, int iterations, int warmup) {
        lua_State* L = luaL_newstate();
        luaL_openlibs(L);

        // Compile script once, store ref
        luaL_loadstring(L, script.toUTF8());
        int funcRef = luaL_ref(L, LUA_REGISTRYINDEX);

        // Simulate post-first-call seenStates (L is already "seen")
        std::vector<lua_State*> seenStates;
        seenStates.push_back(L);

        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        vars.step = 1;
        vars.phase = 0;
        for (int i = 0; i < NUM_SLIDERS; i++) vars.sliders[i] = 0.5;

        // Dummy hook matching LuaParser's signature
        static auto dummyHook = [](lua_State*, lua_Debug*) {};

        auto runOnceBefore = [&]() {
            // 1. seenStates lookup (std::find — worst-case for 1 element: finds at index 0)
            [[maybe_unused]] volatile bool found =
                (std::find(seenStates.begin(), seenStates.end(), L) != seenStates.end());

            // 2. Set all 43 globals unconditionally
            lua_pushnumber(L, vars.step);              lua_setglobal(L, "step");
            lua_pushnumber(L, vars.sampleRate);        lua_setglobal(L, "sample_rate");
            lua_pushnumber(L, vars.frequency);         lua_setglobal(L, "frequency");
            lua_pushnumber(L, vars.phase);             lua_setglobal(L, "phase");
            lua_pushnumber(L, vars.cycle);             lua_setglobal(L, "cycle_count");
            for (int i = 0; i < NUM_SLIDERS; i++) {
                lua_pushnumber(L, vars.sliders[i]);
                lua_setglobal(L, SLIDER_NAMES[i]);
            }
            lua_pushnumber(L, vars.x);    lua_setglobal(L, "x");
            lua_pushnumber(L, vars.y);    lua_setglobal(L, "y");
            lua_pushnumber(L, vars.z);    lua_setglobal(L, "z");
            lua_pushnumber(L, vars.ext_x); lua_setglobal(L, "ext_x");
            lua_pushnumber(L, vars.ext_y); lua_setglobal(L, "ext_y");
            lua_pushnumber(L, vars.midiNote);       lua_setglobal(L, "midi_note");
            lua_pushnumber(L, vars.velocity);       lua_setglobal(L, "velocity");
            lua_pushnumber(L, vars.voiceIndex);     lua_setglobal(L, "voice_index");
            lua_pushboolean(L, vars.noteOn ? 1 : 0); lua_setglobal(L, "note_on");
            lua_pushnumber(L, vars.bpm);            lua_setglobal(L, "bpm");
            lua_pushnumber(L, vars.playTime);       lua_setglobal(L, "play_time");
            lua_pushnumber(L, vars.playTimeBeats);  lua_setglobal(L, "play_time_beats");
            lua_pushboolean(L, vars.isPlaying ? 1 : 0); lua_setglobal(L, "is_playing");
            lua_pushnumber(L, vars.timeSigNumerator);   lua_setglobal(L, "time_sig_num");
            lua_pushnumber(L, vars.timeSigDenominator); lua_setglobal(L, "time_sig_den");
            lua_pushnumber(L, vars.envelope);       lua_setglobal(L, "envelope");
            lua_pushnumber(L, vars.envelopeStage);  lua_setglobal(L, "envelope_stage");

            // 3. Get function and install instruction hook
            lua_rawgeti(L, LUA_REGISTRYINDEX, funcRef);
            lua_sethook(L, dummyHook, LUA_MASKCOUNT, 5000000);

            // 4. Call
            if (lua_isfunction(L, -1))
                lua_pcall(L, 0, LUA_MULTRET, 0);

            // 5. readTable -> std::vector<float> using lua_gettable (old impl)
            std::vector<float> values;
            if (lua_istable(L, -1)) {
                auto length = lua_objlen(L, -1);
                for (int i = 1; i <= (int)length; i++) {
                    lua_pushinteger(L, i);
                    lua_gettable(L, -2);
                    values.push_back((float)lua_tonumber(L, -1));
                    lua_pop(L, 1);
                }
            }

            // 6. Reset hook
            lua_sethook(L, dummyHook, 0, 0);

            // 7. Clear stack + increment vars
            lua_settop(L, 0);
            vars.step++;
            vars.phase += 2 * std::numbers::pi * vars.frequency / vars.sampleRate;
            if (vars.phase > 2 * std::numbers::pi) { vars.phase -= 2 * std::numbers::pi; vars.cycle += 1; }
        };

        for (int i = 0; i < warmup; i++) runOnceBefore();
        vars.step = 1; vars.phase = 0;

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) runOnceBefore();
        const auto end = juce::Time::getHighResolutionTicks();

        lua_close(L);
        return { juce::Time::highResolutionTicksToSeconds(end - start), iterations };
    }

    // -----------------------------------------------------------------------
    // "After" runner: current LuaParser::run()
    // -----------------------------------------------------------------------
    BenchmarkResult benchmarkAfter(const juce::String& script, int iterations, int warmup) {
        struct NoOpErrors {
            std::function<void(int, juce::String, juce::String)> callback() {
                return [](int, juce::String, juce::String) {};
            }
        } errors;

        LuaParser parser("bench.lua", script, errors.callback());
        lua_State* L = nullptr;

        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        vars.step = 1;
        vars.phase = 0;
        for (int i = 0; i < NUM_SLIDERS; i++) vars.sliders[i] = 0.5;

        for (int i = 0; i < warmup; i++) parser.run(L, vars);
        vars.step = 1; vars.phase = 0;

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < iterations; i++) parser.run(L, vars);
        const auto end = juce::Time::getHighResolutionTicks();

        parser.close(L);
        return { juce::Time::highResolutionTicksToSeconds(end - start), iterations };
    }
};

static LuaBeforeAfterComparisonTest luaBeforeAfterComparisonTest;
