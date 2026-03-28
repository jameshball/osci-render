#include <JuceHeader.h>
#include "../Source/audio/modulation/LfoState.h"

// ============================================================================
// LFO Stress Tests — focus on data races, deadlocks, and thread-safety of
// the global LFO system's lock-guarded containers
// ============================================================================

// ---------------------------------------------------------------------------
// Helpers — lightweight substitutes so we don't need the full processor
// ---------------------------------------------------------------------------

// Mimics the processor's LFO assignment storage and locking pattern exactly.
// This lets us stress-test the lock/container interactions in isolation.
class LfoAssignmentStore {
public:
    void addAssignment(const LfoAssignment& assignment) {
        juce::SpinLock::ScopedLockType lock(spinLock);
        for (auto& a : assignments) {
            if (a.sourceIndex == assignment.sourceIndex && a.paramId == assignment.paramId) {
                a.depth = assignment.depth;
                a.bipolar = assignment.bipolar;
                return;
            }
        }
        assignments.push_back(assignment);
    }

    void removeAssignment(int lfoIndex, const juce::String& paramId) {
        juce::SpinLock::ScopedLockType lock(spinLock);
        assignments.erase(
            std::remove_if(assignments.begin(), assignments.end(),
                [&](const LfoAssignment& a) { return a.sourceIndex == lfoIndex && a.paramId == paramId; }),
            assignments.end());
    }

    std::vector<LfoAssignment> getAssignments() const {
        juce::SpinLock::ScopedLockType lock(spinLock);
        return assignments;
    }

    int size() const {
        juce::SpinLock::ScopedLockType lock(spinLock);
        return (int)assignments.size();
    }

private:
    std::vector<LfoAssignment> assignments;
    mutable juce::SpinLock spinLock;
};

// Mimics the processor's LFO waveform storage and locking pattern.
class LfoWaveformStore {
public:
    void setWaveform(int index, const LfoWaveform& wf) {
        juce::SpinLock::ScopedLockType lock(spinLock);
        if (index >= 0 && index < NUM_LFOS)
            waveforms[index] = wf;
    }

    LfoWaveform getWaveform(int index) const {
        juce::SpinLock::ScopedLockType lock(spinLock);
        if (index >= 0 && index < NUM_LFOS)
            return waveforms[index];
        return {};
    }

private:
    LfoWaveform waveforms[NUM_LFOS];
    mutable juce::SpinLock spinLock;
};

// ============================================================================
// Test 1: Concurrent assignment add/remove/read (simulates GUI drag-drop +
// audio-thread iteration happening simultaneously)
// ============================================================================

class LfoAssignmentRaceTest : public juce::UnitTest {
public:
    LfoAssignmentRaceTest() : juce::UnitTest("LFO Assignment Race Test", "LFO") {}

