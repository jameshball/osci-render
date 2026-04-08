#include <JuceHeader.h>
#include "../Source/lua/LuaParser.h"
#include "../Source/lua/LuaLibrary.h"
#include <lua.hpp>

// ============================================================================
// Helpers
// ============================================================================

struct ErrorCollector {
    std::vector<std::tuple<int, juce::String, juce::String>> errors;

    std::function<void(int, juce::String, juce::String)> callback() {
        return [this](int line, juce::String file, juce::String msg) {
            if (line >= 0)
                errors.push_back({line, file, msg});
            else
                errors.clear();
        };
    }

    void clear() { errors.clear(); }
    bool hasErrors() const { return !errors.empty(); }
};

// ============================================================================
// Correctness Tests
// ============================================================================

class LuaCorrectnessTest : public juce::UnitTest {
public:
    LuaCorrectnessTest() : juce::UnitTest("Lua Correctness", "Lua") {}

    void initialise() override {
        LuaParser::onPrint = [](const std::string&) {};
        LuaParser::onClear = []() {};
    }

    void runTest() override {
        testBasicExecution();
        testReturnValues();
        testGlobalVariables();
        testSliderAccess();
        testMidiVariables();
        testDawTransport();
        testEnvelopeVariables();
        testEffectModeVariables();
        testExternalInputVariables();
        testPhaseIncrementation();
        testOsciCircle();
        testOsciLine();
        testOsciRotate();
        testOsciTranslate();
        testOsciScale();
        testOsciMix();
        testOsciLerp();
        testOsciClamp();
        testOsciMap();
        testOsciNormalize();
        testOsciDot();
        testOsciCross();
        testWaveforms();
        testOsciChain();
        testOsciPolygon();
        testOsciLissajous();
        testOsciSmoothstep();
        testSyntaxError();
        testInfiniteLoopProtection();
        testMultipleStates();
        testScriptStatePreservation();
        testPrintAllTypes();
        testResetClearsState();
        testResetMultipleStates();
        testRapidResetBetweenRuns();
        testResetPreservesScriptCorrectness();
        testResetStressWithManyCycles();
    }

private:
    std::vector<float> runOnce(const juce::String& script, LuaVariables& vars) {
        ErrorCollector errors;
        LuaParser parser("test.lua", script, errors.callback());
        lua_State* L = nullptr;
        auto lr = parser.run(L, vars);
        parser.close(L);
        return std::vector<float>(lr.values, lr.values + lr.count);
    }

    std::vector<float> runOnce(const juce::String& script) {
        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        return runOnce(script, vars);
    }

    // ------------------------------------------------------------------

    void testBasicExecution() {
        beginTest("Basic script returns table");
        auto result = runOnce("return {1.0, 2.0}");
        expectEquals((int)result.size(), 2);
        expectWithinAbsoluteError(result[0], 1.0f, 0.001f);
        expectWithinAbsoluteError(result[1], 2.0f, 0.001f);
    }

    void testReturnValues() {
        beginTest("3D return values");
        auto result = runOnce("return {1.0, 2.0, 3.0}");
        expectEquals((int)result.size(), 3);
        expectWithinAbsoluteError(result[2], 3.0f, 0.001f);

        beginTest("6-element return (with color)");
        auto result6 = runOnce("return {0.1, 0.2, 0.3, 0.4, 0.5, 0.6}");
        expectEquals((int)result6.size(), 6);
        expectWithinAbsoluteError(result6[3], 0.4f, 0.001f);
        expectWithinAbsoluteError(result6[5], 0.6f, 0.001f);
    }

    void testGlobalVariables() {
        beginTest("Core globals: step, phase, sample_rate, frequency, cycle_count");
        LuaVariables vars;
        vars.step = 42;
        vars.phase = 1.5;
        vars.sampleRate = 48000;
        vars.frequency = 220;
        vars.cycle = 7;

        auto result = runOnce("return {step, phase, sample_rate, frequency, cycle_count}", vars);
        expectEquals((int)result.size(), 5);
        expectWithinAbsoluteError(result[0], 42.0f, 0.001f);
        expectWithinAbsoluteError(result[1], 1.5f, 0.001f);
        expectWithinAbsoluteError(result[2], 48000.0f, 0.5f);
        expectWithinAbsoluteError(result[3], 220.0f, 0.001f);
        expectWithinAbsoluteError(result[4], 7.0f, 0.001f);
    }

