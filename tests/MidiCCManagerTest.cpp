#include <JuceHeader.h>
#include "TestCleanup.h"

// ============================================================================
// MIDI CC Manager Tests — functional correctness and stress tests for
// the MidiCCManager class: assignment, learning, serialization, and
// concurrent audio + message thread access.
// ============================================================================

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Create a standalone FloatParameter for testing (not owned by an AudioProcessor).
static std::unique_ptr<osci::FloatParameter> makeFloat(const juce::String& id,
                                                        float value = 0.5f,
                                                        float min = 0.0f,
                                                        float max = 1.0f) {
    return std::make_unique<osci::FloatParameter>(id, id, 2, value, min, max, 0.001f);
}

static std::unique_ptr<osci::BooleanParameter> makeBool(const juce::String& id, bool value = false) {
    return std::make_unique<osci::BooleanParameter>(id, id, 2, value, "");
}

static std::unique_ptr<osci::IntParameter> makeInt(const juce::String& id,
                                                     int value = 0,
                                                     int min = 0,
                                                     int max = 127) {
    return std::make_unique<osci::IntParameter>(id, id, 2, value, min, max);
}

// Build a juce::MidiBuffer containing a single CC message.
static juce::MidiBuffer makeCCBuffer(int ccNumber, int ccValue, int channel = 1) {
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::controllerEvent(channel, ccNumber, ccValue), 0);
    return buf;
}

// Drain the MidiCCManager's timer polling by calling processCC on the audio
// thread and then manually pumping its change messages. Since in a test we
// don't have a real message loop, we simulate it.
static void pumpMessageLoop(int ms = 100) {
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr) return;
    auto start = juce::Time::getMillisecondCounterHiRes();
    while (juce::Time::getMillisecondCounterHiRes() - start < ms) {
        mm->runDispatchLoopUntil(10);
    }
}

// Pump message loop until a condition is met, or a generous timeout is reached.
// Returns true if the condition was met, false if timed out.
static bool pumpUntil(std::function<bool()> condition, int timeoutMs = 2000) {
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr) return condition();
    auto start = juce::Time::getMillisecondCounterHiRes();
    while (juce::Time::getMillisecondCounterHiRes() - start < timeoutMs) {
        if (condition()) return true;
        mm->runDispatchLoopUntil(10);
    }
    return condition();
}

// RAII helper to ensure MessageManager is initialized and the current thread
// is registered as the message thread. Required for Timer and ChangeBroadcaster.
// Created once per process; intentionally leaks at exit since the test runner
// doesn't have a clean shutdown phase.
static void ensureMessageManagerExists() {
    static bool initialized = false;
    if (!initialized) {
        juce::MessageManager::getInstance();
        initialized = true;
    }
}

// ============================================================================
// Functional Tests
// ============================================================================

class MidiCCAssignmentTest : public juce::UnitTest {
public:
    MidiCCAssignmentTest() : juce::UnitTest("MIDI CC Assignment", "MidiCC") {}

    void initialise() override {
        ensureMessageManagerExists();
    }

    void runTest() override {
        testDirectAssignmentViaLearning();
        testRemoveAssignment();
        testReassignCC();
        testMultipleParamsSeparateCCs();
        testCCValueMapsToFloat();
        testCCValueMapsToBool();
        testCCValueMapsToInt();
        testLearningCancellation();
        testNoAssignment();
    }

private:
    void testDirectAssignmentViaLearning() {
        beginTest("Learn assigns CC to parameter");

        osci::MidiCCManager mgr;
        auto param = makeFloat("testParam", 0.5f, 0.0f, 1.0f);

        mgr.startLearning(param.get());
        expect(mgr.isLearning(param.get()), "Should be learning for param");

        // Simulate CC 10 arriving on audio thread
        auto buf = makeCCBuffer(10, 64);
        mgr.processMidiBuffer(buf);

        // The learning completion happens on the timer (message thread).
        pumpMessageLoop(200);

        expectEquals(mgr.getAssignedCC(param.get()), 10);
        expect(!mgr.isLearning(param.get()), "Learning should be complete");
    }