    void runTest() override {
        beginTest("Concurrent add/remove/read does not crash or corrupt");

        LfoAssignmentStore store;
        std::atomic<bool> running{true};
        std::atomic<int> readCount{0};
        std::atomic<int> writeCount{0};

        // Simulate 4 "GUI threads" adding/removing assignments rapidly
        std::array<std::unique_ptr<juce::Thread>, 4> writers;
        for (int w = 0; w < 4; ++w) {
            struct Writer : juce::Thread {
                Writer(LfoAssignmentStore& s, std::atomic<bool>& r, std::atomic<int>& wc, int id)
                    : juce::Thread("Writer" + juce::String(id)), store(s), running(r), writeCount(wc), writerId(id) {}
                void run() override {
                    juce::Random rng(writerId * 12345);
                    while (running.load()) {
                        int lfo = rng.nextInt(NUM_LFOS);
                        juce::String param = "param_" + juce::String(rng.nextInt(20));
                        float depth = rng.nextFloat() * 2.0f - 1.0f;
                        bool bipolar = rng.nextBool();

                        if (rng.nextFloat() < 0.7f)
                            store.addAssignment({lfo, param, depth, bipolar});
                        else
                            store.removeAssignment(lfo, param);

                        writeCount.fetch_add(1);
                    }
                }
                LfoAssignmentStore& store;
                std::atomic<bool>& running;
                std::atomic<int>& writeCount;
                int writerId;
            };
            writers[w] = std::make_unique<Writer>(store, running, writeCount, w);
            writers[w]->startThread();
        }

        // Simulate 2 "audio threads" reading the full assignment list rapidly
        std::array<std::unique_ptr<juce::Thread>, 2> readers;
        for (int r = 0; r < 2; ++r) {
            struct Reader : juce::Thread {
                Reader(LfoAssignmentStore& s, std::atomic<bool>& run, std::atomic<int>& rc, int id)
                    : juce::Thread("Reader" + juce::String(id)), store(s), running(run), readCount(rc) {}
                void run() override {
                    while (running.load()) {
                        auto assignments = store.getAssignments();
                        // Iterate the snapshot — must not crash even as underlying data changes
                        float sum = 0.0f;
                        for (const auto& a : assignments)
                            sum += a.depth;
                        (void)sum;
                        readCount.fetch_add(1);
                    }
                }
                LfoAssignmentStore& store;
                std::atomic<bool>& running;
                std::atomic<int>& readCount;
            };
            readers[r] = std::make_unique<Reader>(store, running, readCount, r);
            readers[r]->startThread();
        }

        // Let them hammer for 2 seconds
        juce::Thread::sleep(2000);
        running.store(false);

        for (auto& w : writers) w->waitForThreadToExit(5000);
        for (auto& r : readers) r->waitForThreadToExit(5000);

        logMessage("  Writes: " + juce::String(writeCount.load()) + ", Reads: " + juce::String(readCount.load()));
        expect(writeCount.load() > 0, "Writers should have completed some operations");
        expect(readCount.load() > 0, "Readers should have completed some operations");

        // Store should be in a consistent state
        auto final_assignments = store.getAssignments();
        for (const auto& a : final_assignments) {
            expect(a.sourceIndex >= 0 && a.sourceIndex < NUM_LFOS, "LFO index in valid range");
            expect(a.depth >= -1.0f && a.depth <= 1.0f, "Depth in valid range");
        }
    }
};

// ============================================================================
// Test 2: Concurrent waveform mutation + evaluation (simulates GUI editing
// LFO shape while audio thread advances through the waveform)
// ============================================================================

class LfoWaveformRaceTest : public juce::UnitTest {
public:
    LfoWaveformRaceTest() : juce::UnitTest("LFO Waveform Race Test", "LFO") {}

    void runTest() override {
        beginTest("Concurrent waveform set/evaluate does not crash or corrupt");

        LfoWaveformStore store;
        std::atomic<bool> running{true};
        std::atomic<int> evalCount{0};
        std::atomic<int> setCount{0};

        // Initialize with triangle preset
        for (int i = 0; i < NUM_LFOS; ++i)
            store.setWaveform(i, createLfoPreset(LfoPreset::Triangle));

        // Writer threads: swap presets randomly (simulates user changing LFO shape)
        std::array<std::unique_ptr<juce::Thread>, 2> writers;
        for (int w = 0; w < 2; ++w) {
            struct Writer : juce::Thread {
                Writer(LfoWaveformStore& s, std::atomic<bool>& r, std::atomic<int>& sc, int id)
                    : juce::Thread("WfWriter" + juce::String(id)), store(s), running(r), setCount(sc), writerId(id) {}
                void run() override {
                    static const LfoPreset presets[] = { LfoPreset::Sine, LfoPreset::Triangle, LfoPreset::Sawtooth, LfoPreset::Square };
                    juce::Random rng(writerId * 54321);
                    while (running.load()) {
                        int lfo = rng.nextInt(NUM_LFOS);
                        auto preset = presets[rng.nextInt(4)];
                        store.setWaveform(lfo, createLfoPreset(preset));
                        setCount.fetch_add(1);
                    }
                }
                LfoWaveformStore& store;
                std::atomic<bool>& running;
                std::atomic<int>& setCount;
                int writerId;
            };
            writers[w] = std::make_unique<Writer>(store, running, setCount, w);
            writers[w]->startThread();
        }

        // Reader threads: evaluate waveform at random phases (simulates audio thread)
        std::array<std::unique_ptr<juce::Thread>, 4> readers;
        std::atomic<bool> anyOutOfRange{false};
        for (int r = 0; r < 4; ++r) {
            struct Reader : juce::Thread {
                Reader(LfoWaveformStore& s, std::atomic<bool>& run, std::atomic<int>& ec, std::atomic<bool>& oor, int id)
                    : juce::Thread("WfReader" + juce::String(id)), store(s), running(run), evalCount(ec), outOfRange(oor) {}
                void run() override {
                    juce::Random rng(42);
                    while (running.load()) {
                        int lfo = rng.nextInt(NUM_LFOS);
                        auto wf = store.getWaveform(lfo);
                        float phase = rng.nextFloat();
                        float val = wf.evaluate(phase);
                        if (val < -0.01f || val > 1.01f)
                            outOfRange.store(true);
                        evalCount.fetch_add(1);
                    }
                }
                LfoWaveformStore& store;
                std::atomic<bool>& running;
                std::atomic<int>& evalCount;
                std::atomic<bool>& outOfRange;
            };
            readers[r] = std::make_unique<Reader>(store, running, evalCount, anyOutOfRange, r);
            readers[r]->startThread();
        }

        juce::Thread::sleep(2000);
        running.store(false);

        for (auto& w : writers) w->waitForThreadToExit(5000);
        for (auto& r : readers) r->waitForThreadToExit(5000);

        logMessage("  Waveform sets: " + juce::String(setCount.load()) + ", Evaluations: " + juce::String(evalCount.load()));
        expect(!anyOutOfRange.load(), "Waveform evaluation should always return values in [0, 1]");
        expect(evalCount.load() > 1000, "Should have completed many evaluations");
    }
};