    void testSliderAccess() {
        beginTest("Slider globals (a, b, z)");
        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        vars.sliders[0] = 0.25;
        vars.sliders[1] = 0.75;
        vars.sliders[25] = 1.0;

        auto result = runOnce("return {slider_a, slider_b, slider_z}", vars);
        expectEquals((int)result.size(), 3);
        expectWithinAbsoluteError(result[0], 0.25f, 0.001f);
        expectWithinAbsoluteError(result[1], 0.75f, 0.001f);
        expectWithinAbsoluteError(result[2], 1.0f, 0.001f);
    }

    void testMidiVariables() {
        beginTest("MIDI context variables");
        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        vars.midiNote = 72;
        vars.velocity = 0.8;
        vars.voiceIndex = 3;
        vars.noteOn = true;

        auto result = runOnce("return {midi_note, velocity, voice_index, note_on and 1 or 0}", vars);
        expectEquals((int)result.size(), 4);
        expectWithinAbsoluteError(result[0], 72.0f, 0.001f);
        expectWithinAbsoluteError(result[1], 0.8f, 0.001f);
        expectWithinAbsoluteError(result[2], 3.0f, 0.001f);
        expectWithinAbsoluteError(result[3], 1.0f, 0.001f);
    }

    void testDawTransport() {
        beginTest("DAW transport variables");
        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        vars.bpm = 140.0;
        vars.playTime = 5.5;
        vars.playTimeBeats = 12.0;
        vars.isPlaying = true;
        vars.timeSigNumerator = 3;
        vars.timeSigDenominator = 8;

        auto result = runOnce(
            "return {bpm, play_time, play_time_beats, is_playing and 1 or 0, time_sig_num, time_sig_den}",
            vars);
        expectEquals((int)result.size(), 6);
        expectWithinAbsoluteError(result[0], 140.0f, 0.01f);
        expectWithinAbsoluteError(result[1], 5.5f, 0.001f);
        expectWithinAbsoluteError(result[2], 12.0f, 0.001f);
        expectWithinAbsoluteError(result[3], 1.0f, 0.001f);
        expectWithinAbsoluteError(result[4], 3.0f, 0.001f);
        expectWithinAbsoluteError(result[5], 8.0f, 0.001f);
    }

    void testEnvelopeVariables() {
        beginTest("Envelope variables");
        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        vars.envelope = 0.65;
        vars.envelopeStage = 3;

        auto result = runOnce("return {envelope, envelope_stage}", vars);
        expectEquals((int)result.size(), 2);
        expectWithinAbsoluteError(result[0], 0.65f, 0.001f);
        expectWithinAbsoluteError(result[1], 3.0f, 0.001f);
    }

    void testEffectModeVariables() {
        beginTest("Effect mode (x, y, z)");
        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        vars.isEffect = true;
        vars.x = 0.3;
        vars.y = 0.7;
        vars.z = 0.9;

        auto result = runOnce("return {x, y, z}", vars);
        expectEquals((int)result.size(), 3);
        expectWithinAbsoluteError(result[0], 0.3f, 0.001f);
        expectWithinAbsoluteError(result[1], 0.7f, 0.001f);
        expectWithinAbsoluteError(result[2], 0.9f, 0.001f);
    }

    void testExternalInputVariables() {
        beginTest("External input (ext_x, ext_y)");
        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        vars.ext_x = 0.123;
        vars.ext_y = 0.456;

        auto result = runOnce("return {ext_x, ext_y}", vars);
        expectEquals((int)result.size(), 2);
        expectWithinAbsoluteError(result[0], 0.123f, 0.001f);
        expectWithinAbsoluteError(result[1], 0.456f, 0.001f);
    }