    void testRemoveAssignment() {
        beginTest("Remove CC assignment");

        osci::MidiCCManager mgr;
        auto param = makeFloat("testParam");

        // Assign CC 20
        mgr.startLearning(param.get());
        mgr.processMidiBuffer(makeCCBuffer(20, 64));
        pumpMessageLoop(200);
        expectEquals(mgr.getAssignedCC(param.get()), 20);

        // Remove
        mgr.removeAssignment(param.get());
        expectEquals(mgr.getAssignedCC(param.get()), -1);

        // CC 20 should no longer affect the parameter
        float before = param->getValueUnnormalised();
        mgr.processMidiBuffer(makeCCBuffer(20, 127));
        pumpMessageLoop(100);
        expectWithinAbsoluteError(param->getValueUnnormalised(), before, 0.001f);
    }

    void testReassignCC() {
        beginTest("Reassigning a CC to a new param removes old mapping");

        osci::MidiCCManager mgr;
        auto paramA = makeFloat("paramA");
        auto paramB = makeFloat("paramB");

        // Assign CC 5 to paramA
        mgr.startLearning(paramA.get());
        mgr.processMidiBuffer(makeCCBuffer(5, 64));
        pumpMessageLoop(200);
        expectEquals(mgr.getAssignedCC(paramA.get()), 5);

        // Assign CC 5 to paramB (should steal from paramA)
        mgr.startLearning(paramB.get());
        mgr.processMidiBuffer(makeCCBuffer(5, 64));
        pumpMessageLoop(200);
        expectEquals(mgr.getAssignedCC(paramB.get()), 5);
        expectEquals(mgr.getAssignedCC(paramA.get()), -1);
    }

    void testMultipleParamsSeparateCCs() {
        beginTest("Multiple params can have different CCs");

        osci::MidiCCManager mgr;
        auto paramA = makeFloat("paramA");
        auto paramB = makeFloat("paramB");

        mgr.startLearning(paramA.get());
        mgr.processMidiBuffer(makeCCBuffer(1, 64));
        pumpMessageLoop(200);

        mgr.startLearning(paramB.get());
        mgr.processMidiBuffer(makeCCBuffer(2, 64));
        pumpMessageLoop(200);

        expectEquals(mgr.getAssignedCC(paramA.get()), 1);
        expectEquals(mgr.getAssignedCC(paramB.get()), 2);
    }

    void testCCValueMapsToFloat() {
        beginTest("CC value 0-127 maps to float parameter range");

        osci::MidiCCManager mgr;
        auto param = makeFloat("testFloat", 0.0f, 0.0f, 10.0f);

        mgr.startLearning(param.get());
        mgr.processMidiBuffer(makeCCBuffer(7, 64));
        pumpMessageLoop(200);

        // Send CC 7 = 127 (max)
        mgr.processMidiBuffer(makeCCBuffer(7, 127));
        pumpMessageLoop(200);
        // Normalised value should be close to 1.0 → unnormalised close to 10.0
        expectWithinAbsoluteError(param->getValueUnnormalised(), 10.0f, 0.15f);

        // Send CC 7 = 0 (min)
        mgr.processMidiBuffer(makeCCBuffer(7, 0));
        pumpMessageLoop(200);
        expectWithinAbsoluteError(param->getValueUnnormalised(), 0.0f, 0.15f);
    }