// ============================================================================
// Test 3: LfoAudioState phase advance under concurrent waveform changes
// (simulates the exact audio-thread processBlock pattern)
// ============================================================================

class LfoAudioStateStressTest : public juce::UnitTest {
public:
    LfoAudioStateStressTest() : juce::UnitTest("LFO AudioState Stress Test", "LFO") {}

    void runTest() override {
        beginTest("Phase advancement produces valid output under rapid waveform swaps");

        LfoWaveformStore wfStore;
        for (int i = 0; i < NUM_LFOS; ++i)
            wfStore.setWaveform(i, createLfoPreset(LfoPreset::Sine));

        std::atomic<bool> running{true};
        std::atomic<int> blockCount{0};
        std::atomic<bool> anyBadValue{false};

        // Audio thread simulation: advance LFOs block-by-block
        struct AudioThread : juce::Thread {
            AudioThread(LfoWaveformStore& ws, std::atomic<bool>& r, std::atomic<int>& bc, std::atomic<bool>& bad)
                : juce::Thread("AudioSim"), wfStore(ws), running(r), blockCount(bc), anyBadValue(bad) {}
            void run() override {
                LfoAudioState states[NUM_LFOS];
                std::array<std::vector<float>, NUM_LFOS> blockBuf;
                for (auto& buf : blockBuf) buf.resize(512);
                float sampleRate = 48000.0f;

                while (running.load()) {
                    for (int l = 0; l < NUM_LFOS; ++l) {
                        auto wf = wfStore.getWaveform(l);
                        float rate = 2.0f + (float)l;
                        states[l].advanceBlock(blockBuf[l].data(), 512, rate, sampleRate, wf, LfoMode::Free, 0.0f);
                        for (int s = 0; s < 512; ++s) {
                            if (blockBuf[l][(size_t)s] < -0.01f || blockBuf[l][(size_t)s] > 1.01f)
                                anyBadValue.store(true);
                        }
                    }
                    blockCount.fetch_add(1);
                }
            }
            LfoWaveformStore& wfStore;
            std::atomic<bool>& running;
            std::atomic<int>& blockCount;
            std::atomic<bool>& anyBadValue;
        };

        auto audioThread = std::make_unique<AudioThread>(wfStore, running, blockCount, anyBadValue);
        audioThread->startThread(juce::Thread::Priority::highest);

        // GUI thread simulation: swap waveforms rapidly
        struct GuiThread : juce::Thread {
            GuiThread(LfoWaveformStore& ws, std::atomic<bool>& r)
                : juce::Thread("GuiSim"), wfStore(ws), running(r) {}
            void run() override {
                static const LfoPreset presets[] = {
                    LfoPreset::Sine, LfoPreset::Triangle, LfoPreset::Sawtooth, LfoPreset::Square
                };
                juce::Random rng(99);
                while (running.load()) {
                    int lfo = rng.nextInt(NUM_LFOS);
                    auto wf = createLfoPreset(presets[rng.nextInt(4)]);
                    // Occasionally add/remove nodes to stress the node vector
                    if (rng.nextFloat() < 0.2f && wf.nodes.size() > 2) {
                        double t = rng.nextFloat();
                        double v = rng.nextFloat();
                        wf.nodes.insert(wf.nodes.begin() + 1, {t, v, rng.nextFloat() * 2.0f - 1.0f});
                    }
                    wfStore.setWaveform(lfo, wf);
                }
            }
            LfoWaveformStore& wfStore;
            std::atomic<bool>& running;
        };

        auto guiThread = std::make_unique<GuiThread>(wfStore, running);
        guiThread->startThread();

        juce::Thread::sleep(3000);
        running.store(false);

        audioThread->waitForThreadToExit(5000);
        guiThread->waitForThreadToExit(5000);

        logMessage("  Audio blocks processed: " + juce::String(blockCount.load()));
        expect(!anyBadValue.load(), "All LFO output values should be in [0, 1]");
        expect(blockCount.load() > 100, "Should have processed many audio blocks");
    }
};