    void testPhaseIncrementation() {
        beginTest("Phase and step increment after run()");
        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        vars.step = 1;
        vars.phase = 0;

        ErrorCollector errors;
        LuaParser parser("test.lua", "return {step, phase}", errors.callback());
        lua_State* L = nullptr;

        auto result1 = parser.run(L, vars);
        expectWithinAbsoluteError(result1.values[0], 1.0f, 0.001f);
        // After run(), step and phase are incremented
        expect(vars.step > 1, "step should advance");
        expect(vars.phase > 0, "phase should advance");

        auto result2 = parser.run(L, vars);
        // Second call sees the incremented values
        expect(result2.values[0] > result1.values[0], "step should be larger on second call");

        parser.close(L);
    }

    void testOsciCircle() {
        beginTest("osci_circle at phase=0 returns (0.8, 0)");
        auto result = runOnce("return osci_circle(0)");
        expectEquals((int)result.size(), 2);
        // CircleArc(0,0,0.8,0.8,0,2pi).nextVector(0) = (0.8*cos(0), 0.8*sin(0))
        expectWithinAbsoluteError(result[0], 0.8f, 0.01f);
        expectWithinAbsoluteError(result[1], 0.0f, 0.01f);

        beginTest("osci_circle at phase=pi/2 returns (0, 0.8)");
        auto result2 = runOnce("return osci_circle(math.pi / 2)");
        // t = (pi/2)/(2pi) = 0.25, angle = 2pi*0.25 = pi/2
        expectWithinAbsoluteError(result2[0], 0.0f, 0.01f);
        expectWithinAbsoluteError(result2[1], 0.8f, 0.01f);

        beginTest("osci_circle custom radius");
        auto result3 = runOnce("return osci_circle(0, 0.5)");
        expectWithinAbsoluteError(result3[0], 0.5f, 0.01f);
        expectWithinAbsoluteError(result3[1], 0.0f, 0.01f);
    }

    void testOsciLine() {
        beginTest("osci_line default at phase=0 returns start point");
        auto result = runOnce("return osci_line(0)");
        // Default line from (-1,-1) to (1,1), t = 0/(2pi) = 0
        expectEquals((int)result.size(), 3);
        expectWithinAbsoluteError(result[0], -1.0f, 0.01f);
        expectWithinAbsoluteError(result[1], -1.0f, 0.01f);

        beginTest("osci_line custom endpoints");
        auto result2 = runOnce("return osci_line(math.pi, {0,0,0}, {2,4,0})");
        // t = pi/(2pi) = 0.5, lerp(0->2, 0->4) at 0.5 = (1, 2)
        expectWithinAbsoluteError(result2[0], 1.0f, 0.01f);
        expectWithinAbsoluteError(result2[1], 2.0f, 0.01f);
    }

    void testOsciRotate() {
        beginTest("osci_rotate {1,0} by pi/2 gives approximately {0,1}");
        auto result = runOnce("return osci_rotate({1, 0}, math.pi / 2)");
        expectEquals((int)result.size(), 2);
        expectWithinAbsoluteError(result[0], 0.0f, 0.01f);
        expectWithinAbsoluteError(result[1], 1.0f, 0.01f);

        beginTest("osci_rotate {1,0} by pi gives approximately {-1,0}");
        auto result2 = runOnce("return osci_rotate({1, 0}, math.pi)");
        expectWithinAbsoluteError(result2[0], -1.0f, 0.01f);
        expectWithinAbsoluteError(result2[1], 0.0f, 0.01f);
    }

    void testOsciTranslate() {
        beginTest("osci_translate adds offset");
        auto result = runOnce("return osci_translate({1, 2, 3}, {0.5, 0.5, 0.5})");
        expectEquals((int)result.size(), 3);
        expectWithinAbsoluteError(result[0], 1.5f, 0.01f);
        expectWithinAbsoluteError(result[1], 2.5f, 0.01f);
        expectWithinAbsoluteError(result[2], 3.5f, 0.01f);
    }

    void testOsciScale() {
        beginTest("osci_scale multiplies components");
        auto result = runOnce("return osci_scale({1, 2, 3}, {2, 3, 4})");
        expectEquals((int)result.size(), 3);
        expectWithinAbsoluteError(result[0], 2.0f, 0.01f);
        expectWithinAbsoluteError(result[1], 6.0f, 0.01f);
        expectWithinAbsoluteError(result[2], 12.0f, 0.01f);
    }