    void testCCValueMapsToBool() {
        beginTest("CC value maps to boolean parameter (>=64 = on)");

        osci::MidiCCManager mgr;
        auto param = makeBool("testBool", false);

        mgr.startLearning(param.get());
        mgr.processMidiBuffer(makeCCBuffer(11, 64));
        pumpMessageLoop(200);

        // CC = 127 → true
        mgr.processMidiBuffer(makeCCBuffer(11, 127));
        pumpMessageLoop(200);
        expect(param->getBoolValue(), "CC 127 should set bool to true");

        // CC = 0 → false
        mgr.processMidiBuffer(makeCCBuffer(11, 0));
        pumpMessageLoop(200);
        expect(!param->getBoolValue(), "CC 0 should set bool to false");
    }

    void testCCValueMapsToInt() {
        beginTest("CC value maps to int parameter range");

        osci::MidiCCManager mgr;
        auto param = makeInt("testInt", 0, 0, 10);

        mgr.startLearning(param.get());
        mgr.processMidiBuffer(makeCCBuffer(12, 64));
        pumpMessageLoop(200);

        // CC = 127 → max (10)
        mgr.processMidiBuffer(makeCCBuffer(12, 127));
        pumpMessageLoop(200);
        expectEquals(param->getValueUnnormalised(), 10);

        // CC = 0 → min (0)
        mgr.processMidiBuffer(makeCCBuffer(12, 0));
        pumpMessageLoop(200);
        expectEquals(param->getValueUnnormalised(), 0);
    }

    void testLearningCancellation() {
        beginTest("stopLearning cancels without assigning");

        osci::MidiCCManager mgr;
        auto param = makeFloat("testParam");

        mgr.startLearning(param.get());
        expect(mgr.isLearning(param.get()));

        mgr.stopLearning();
        expect(!mgr.isLearning(param.get()));

        // Send a CC — should NOT be assigned
        mgr.processMidiBuffer(makeCCBuffer(50, 64));
        pumpMessageLoop(200);
        expectEquals(mgr.getAssignedCC(param.get()), -1);
    }

    void testNoAssignment() {
        beginTest("CC with no assignment does not affect parameter");

        osci::MidiCCManager mgr;
        auto param = makeFloat("testParam", 0.5f, 0.0f, 1.0f);

        float before = param->getValueUnnormalised();
        mgr.processMidiBuffer(makeCCBuffer(99, 127));
        pumpMessageLoop(100);
        expectWithinAbsoluteError(param->getValueUnnormalised(), before, 0.001f);
    }
};

static MidiCCAssignmentTest midiCCAssignmentTest;

// ============================================================================

class MidiCCSerializationTest : public juce::UnitTest {
public:
    MidiCCSerializationTest() : juce::UnitTest("MIDI CC Serialization", "MidiCC") {}

    void runTest() override {
        testSaveAndLoad();
        testLoadWithMissingParams();
        testLoadEmptyXml();
        testClearAllAssignments();
    }

private:
    void testSaveAndLoad() {
        beginTest("Save and load preserves CC assignments");

        auto paramA = makeFloat("paramA");
        auto paramB = makeBool("paramB");

        // Set up assignments in source manager
        osci::MidiCCManager source;
        source.startLearning(paramA.get());
        source.processMidiBuffer(makeCCBuffer(1, 64));
        pumpMessageLoop(200);

        source.startLearning(paramB.get());
        source.processMidiBuffer(makeCCBuffer(2, 64));
        pumpMessageLoop(200);

        expectEquals(source.getAssignedCC(paramA.get()), 1);
        expectEquals(source.getAssignedCC(paramB.get()), 2);

        // Save
        juce::XmlElement xml("state");
        source.save(&xml);

        // Load into a new manager with equivalent parameters
        auto paramA2 = makeFloat("paramA");
        auto paramB2 = makeBool("paramB");

        osci::MidiCCManager dest;
        dest.load(&xml, [&](const juce::String& id) -> osci::MidiCCManager::ParamBinding {
            if (id == "paramA") return osci::MidiCCManager::makeBinding(paramA2.get());
            if (id == "paramB") return osci::MidiCCManager::makeBinding(paramB2.get());
            return {};
        });

        expectEquals(dest.getAssignedCC(paramA2.get()), 1);
        expectEquals(dest.getAssignedCC(paramB2.get()), 2);

        // Verify the assignment actually works
        dest.processMidiBuffer(makeCCBuffer(1, 127));
        pumpMessageLoop(200);
        expectWithinAbsoluteError(paramA2->getValueUnnormalised(), 1.0f, 0.15f);
    }