// ============================================================================
// Test 4: addLfoAssignment order stability under concurrent depth updates
// (regression test for the reordering bug that was fixed)
// ============================================================================

class LfoAssignmentOrderTest : public juce::UnitTest {
public:
    LfoAssignmentOrderTest() : juce::UnitTest("LFO Assignment Order Stability", "LFO") {}

    void runTest() override {
        beginTest("Updating depth preserves insertion order");

        LfoAssignmentStore store;

        // Create 10 assignments in a known order
        const int N = 10;
        for (int i = 0; i < N; ++i)
            store.addAssignment({0, "param_" + juce::String(i), (float)i / N, false});

        // Verify initial order
        auto initial = store.getAssignments();
        expectEquals((int)initial.size(), N);
        for (int i = 0; i < N; ++i)
            expectEquals(initial[(size_t)i].paramId, juce::String("param_" + juce::String(i)));

        // Now update depths many times from multiple threads
        std::atomic<bool> running{true};
        std::array<std::unique_ptr<juce::Thread>, 4> updaters;
        for (int t = 0; t < 4; ++t) {
            struct Updater : juce::Thread {
                Updater(LfoAssignmentStore& s, std::atomic<bool>& r, int id)
                    : juce::Thread("Updater" + juce::String(id)), store(s), running(r), threadId(id) {}
                void run() override {
                    juce::Random rng(threadId * 777);
                    while (running.load()) {
                        int param = rng.nextInt(10);
                        float newDepth = rng.nextFloat() * 2.0f - 1.0f;
                        store.addAssignment({0, "param_" + juce::String(param), newDepth, rng.nextBool()});
                    }
                }
                LfoAssignmentStore& store;
                std::atomic<bool>& running;
                int threadId;
            };
            updaters[t] = std::make_unique<Updater>(store, running, t);
            updaters[t]->startThread();
        }

        juce::Thread::sleep(1000);
        running.store(false);
        for (auto& u : updaters) u->waitForThreadToExit(5000);

        // Order must be preserved: param_0, param_1, ..., param_9
        auto final_assignments = store.getAssignments();
        expectEquals((int)final_assignments.size(), N, "No duplicates should be created");
        for (int i = 0; i < N; ++i) {
            expectEquals(final_assignments[(size_t)i].paramId, juce::String("param_" + juce::String(i)),
                         "Order must be preserved after concurrent depth updates");
        }
    }
};

// ============================================================================
// Test 5: Simulated mixed audio/GUI workload (the "real world" stress test)
// Runs all the patterns simultaneously: assignment add/remove, waveform
// changes, LFO evaluation, and atomic value reads
// ============================================================================