    void testOsciMix() {
        beginTest("osci_mix at weight 0 returns first point");
        auto r0 = runOnce("return osci_mix({1,2,3}, {4,5,6}, 0)");
        expectWithinAbsoluteError(r0[0], 1.0f, 0.01f);
        expectWithinAbsoluteError(r0[1], 2.0f, 0.01f);

        beginTest("osci_mix at weight 1 returns second point");
        auto r1 = runOnce("return osci_mix({1,2,3}, {4,5,6}, 1)");
        expectWithinAbsoluteError(r1[0], 4.0f, 0.01f);
        expectWithinAbsoluteError(r1[1], 5.0f, 0.01f);

        beginTest("osci_mix at weight 0.5 returns midpoint");
        auto r05 = runOnce("return osci_mix({0,0,0}, {2,4,6}, 0.5)");
        expectWithinAbsoluteError(r05[0], 1.0f, 0.01f);
        expectWithinAbsoluteError(r05[1], 2.0f, 0.01f);
        expectWithinAbsoluteError(r05[2], 3.0f, 0.01f);
    }

    void testOsciLerp() {
        beginTest("osci_lerp");
        auto r1 = runOnce("return {osci_lerp(0, 10, 0.3)}");
        expectWithinAbsoluteError(r1[0], 3.0f, 0.01f);

        auto r2 = runOnce("return {osci_lerp(0, 10, 0)}");
        expectWithinAbsoluteError(r2[0], 0.0f, 0.01f);

        auto r3 = runOnce("return {osci_lerp(0, 10, 1)}");
        expectWithinAbsoluteError(r3[0], 10.0f, 0.01f);
    }

    void testOsciClamp() {
        beginTest("osci_clamp above range");
        auto r1 = runOnce("return {osci_clamp(5, 0, 1)}");
        expectWithinAbsoluteError(r1[0], 1.0f, 0.001f);

        beginTest("osci_clamp below range");
        auto r2 = runOnce("return {osci_clamp(-5, 0, 1)}");
        expectWithinAbsoluteError(r2[0], 0.0f, 0.001f);

        beginTest("osci_clamp within range");
        auto r3 = runOnce("return {osci_clamp(0.5, 0, 1)}");
        expectWithinAbsoluteError(r3[0], 0.5f, 0.001f);
    }

    void testOsciMap() {
        beginTest("osci_map range mapping");
        auto result = runOnce("return {osci_map(5, 0, 10, 0, 100)}");
        expectWithinAbsoluteError(result[0], 50.0f, 0.01f);

        beginTest("osci_map inverted range");
        auto r2 = runOnce("return {osci_map(0, 0, 10, 100, 0)}");
        expectWithinAbsoluteError(r2[0], 100.0f, 0.01f);
    }

    void testOsciNormalize() {
        beginTest("osci_normalize 3-4-5 triangle");
        auto result = runOnce("return osci_normalize({3, 0, 4})");
        expectEquals((int)result.size(), 3);
        expectWithinAbsoluteError(result[0], 0.6f, 0.01f);
        expectWithinAbsoluteError(result[1], 0.0f, 0.01f);
        expectWithinAbsoluteError(result[2], 0.8f, 0.01f);

        beginTest("osci_normalize zero vector");
        auto r2 = runOnce("return osci_normalize({0, 0, 0})");
        expectWithinAbsoluteError(r2[0], 0.0f, 0.01f);
    }

    void testOsciDot() {
        beginTest("osci_dot product");
        auto result = runOnce("return {osci_dot({1,2,3}, {4,5,6})}");
        // 1*4 + 2*5 + 3*6 = 32
        expectWithinAbsoluteError(result[0], 32.0f, 0.01f);

        beginTest("osci_dot perpendicular vectors");
        auto r2 = runOnce("return {osci_dot({1,0,0}, {0,1,0})}");
        expectWithinAbsoluteError(r2[0], 0.0f, 0.01f);
    }

