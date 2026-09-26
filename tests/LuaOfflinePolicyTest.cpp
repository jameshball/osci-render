#include <JuceHeader.h>
#include <osci_scripting/osci_scripting.h>
#include <thread>
#include <chrono>

namespace {
struct OfflineLuaErrors {
    juce::String message;
    auto callback() {
        return [this](int line, juce::String, juce::String text) {
            if (line >= 0) { message = text; }
            else { message.clear(); }
        };
    }
};
LuaVariables offlineVariables() {
    LuaVariables vars;
    vars.sampleRate = 48000;
    vars.frequency = 60;
    return vars;
}
}

class LuaOfflinePolicyTest : public juce::UnitTest {
public:
    LuaOfflinePolicyTest() : juce::UnitTest("Lua Offline Policy", "Lua") {}
    void runTest() override {
        beginTest("Offline policy is opt-in; default libraries remain available");
        OfflineLuaErrors defaults;
        LuaParser unrestricted("default.lua", "return {type(io)=='table' and 1 or 0, type(os)=='table' and 1 or 0, type(require)=='function' and 1 or 0}", defaults.callback());
        LuaState sharedState;
        auto vars = offlineVariables();
        auto result = unrestricted.run(sharedState, vars);
        expectEquals(result.count, 3);
        for (int index = 0; index < result.count; ++index) { expectEquals(result.values[index], 1.0f); }

        beginTest("Offline library restrictions and uncatchable execution limits");
        OfflineLuaErrors errors;
        LuaParser restricted("offline.lua", R"(
            for _, name in ipairs({'io','os','package','debug','ffi','jit','require','dofile','loadfile','load','loadstring',
                'coroutine','pcall','xpcall','newproxy','print','clear'}) do
                if _G[name] ~= nil then error(name .. ' remained available') end
            end
            return {1, math.sin(0), osci_lerp(2,4,0.5)}
        )", errors.callback());
        expect(restricted.setOfflinePolicy({}).wasOk());
        result = restricted.run(sharedState, vars);
        expect(errors.message.isEmpty(), errors.message);
        expectEquals(result.count, 3);
        expectEquals(result.values[0], 1.0f);
        expectEquals(result.values[2], 3.0f);
        // Switching back restores the native allocator before destroying its VM.
        result = unrestricted.run(sharedState, vars);
        expectEquals(result.count, 3);
        expectEquals(result.values[0], 1.0f);

        beginTest("Fresh offline states produce identical seeded random sequences");
        OfflineLuaErrors errorsA, errorsB;
        const juce::String script = "counter=(counter or 0)+1; return {math.random(),math.random(-100,100),counter}";
        LuaParser a("a.lua", script, errorsA.callback()), b("b.lua", script, errorsB.callback());
        LuaParser::OfflinePolicy policy;
        policy.randomSeed = 1234;
        expect(a.setOfflinePolicy(policy).wasOk() && b.setOfflinePolicy(policy).wasOk());
        LuaState stateA, stateB;
        auto varsA = offlineVariables(), varsB = offlineVariables();
        for (int iteration = 0; iteration < 100; ++iteration) {
            const auto first = a.run(stateA, varsA), second = b.run(stateB, varsB);
            expectEquals(first.count, 3);
            expectEquals(second.count, 3);
            for (int index = 0; index < first.count; ++index) { expectEquals(first.values[index], second.values[index]); }
            expectEquals(first.values[2], static_cast<float>(iteration + 1));
        }

        beginTest("Syntax/runtime failures never execute a fallback");
        for (const auto& badScript : {juce::String("return {"), juce::String("error('failure')"),
                juce::String("return {os.time()}"), juce::String("return {slider_missing * 2}")}) {
            OfflineLuaErrors failure;
            LuaParser parser("invalid.lua", badScript, failure.callback(), "return {99,99}");
            expect(parser.setOfflinePolicy({}).wasOk());
            LuaState state;
            auto variables = offlineVariables();
            expectEquals(parser.run(state, variables).count, 0);
            expect(failure.message.isNotEmpty(), "Offline errors must not be suppressed");
            expect(!parser.isFunctionValid());
            expectEquals(parser.run(state, variables).count, 0);
        }

        beginTest("Offline results are dense numeric arrays without coercion or truncation");
        for (const auto* badResult : {"return {false, true}", "return {'1','2'}", "return {{},1}",
                "return {1,2,3,0,0,0,7}", "return {[1]=1,[2]=2,[1000]=3}", "return {[1]=1,[3]=3}",
                "return {1,2,extra=3}", "return {}", "return {0/0,1}", "return {1e300,1}",
                "return {1,2},{3,4}", "return 1,2"}) {
            OfflineLuaErrors failure;
            LuaParser parser("result.lua", badResult, failure.callback());
            expect(parser.setOfflinePolicy({}).wasOk());
            LuaState state;
            auto variables = offlineVariables();
            expectEquals(parser.run(state, variables).count, 0);
            expect(failure.message.isNotEmpty(), badResult);
            expect(!parser.isFunctionValid());
        }
        OfflineLuaErrors permissiveErrors;
        LuaParser permissive("render.lua", "return {'1',false,3,4,5,6,7}", permissiveErrors.callback());
        LuaState permissiveState;
        result = permissive.run(permissiveState, vars);
        expectEquals(result.count, 6);
        expectEquals(result.values[0], 1.0f);
        expectEquals(result.values[1], 0.0f);

        beginTest("Infinite loops stop at the per-run instruction budget");
        OfflineLuaErrors loopErrors;
        LuaParser loop("loop.lua", "while true do end", loopErrors.callback());
        policy.instructionBudget = 2048;
        expect(loop.setOfflinePolicy(policy).wasOk());
        LuaState loopState;
        expectEquals(loop.run(loopState, vars).count, 0);
        expect(loopErrors.message.containsIgnoreCase("instruction budget"), loopErrors.message);

        beginTest("Cancellation is checked before and during execution");
        std::atomic<bool> cancelled {true};
        policy.cancelled = &cancelled;
        OfflineLuaErrors cancelErrors;
        LuaParser cancellable("cancel.lua", "while true do end", cancelErrors.callback());
        expect(cancellable.setOfflinePolicy(policy).wasOk());
        LuaState cancelState;
        expectEquals(cancellable.run(cancelState, vars).count, 0);
        expect(cancelErrors.message.containsIgnoreCase("cancel"));
        cancelled.store(false);
        policy.instructionBudget = 1000000000;
        expect(cancellable.setOfflinePolicy(policy).wasOk());
        std::thread cancellation([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            cancelled.store(true);
        });
        result = cancellable.run(cancelState, vars);
        cancellation.join();
        expectEquals(result.count, 0);
        expect(cancelErrors.message.containsIgnoreCase("cancel"), cancelErrors.message);

        beginTest("Native large allocation and persistent heap growth are bounded");
        policy = {};
        policy.memoryLimitBytes = 128 * 1024;
        OfflineLuaErrors memoryErrors;
        LuaParser large("large.lua", "return {#string.rep('x', 1024 * 1024)}", memoryErrors.callback());
        expect(large.setOfflinePolicy(policy).wasOk());
        LuaState memoryState;
        expectEquals(large.run(memoryState, vars).count, 0);
        expect(memoryErrors.message.containsIgnoreCase("memory"), memoryErrors.message);
        memoryErrors.message.clear();
        LuaParser accumulated("accumulate.lua", "saved=saved or {}; saved[#saved+1]=string.rep('x',8192)..step; return {1,2}", memoryErrors.callback());
        expect(accumulated.setOfflinePolicy(policy).wasOk());
        LuaState accumulatedState;
        vars = offlineVariables();
        for (int iteration = 0; iteration < 64 && memoryErrors.message.isEmpty(); ++iteration) { accumulated.run(accumulatedState, vars); }
        expect(memoryErrors.message.containsIgnoreCase("memory"), memoryErrors.message);
        accumulatedState.reset();
        expectEquals(accumulated.run(accumulatedState, vars).count, 2);

        beginTest("Invalid policies fail before changing execution mode");
        policy.instructionBudget = 0;
        expect(large.setOfflinePolicy(policy).failed());
        policy.instructionBudget = 1000;
        policy.memoryLimitBytes = 1;
        expect(large.setOfflinePolicy(policy).failed());
    }
};
static LuaOfflinePolicyTest luaOfflinePolicyTest;