class LfoFullSystemStressTest : public juce::UnitTest {
public:
    LfoFullSystemStressTest() : juce::UnitTest("LFO Full System Stress Test", "LFO") {}

    void runTest() override {
        beginTest("Mixed audio + GUI workload sustains without deadlock for 5 seconds");

        LfoAssignmentStore assignmentStore;
        LfoWaveformStore waveformStore;
        std::atomic<float> lfoCurrentValues[NUM_LFOS] = {};
        std::atomic<bool> running{true};
        std::atomic<bool> deadlockDetected{false};

        // Initialize
        for (int i = 0; i < NUM_LFOS; ++i)
            waveformStore.setWaveform(i, createLfoPreset(LfoPreset::Triangle));

        // --- Audio thread: advance LFOs, read assignments, apply modulation ---
        struct AudioThread : juce::Thread {
            AudioThread(LfoAssignmentStore& as, LfoWaveformStore& ws,
                        std::atomic<float>* vals, std::atomic<bool>& r)
                : juce::Thread("AudioStress"), assignStore(as), wfStore(ws),
                  currentValues(vals), running(r) {}
            void run() override {
                LfoAudioState states[NUM_LFOS];
                const int blockSize = 256;
                float sampleRate = 44100.0f;
                int blocksProcessed = 0;

                while (running.load()) {
                    // Advance LFOs
                    for (int l = 0; l < NUM_LFOS; ++l) {
                        auto wf = wfStore.getWaveform(l);
                        std::vector<float> buf((size_t)blockSize);
                        states[l].advanceBlock(buf.data(), blockSize, 2.0f, sampleRate, wf, LfoMode::Free, 0.0f);
                        currentValues[l].store(buf[(size_t)(blockSize - 1)], std::memory_order_relaxed);
                    }

                    // Read assignments and do fake modulation work
                    auto assignments = assignStore.getAssignments();
                    float accumulator = 0.0f;
                    for (const auto& a : assignments) {
                        float lfoVal = currentValues[a.sourceIndex].load(std::memory_order_relaxed);
                        if (a.bipolar)
                            accumulator += (lfoVal * 2.0f - 1.0f) * a.depth * 0.5f;
                        else
                            accumulator += lfoVal * a.depth;
                    }
                    (void)accumulator; // prevent optimization
                    blocksProcessed++;
                }
            }
            LfoAssignmentStore& assignStore;
            LfoWaveformStore& wfStore;
            std::atomic<float>* currentValues;
            std::atomic<bool>& running;
        };

        // --- GUI thread 1: add/remove/update assignments ---
        struct GuiAssignmentThread : juce::Thread {
            GuiAssignmentThread(LfoAssignmentStore& s, std::atomic<bool>& r)
                : juce::Thread("GuiAssign"), store(s), running(r) {}
            void run() override {
                juce::Random rng(42);
                while (running.load()) {
                    float roll = rng.nextFloat();
                    if (roll < 0.5f) {
                        store.addAssignment({
                            rng.nextInt(NUM_LFOS),
                            "effect_" + juce::String(rng.nextInt(10)),
                            rng.nextFloat() * 2.0f - 1.0f,
                            rng.nextBool()
                        });
                    } else if (roll < 0.8f) {
                        store.removeAssignment(rng.nextInt(NUM_LFOS),
                                               "effect_" + juce::String(rng.nextInt(10)));
                    } else {
                        // Read and iterate (simulates timer callback)
                        auto assignments = store.getAssignments();
                        for (const auto& a : assignments)
                            (void)a.depth;
                    }
                }
            }
            LfoAssignmentStore& store;
            std::atomic<bool>& running;
        };

        // --- GUI thread 2: swap waveforms ---
        struct GuiWaveformThread : juce::Thread {
            GuiWaveformThread(LfoWaveformStore& s, std::atomic<bool>& r)
                : juce::Thread("GuiWaveform"), store(s), running(r) {}
            void run() override {
                static const LfoPreset presets[] = {
                    LfoPreset::Sine, LfoPreset::Triangle, LfoPreset::Sawtooth, LfoPreset::Square
                };
                juce::Random rng(77);
                while (running.load()) {
                    store.setWaveform(rng.nextInt(NUM_LFOS), createLfoPreset(presets[rng.nextInt(4)]));
                    juce::Thread::sleep(1); // ~1ms between changes like a real user
                }
            }
            LfoWaveformStore& store;
            std::atomic<bool>& running;
        };

        // --- GUI thread 3: read LFO current values (simulates EffectComponent timer) ---
        struct GuiValueReaderThread : juce::Thread {
            GuiValueReaderThread(std::atomic<float>* vals, LfoAssignmentStore& as, std::atomic<bool>& r)
                : juce::Thread("GuiValueReader"), currentValues(vals), assignStore(as), running(r) {}
            void run() override {
                while (running.load()) {
                    auto assignments = assignStore.getAssignments();
                    for (const auto& a : assignments) {
                        float val = currentValues[a.sourceIndex].load(std::memory_order_relaxed);
                        (void)val;
                    }
                    juce::Thread::sleep(16); // ~60fps
                }
            }
            std::atomic<float>* currentValues;
            LfoAssignmentStore& assignStore;
            std::atomic<bool>& running;
        };

        // Launch everything
        auto audio = std::make_unique<AudioThread>(assignmentStore, waveformStore, lfoCurrentValues, running);
        auto gui1 = std::make_unique<GuiAssignmentThread>(assignmentStore, running);
        auto gui2 = std::make_unique<GuiWaveformThread>(waveformStore, running);
        auto gui3 = std::make_unique<GuiValueReaderThread>(lfoCurrentValues, assignmentStore, running);

        audio->startThread(juce::Thread::Priority::highest);
        gui1->startThread();
        gui2->startThread();
        gui3->startThread();

        // Run for 5 seconds — if we deadlock, the test runner will hang (detectable by timeout)
        juce::Thread::sleep(5000);
        running.store(false);

        // Wait for all threads with a generous timeout — if any hangs, it's a deadlock
        bool allExited = true;
        allExited &= audio->waitForThreadToExit(5000);
        allExited &= gui1->waitForThreadToExit(5000);
        allExited &= gui2->waitForThreadToExit(5000);
        allExited &= gui3->waitForThreadToExit(5000);

        expect(allExited, "All threads should exit cleanly (no deadlock)");
    }
};