    void testOsciCross() {
        beginTest("osci_cross product i x j = k");
        auto result = runOnce("return osci_cross({1,0,0}, {0,1,0})");
        expectEquals((int)result.size(), 3);
        expectWithinAbsoluteError(result[0], 0.0f, 0.01f);
        expectWithinAbsoluteError(result[1], 0.0f, 0.01f);
        expectWithinAbsoluteError(result[2], 1.0f, 0.01f);

        beginTest("osci_cross product j x i = -k");
        auto r2 = runOnce("return osci_cross({0,1,0}, {1,0,0})");
        expectWithinAbsoluteError(r2[2], -1.0f, 0.01f);
    }

    void testWaveforms() {
        beginTest("osci_square_wave at phase=0 returns 1");
        auto r1 = runOnce("return {osci_square_wave(0)}");
        expectWithinAbsoluteError(r1[0], 1.0f, 0.01f);

        beginTest("osci_square_wave at phase=pi returns 0");
        auto r2 = runOnce("return {osci_square_wave(math.pi)}");
        expectWithinAbsoluteError(r2[0], 0.0f, 0.01f);

        beginTest("osci_saw_wave at phase=0 returns 0");
        auto r3 = runOnce("return {osci_saw_wave(0)}");
        expectWithinAbsoluteError(r3[0], 0.0f, 0.01f);

        beginTest("osci_saw_wave at phase=pi returns 0.5");
        auto r4 = runOnce("return {osci_saw_wave(math.pi)}");
        expectWithinAbsoluteError(r4[0], 0.5f, 0.01f);

        beginTest("osci_triangle_wave at phase=0 returns 1");
        // t=0, |2*0 - 1| = 1
        auto r5 = runOnce("return {osci_triangle_wave(0)}");
        expectWithinAbsoluteError(r5[0], 1.0f, 0.01f);

        beginTest("osci_triangle_wave at phase=pi returns 0");
        // t=0.5, |2*0.5 - 1| = 0
        auto r6 = runOnce("return {osci_triangle_wave(math.pi)}");
        expectWithinAbsoluteError(r6[0], 0.0f, 0.01f);

        beginTest("osci_pulse_wave with duty cycle");
        auto r7 = runOnce("return {osci_pulse_wave(0, 0.25)}");
        expectWithinAbsoluteError(r7[0], 1.0f, 0.01f);
        auto r8 = runOnce("return {osci_pulse_wave(math.pi, 0.25)}");
        expectWithinAbsoluteError(r8[0], 0.0f, 0.01f);
    }

    void testOsciChain() {
        beginTest("osci_chain selects first segment at phase=0");
        auto result = runOnce(
            "return osci_chain(0, {\n"
            "  function(p) return {1, 0} end,\n"
            "  function(p) return {0, 1} end\n"
            "})");
        expectEquals((int)result.size(), 2);
        expectWithinAbsoluteError(result[0], 1.0f, 0.01f);
        expectWithinAbsoluteError(result[1], 0.0f, 0.01f);

        beginTest("osci_chain selects second segment at phase=pi");
        auto r2 = runOnce(
            "return osci_chain(math.pi, {\n"
            "  function(p) return {1, 0} end,\n"
            "  function(p) return {0, 1} end\n"
            "})");
        expectWithinAbsoluteError(r2[0], 0.0f, 0.01f);
        expectWithinAbsoluteError(r2[1], 1.0f, 0.01f);
    }

    void testOsciPolygon() {
        beginTest("osci_polygon default (pentagon)");
        auto result = runOnce("return osci_polygon(0)");
        expectEquals((int)result.size(), 2);
        // Just verify it returns something sensible (finite values)
        expect(std::isfinite(result[0]), "x should be finite");
        expect(std::isfinite(result[1]), "y should be finite");

        beginTest("osci_polygon triangle (n=3)");
        auto r2 = runOnce("return osci_polygon(0, 3)");
        expectEquals((int)r2.size(), 2);
        expect(std::isfinite(r2[0]) && std::isfinite(r2[1]), "values should be finite");
    }