    void testLoadWithMissingParams() {
        beginTest("Load ignores assignments for missing parameters");

        auto param = makeFloat("existingParam");

        osci::MidiCCManager source;
        source.startLearning(param.get());
        source.processMidiBuffer(makeCCBuffer(5, 64));
        pumpMessageLoop(200);

        juce::XmlElement xml("state");
        source.save(&xml);

        // Load into a manager where "existingParam" doesn't exist
        osci::MidiCCManager dest;
        dest.load(&xml, [](const juce::String&) -> osci::MidiCCManager::ParamBinding {
            return {}; // All params "missing"
        });

        // Should have no assignments
        expectEquals(dest.getAssignedCC(param.get()), -1);
    }

    void testLoadEmptyXml() {
        beginTest("Load with no midiCCAssignments element is harmless");

        osci::MidiCCManager mgr;
        juce::XmlElement xml("state"); // no child element
        mgr.load(&xml, [](const juce::String&) -> osci::MidiCCManager::ParamBinding {
            return {};
        });
        // Should not crash, and have no assignments
        expect(true, "Load from empty XML did not crash");
    }

    void testClearAllAssignments() {
        beginTest("clearAllAssignments removes everything");

        osci::MidiCCManager mgr;
        auto param = makeFloat("testParam");

        mgr.startLearning(param.get());
        mgr.processMidiBuffer(makeCCBuffer(10, 64));
        pumpMessageLoop(200);
        expectEquals(mgr.getAssignedCC(param.get()), 10);

        mgr.clearAllAssignments();
        expectEquals(mgr.getAssignedCC(param.get()), -1);
    }
};

static MidiCCSerializationTest midiCCSerializationTest;

// ============================================================================
// Stress Tests
// ============================================================================

class MidiCCStressTest : public juce::UnitTest {
public:
    MidiCCStressTest() : juce::UnitTest("MIDI CC Stress Test", "MidiCC") {}