// ============================================================================
// Test 6: LfoWaveform::evaluate correctness — ensure presets produce expected
// values at known phases
// ============================================================================

class LfoWaveformCorrectnessTest : public juce::UnitTest {
public:
    LfoWaveformCorrectnessTest() : juce::UnitTest("LFO Waveform Correctness", "LFO") {}

    void runTest() override {
        beginTest("Triangle preset boundary values");
        {
            auto wf = createLfoPreset(LfoPreset::Triangle);
            expectWithinAbsoluteError(wf.evaluate(0.0f), 0.0f, 0.01f);
            expectWithinAbsoluteError(wf.evaluate(0.5f), 1.0f, 0.01f);
            expectWithinAbsoluteError(wf.evaluate(1.0f), 0.0f, 0.01f);
        }

        beginTest("Sawtooth preset boundary values");
        {
            auto wf = createLfoPreset(LfoPreset::Sawtooth);
            expectWithinAbsoluteError(wf.evaluate(0.0f), 0.0f, 0.01f);
            expectWithinAbsoluteError(wf.evaluate(0.5f), 0.5f, 0.01f);
            expectWithinAbsoluteError(wf.evaluate(0.99f), 0.99f, 0.02f);
        }

        beginTest("Square preset values");
        {
            auto wf = createLfoPreset(LfoPreset::Square);
            expectWithinAbsoluteError(wf.evaluate(0.25f), 1.0f, 0.01f);
            expectWithinAbsoluteError(wf.evaluate(0.75f), 0.0f, 0.01f);
        }

        beginTest("Sine preset mid-points");
        {
            auto wf = createLfoPreset(LfoPreset::Sine);
            // At phase 0, should be ~0.5 (midpoint of sine)
            expectWithinAbsoluteError(wf.evaluate(0.0f), 0.5f, 0.05f);
            // At phase 0.25, should be ~1.0 (peak)
            expectWithinAbsoluteError(wf.evaluate(0.25f), 1.0f, 0.05f);
            // At phase 0.75, should be ~0.0 (trough)
            expectWithinAbsoluteError(wf.evaluate(0.75f), 0.0f, 0.05f);
        }

        beginTest("Evaluate wraps phase correctly");
        {
            auto wf = createLfoPreset(LfoPreset::Triangle);
            // Phase > 1 should wrap
            expectWithinAbsoluteError(wf.evaluate(1.5f), 1.0f, 0.01f);
            // Negative phase should wrap
            expectWithinAbsoluteError(wf.evaluate(-0.5f), 1.0f, 0.01f);
        }

        beginTest("LfoAudioState phase wrapping");
        {
            LfoAudioState state;
            auto wf = createLfoPreset(LfoPreset::Sawtooth);
            // Advance through many cycles, phase should always stay in [0, 1)
            float val;
            for (int i = 0; i < 100000; ++i) {
                state.advanceBlock(&val, 1, 440.0f, 44100.0f, wf, LfoMode::Free, 0.0f);
                expect(val >= -0.01f && val <= 1.01f,
                       "LFO value out of range at sample " + juce::String(i));
                expect(state.phase >= 0.0f && state.phase < 1.0f,
                       "Phase out of range at sample " + juce::String(i));
            }
        }
    }
};