    void testOsciLissajous() {
        beginTest("osci_lissajous at phase=0");
        auto result = runOnce("return osci_lissajous(0)");
        expectEquals((int)result.size(), 2);
        // Default: x = sin(1*0) = 0, y = cos(5*0 + 0) = 1
        expectWithinAbsoluteError(result[0], 0.0f, 0.01f);
        expectWithinAbsoluteError(result[1], 1.0f, 0.01f);
    }

    void testOsciSmoothstep() {
        beginTest("osci_smoothstep at edges");
        auto r1 = runOnce("return {osci_smoothstep(0, 1, 0)}");
        expectWithinAbsoluteError(r1[0], 0.0f, 0.01f);
        auto r2 = runOnce("return {osci_smoothstep(0, 1, 1)}");
        expectWithinAbsoluteError(r2[0], 1.0f, 0.01f);

        beginTest("osci_smoothstep at midpoint");
        auto r3 = runOnce("return {osci_smoothstep(0, 1, 0.5)}");
        expectWithinAbsoluteError(r3[0], 0.5f, 0.01f);
    }

    void testSyntaxError() {
        beginTest("Syntax error triggers fallback");
        ErrorCollector errors;
        LuaParser parser("test.lua", "return {{{INVALID", errors.callback());
        lua_State* L = nullptr;
        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        auto result = parser.run(L, vars);
        expectEquals(result.count, 2);
        expectWithinAbsoluteError(result.values[0], 0.0f, 0.001f);
        expectWithinAbsoluteError(result.values[1], 0.0f, 0.001f);
        parser.close(L);
    }

    void testInfiniteLoopProtection() {
        beginTest("Infinite loop is caught by instruction limit");
        ErrorCollector errors;
        // Use jit.off() to disable the JIT compiler for this chunk.
        // LuaJIT's JIT compiles tight loops to native code, bypassing the
        // VM count hook that LuaParser relies on for infinite-loop detection.
        // In the interpreter, the hook fires as expected.
        LuaParser parser("test.lua",
            "jit.off()\nwhile true do end\nreturn {0,0}",
            errors.callback());
        lua_State* L = nullptr;
        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;

        // First call: instruction limit fires, returns empty
        auto result = parser.run(L, vars);
        expectEquals(result.count, 0);

        // Second call: parser has reverted to fallback script "return {0.0, 0.0}"
        auto result2 = parser.run(L, vars);
        expectEquals(result2.count, 2);

        parser.close(L);
    }

    void testMultipleStates() {
        beginTest("Multiple lua_States get independent initialization");
        ErrorCollector errors;
        LuaParser parser("test.lua", "return {42, 0}", errors.callback());

        lua_State* L1 = nullptr;
        lua_State* L2 = nullptr;
        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;

        auto result1 = parser.run(L1, vars);
        auto result2 = parser.run(L2, vars);

        expectEquals(result1.count, 2);
        expectWithinAbsoluteError(result1.values[0], 42.0f, 0.001f);
        expectEquals(result2.count, 2);
        expectWithinAbsoluteError(result2.values[0], 42.0f, 0.001f);

        expect(L1 != L2, "States should be different pointers");

        parser.close(L1);
        parser.close(L2);
    }

    void testScriptStatePreservation() {
        beginTest("Script-side state persists across calls");
        ErrorCollector errors;
        LuaParser parser("test.lua",
            "if counter == nil then counter = 0 end\n"
            "counter = counter + 1\n"
            "return {counter, 0}",
            errors.callback());

        lua_State* L = nullptr;
        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;

        auto r1 = parser.run(L, vars);
        expectWithinAbsoluteError(r1.values[0], 1.0f, 0.001f);

        auto r2 = parser.run(L, vars);
        expectWithinAbsoluteError(r2.values[0], 2.0f, 0.001f);

        auto r3 = parser.run(L, vars);
        expectWithinAbsoluteError(r3.values[0], 3.0f, 0.001f);

        parser.close(L);
    }