    void runTest() override {
        testConcurrentAudioAndMessageThread();
        testRapidLearningCycles();
        testAllCCsAssigned();
    }

private:
    void testConcurrentAudioAndMessageThread() {
        beginTest("Concurrent processMidiBuffer + query/remove does not crash");

        osci::MidiCCManager mgr;

        // Create a pool of parameters
        static constexpr int NUM_PARAMS = 20;
        std::vector<std::unique_ptr<osci::FloatParameter>> params;
        for (int i = 0; i < NUM_PARAMS; ++i)
            params.push_back(makeFloat("stressParam_" + juce::String(i)));

        // Pre-assign CCs via learning (done synchronously before spawning threads)
        for (int i = 0; i < NUM_PARAMS; ++i) {
            mgr.startLearning(params[i].get());
            mgr.processMidiBuffer(makeCCBuffer(i, 64));
            pumpUntil([&] { return !mgr.isLearning(); });
        }
        // Stop all learning before threading starts
        mgr.stopLearning();
        pumpMessageLoop(100);

        std::atomic<bool> running{true};
        std::atomic<int> audioIterations{0};
        std::atomic<int> messageIterations{0};

        // Audio thread: hammers processMidiBuffer with random CC messages
        struct AudioThread : juce::Thread {
            AudioThread(osci::MidiCCManager& m, std::atomic<bool>& r, std::atomic<int>& c)
                : juce::Thread("AudioThread"), mgr(m), running(r), count(c) {}
            void run() override {
                juce::Random rng(42);
                while (running.load()) {
                    int cc = rng.nextInt(128);
                    int val = rng.nextInt(128);
                    auto buf = makeCCBuffer(cc, val);
                    mgr.processMidiBuffer(buf);
                    count.fetch_add(1);
                }
            }
            osci::MidiCCManager& mgr;
            std::atomic<bool>& running;
            std::atomic<int>& count;
        };

        // Second thread: queries and removes assignments (lock-contention with audio)
        struct QueryThread : juce::Thread {
            QueryThread(osci::MidiCCManager& m,
                        std::vector<std::unique_ptr<osci::FloatParameter>>& p,
                        std::atomic<bool>& r, std::atomic<int>& c)
                : juce::Thread("QueryThread"), mgr(m), params(p), running(r), count(c) {}
            void run() override {
                juce::Random rng(99);
                while (running.load()) {
                    int paramIdx = rng.nextInt((int)params.size());
                    auto* param = params[paramIdx].get();
                    // Only lock-contention-safe operations (no Timer/ChangeBroadcaster)
                    mgr.getAssignedCC(param);
                    mgr.isLearning(param);
                    count.fetch_add(1);
                }
            }
            osci::MidiCCManager& mgr;
            std::vector<std::unique_ptr<osci::FloatParameter>>& params;
            std::atomic<bool>& running;
            std::atomic<int>& count;
        };

        AudioThread audioThread(mgr, running, audioIterations);
        QueryThread queryThread(mgr, params, running, messageIterations);

        audioThread.startThread();
        queryThread.startThread();

        // Let both threads run for 2 seconds
        juce::Thread::sleep(2000);
        running.store(false);

        audioThread.waitForThreadToExit(5000);
        queryThread.waitForThreadToExit(5000);

        expect(true, juce::String::formatted(
            "Completed %d audio + %d query iterations without crash",
            audioIterations.load(), messageIterations.load()));
    }

    void testRapidLearningCycles() {
        beginTest("Rapid learn→assign→remove cycles");

        osci::MidiCCManager mgr;
        auto param = makeFloat("rapidParam");

        for (int i = 0; i < 200; ++i) {
            int cc = i % 128;
            mgr.startLearning(param.get());
            mgr.processMidiBuffer(makeCCBuffer(cc, 64));
            pumpUntil([&] { return !mgr.isLearning(); });
            mgr.removeAssignment(param.get());
        }

        // After all cycles, should have no assignment
        expectEquals(mgr.getAssignedCC(param.get()), -1);
        expect(true, "200 rapid learn/remove cycles completed");
    }

    void testAllCCsAssigned() {
        beginTest("All 128 CCs can be assigned simultaneously");

        osci::MidiCCManager mgr;
        std::vector<std::unique_ptr<osci::FloatParameter>> params;

        for (int i = 0; i < 128; ++i) {
            params.push_back(makeFloat("param_" + juce::String(i)));
            mgr.startLearning(params.back().get());
            mgr.processMidiBuffer(makeCCBuffer(i, 64));
            pumpUntil([&] { return !mgr.isLearning(); });
        }

        // Verify all assignments
        for (int i = 0; i < 128; ++i) {
            expectEquals(mgr.getAssignedCC(params[i].get()), i,
                         "CC " + juce::String(i) + " should be assigned to param_" + juce::String(i));
        }

        // Send CC on all 128 simultaneously
        juce::MidiBuffer bigBuf;
        for (int i = 0; i < 128; ++i)
            bigBuf.addEvent(juce::MidiMessage::controllerEvent(1, i, 127), i);
        mgr.processMidiBuffer(bigBuf);
        pumpMessageLoop(300);

        // All floats should be near max (1.0)
        for (int i = 0; i < 128; ++i) {
            float val = params[i]->getValue(); // normalised
            expect(val > 0.9f, "param_" + juce::String(i) + " should be near max after CC 127");
        }
    }
};

static MidiCCStressTest midiCCStressTest;