// ============================================================================
// Test 7: XML serialization round-trip for LfoAssignment and LfoWaveform
// ============================================================================

class LfoSerializationTest : public juce::UnitTest {
public:
    LfoSerializationTest() : juce::UnitTest("LFO Serialization Round-trip", "LFO") {}

    void runTest() override {
        beginTest("LfoAssignment XML round-trip");
        {
            LfoAssignment original{2, "bitcrush_amount", -0.75f, true};
            auto xml = std::make_unique<juce::XmlElement>("assignment");
            original.saveToXml(xml.get(), "lfo");
            auto loaded = LfoAssignment::loadFromXml(xml.get(), "lfo");

            expectEquals(loaded.sourceIndex, original.sourceIndex);
            expectEquals(loaded.paramId, original.paramId);
            expectWithinAbsoluteError((double)loaded.depth, (double)original.depth, 0.001);
            expect(loaded.bipolar == original.bipolar, "bipolar flag should round-trip");
        }

        beginTest("LfoWaveform XML round-trip");
        {
            auto original = createLfoPreset(LfoPreset::Sine);
            auto xml = std::make_unique<juce::XmlElement>("waveform");
            original.saveToXml(xml.get());

            LfoWaveform loaded;
            loaded.loadFromXml(xml.get());

            expectEquals((int)loaded.nodes.size(), (int)original.nodes.size());
            for (int i = 0; i < (int)original.nodes.size(); ++i) {
                expectWithinAbsoluteError(loaded.nodes[(size_t)i].time, original.nodes[(size_t)i].time, 0.001);
                expectWithinAbsoluteError(loaded.nodes[(size_t)i].value, original.nodes[(size_t)i].value, 0.001);
                expectWithinAbsoluteError((double)loaded.nodes[(size_t)i].curve, (double)original.nodes[(size_t)i].curve, 0.001);
            }
        }

        beginTest("Negative depth serialization round-trip");
        {
            LfoAssignment original{1, "frequency", -0.42f, false};
            auto xml = std::make_unique<juce::XmlElement>("assignment");
            original.saveToXml(xml.get(), "lfo");
            auto loaded = LfoAssignment::loadFromXml(xml.get(), "lfo");

            expectWithinAbsoluteError((double)loaded.depth, -0.42, 0.001);
            expect(!loaded.bipolar, "Bipolar should be false");
        }
    }
};

// ============================================================================
// Static registration — JUCE auto-discovers these
// ============================================================================
static LfoAssignmentRaceTest lfoAssignmentRaceTest;
static LfoWaveformRaceTest lfoWaveformRaceTest;
static LfoAudioStateStressTest lfoAudioStateStressTest;
static LfoAssignmentOrderTest lfoAssignmentOrderTest;
static LfoFullSystemStressTest lfoFullSystemStressTest;
static LfoWaveformCorrectnessTest lfoWaveformCorrectnessTest;
static LfoSerializationTest lfoSerializationTest;