    void testPrintAllTypes() {
        beginTest("print outputs correct strings for all Lua types");

        std::vector<std::string> printed;
        auto oldPrint = LuaParser::onPrint;
        LuaParser::onPrint = [&](const std::string& s) { printed.push_back(s); };

        ErrorCollector errors;
        LuaParser parser("test.lua",
            "print(nil)\n"                                   // [0] "nil"
            "print(true)\n"                                  // [1] "true"
            "print(false)\n"                                 // [2] "false"
            "print(42)\n"                                    // [3] "42"
            "print('hello')\n"                               // [4] "hello"
            "print(print)\n"                                 // [5] "function: 0x..."
            "local t = {}; print(t)\n"                       // [6] "table: 0x..."
            "local mt = setmetatable({}, {__tostring = function() return 'custom' end})\n"
            "print(mt)\n"                                    // [7] "custom"
            "return {0, 0}",
            errors.callback());

        lua_State* L = nullptr;
        auto vars = makeDefaultVars();
        parser.run(L, vars);
        parser.close(L);

        LuaParser::onPrint = oldPrint;

        expect(!errors.hasErrors(), "Script should not produce errors");
        expect(printed.size() >= 8, "Expected at least 8 print outputs, got " + juce::String((int)printed.size()));

        if (printed.size() >= 8) {
            expectEquals(juce::String(printed[0]), juce::String("nil"));
            expectEquals(juce::String(printed[1]), juce::String("true"));
            expectEquals(juce::String(printed[2]), juce::String("false"));
            expectEquals(juce::String(printed[3]), juce::String("42"));
            expectEquals(juce::String(printed[4]), juce::String("hello"));
            expect(juce::String(printed[5]).startsWith("function: 0x"),
                   "Expected function address, got: " + juce::String(printed[5]));
            expect(juce::String(printed[6]).startsWith("table: 0x"),
                   "Expected table address, got: " + juce::String(printed[6]));
            expectEquals(juce::String(printed[7]), juce::String("custom"));
        }
    }

    // ------------------------------------------------------------------
    // Reset / forgetAllStates stress tests
    // ------------------------------------------------------------------

    static constexpr const char* COUNTER_SCRIPT =
        "if counter == nil then counter = 0 end\n"
        "counter = counter + 1\n"
        "return {counter, 0}";

    LuaVariables makeDefaultVars() {
        LuaVariables vars;
        vars.sampleRate = 44100;
        vars.frequency = 440;
        return vars;
    }

    void testResetClearsState() {
        beginTest("forgetAllStates resets accumulated script state");
        ErrorCollector errors;
        LuaParser parser("test.lua", COUNTER_SCRIPT, errors.callback());

        lua_State* L = nullptr;
        auto vars = makeDefaultVars();

        // Run 5 times to accumulate state
        for (int i = 0; i < 5; i++)
            parser.run(L, vars);

        auto r5 = parser.run(L, vars);
        expectWithinAbsoluteError(r5.values[0], 6.0f, 0.001f);

        // Reset and verify counter starts over
        parser.forgetAllStates();
        auto r6 = parser.run(L, vars);
        expectWithinAbsoluteError(r6.values[0], 1.0f, 0.001f);

        parser.close(L);
    }

    void testResetMultipleStates() {
        beginTest("forgetAllStates resets all independent states");
        ErrorCollector errors;
        LuaParser parser("test.lua", COUNTER_SCRIPT, errors.callback());

        lua_State* L1 = nullptr;
        lua_State* L2 = nullptr;
        lua_State* L3 = nullptr;
        auto vars = makeDefaultVars();

        // Accumulate state in 3 independent lua_States
        for (int i = 0; i < 10; i++) {
            parser.run(L1, vars);
            parser.run(L2, vars);
            parser.run(L3, vars);
        }

        auto r1 = parser.run(L1, vars);
        auto r2 = parser.run(L2, vars);
        auto r3 = parser.run(L3, vars);
        expectWithinAbsoluteError(r1.values[0], 11.0f, 0.001f);
        expectWithinAbsoluteError(r2.values[0], 11.0f, 0.001f);
        expectWithinAbsoluteError(r3.values[0], 11.0f, 0.001f);

        // Reset all states at once
        parser.forgetAllStates();

        auto rr1 = parser.run(L1, vars);
        auto rr2 = parser.run(L2, vars);
        auto rr3 = parser.run(L3, vars);
        expectWithinAbsoluteError(rr1.values[0], 1.0f, 0.001f);
        expectWithinAbsoluteError(rr2.values[0], 1.0f, 0.001f);
        expectWithinAbsoluteError(rr3.values[0], 1.0f, 0.001f);

        parser.close(L1);
        parser.close(L2);
        parser.close(L3);
    }

    void testRapidResetBetweenRuns() {
        beginTest("Rapid successive resets don't corrupt state");
        ErrorCollector errors;
        LuaParser parser("test.lua", COUNTER_SCRIPT, errors.callback());

        lua_State* L = nullptr;
        auto vars = makeDefaultVars();

        // Repeatedly: accumulate a few runs, reset, verify clean start
        for (int cycle = 0; cycle < 50; cycle++) {
            // Run a few iterations
            for (int i = 0; i < 5; i++)
                parser.run(L, vars);

            auto pre = parser.run(L, vars);
            expectWithinAbsoluteError(pre.values[0], 6.0f, 0.001f);

            // Multiple rapid resets (only the first matters, rest are no-ops)
            parser.forgetAllStates();
            parser.forgetAllStates();
            parser.forgetAllStates();

            auto post = parser.run(L, vars);
            expectWithinAbsoluteError(post.values[0], 1.0f, 0.001f);

            // Reset again so next cycle starts clean
            parser.forgetAllStates();
        }

        parser.close(L);
    }

    void testResetPreservesScriptCorrectness() {
        beginTest("Reset preserves script execution correctness");
        ErrorCollector errors;
        LuaParser parser("test.lua",
            "if acc == nil then acc = 0 end\n"
            "acc = acc + step\n"
            "return {acc, math.sin(phase * math.pi * 2)}",
            errors.callback());

        lua_State* L = nullptr;
        auto vars = makeDefaultVars();

        // Run and accumulate
        for (int i = 0; i < 100; i++) {
            vars.step = i;
            vars.phase = i * 0.01;
            parser.run(L, vars);
        }

        // Reset
        parser.forgetAllStates();

        // Verify script still works correctly after reset
        vars.step = 5;
        vars.phase = 0.25;
        auto r1 = parser.run(L, vars);
        expectWithinAbsoluteError(r1.values[0], 5.0f, 0.001f);
        expectWithinAbsoluteError(r1.values[1], 1.0f, 0.001f); // sin(0.25 * 2pi) = 1

        vars.step = 3;
        auto r2 = parser.run(L, vars);
        expectWithinAbsoluteError(r2.values[0], 8.0f, 0.001f); // 5 + 3

        parser.close(L);
        expect(!errors.hasErrors(), "No errors expected after reset");
    }

    void testResetStressWithManyCycles() {
        beginTest("Stress: many run/reset cycles with multiple states");
        ErrorCollector errors;
        LuaParser parser("test.lua", COUNTER_SCRIPT, errors.callback());

        constexpr int NUM_STATES = 4;
        lua_State* states[NUM_STATES] = {};
        auto vars = makeDefaultVars();

        for (int cycle = 0; cycle < 100; cycle++) {
            // Run each state a varying number of times
            int runsPerState = (cycle % 7) + 1;
            LuaResult lastResults[NUM_STATES] = {};
            for (int r = 0; r < runsPerState; r++) {
                for (int s = 0; s < NUM_STATES; s++) {
                    lastResults[s] = parser.run(states[s], vars);
                }
            }

            // Verify accumulated count matches the number of runs
            for (int s = 0; s < NUM_STATES; s++) {
                expectWithinAbsoluteError(lastResults[s].values[0], (float)runsPerState, 0.001f);
            }

            // Reset all states
            parser.forgetAllStates();

            // Verify all states restart from 1
            for (int s = 0; s < NUM_STATES; s++) {
                auto result = parser.run(states[s], vars);
                expectWithinAbsoluteError(result.values[0], 1.0f, 0.001f);
            }

            // Reset again to clean for next cycle
            parser.forgetAllStates();
        }

        for (int s = 0; s < NUM_STATES; s++)
            parser.close(states[s]);

        expect(!errors.hasErrors(), "No errors expected during stress test");
    }
};

static LuaCorrectnessTest luaCorrectnessTest;
