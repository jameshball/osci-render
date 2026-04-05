#include <JuceHeader.h>
#include "TestCleanup.h"
#include <unordered_map>

// ============================================================================
// Undo / Redo Tests — comprehensive coverage of the ValueTree-backed undo
// system for FloatParameter, IntParameter, BooleanParameter, undoGrouping,
// undoSuppressed, linked parameter coalescing, and effect precedence.
// ============================================================================

// ---------------------------------------------------------------------------
// Harness — mirrors CommonPluginProcessor's ValueTree + UndoManager binding
// with a ValueTree::Listener to dispatch undo/redo back to parameters.
// ---------------------------------------------------------------------------

struct UndoTestHarness : public juce::ValueTree::Listener {
    juce::UndoManager undoManager;
    juce::ValueTree stateTree { "State" };
    juce::String lastUndoParamId;
    bool undoSuppressed = false;
    bool undoGrouping = false;
    std::unordered_map<juce::String, juce::AudioProcessorParameter*> paramIdMap;

    UndoTestHarness() { stateTree.addListener(this); }
    ~UndoTestHarness() override { stateTree.removeListener(this); }

    void bind(osci::FloatParameter& p) {
        p.bindToValueTree(stateTree, &undoManager, &lastUndoParamId, &undoSuppressed, &undoGrouping);
        paramIdMap[p.paramID] = &p;
    }
    void bind(osci::BooleanParameter& p) {
        p.bindToValueTree(stateTree, &undoManager, &lastUndoParamId, &undoSuppressed, &undoGrouping);
        paramIdMap[p.paramID] = &p;
    }
    void bind(osci::IntParameter& p) {
        p.bindToValueTree(stateTree, &undoManager, &lastUndoParamId, &undoSuppressed, &undoGrouping);
        paramIdMap[p.paramID] = &p;
    }
    void bindExcluded(osci::FloatParameter& p) {
        p.bindToValueTree(stateTree, nullptr, &lastUndoParamId, &undoSuppressed, &undoGrouping);
        paramIdMap[p.paramID] = &p;
    }
    void bindExcluded(osci::BooleanParameter& p) {
        p.bindToValueTree(stateTree, nullptr, &lastUndoParamId, &undoSuppressed, &undoGrouping);
        paramIdMap[p.paramID] = &p;
    }

    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override {
        if (tree != stateTree) return;
        auto it = paramIdMap.find(property.toString());
        if (it == paramIdMap.end()) return;
        auto* param = it->second;
        if (auto* bp = dynamic_cast<osci::BooleanParameter*>(param))
            bp->applyValueFromTree();
        else if (auto* ip = dynamic_cast<osci::IntParameter*>(param))
            ip->applyValueFromTree();
        else if (auto* fp = dynamic_cast<osci::FloatParameter*>(param))
            fp->applyValueFromTree();
    }
};

// ============================================================================
// FloatParameter Undo/Redo
// ============================================================================

class FloatParameterUndoTest : public juce::UnitTest {
public:
    FloatParameterUndoTest() : juce::UnitTest("FloatParameter Undo/Redo", "Undo") {}

    void runTest() override {
        beginTest("Basic undo/redo round-trip");
        {
            UndoTestHarness h;
            osci::FloatParameter p("Level", "level", 2, 0.5f, 0.0f, 1.0f);
            h.bind(p);

            p.setUnnormalisedValueNotifyingHost(0.75f);
            expect(h.undoManager.canUndo(), "Should have undo after setValue");

            h.undoManager.undo();
            expectWithinAbsoluteError(p.getValueUnnormalised(), 0.5f, 1e-4f,
                "Undo should restore initial value");

            h.undoManager.redo();
            expectWithinAbsoluteError(p.getValueUnnormalised(), 0.75f, 1e-4f,
                "Redo should restore changed value");
        }

        beginTest("Multiple changes to different params create separate transactions");
        {
            UndoTestHarness h;
            osci::FloatParameter p1("Gain", "gain", 2, 0.0f, 0.0f, 10.0f);
            osci::FloatParameter p2("Pan", "pan", 2, 0.0f, -1.0f, 1.0f);
            h.bind(p1);
            h.bind(p2);

            p1.setUnnormalisedValueNotifyingHost(3.0f);
            p2.setUnnormalisedValueNotifyingHost(0.5f);

            h.undoManager.undo(); // undo p2
            expectWithinAbsoluteError(p2.getValueUnnormalised(), 0.0f, 1e-4f, "pan undone");
            expectWithinAbsoluteError(p1.getValueUnnormalised(), 3.0f, 1e-4f, "gain unchanged");

            h.undoManager.undo(); // undo p1
            expectWithinAbsoluteError(p1.getValueUnnormalised(), 0.0f, 1e-4f, "gain undone");
        }

        beginTest("Same parameter repeated changes coalesce into one transaction");
        {
            UndoTestHarness h;
            osci::FloatParameter p("Freq", "freq", 2, 440.0f, 20.0f, 20000.0f);
            h.bind(p);

            p.setUnnormalisedValueNotifyingHost(500.0f);
            p.setUnnormalisedValueNotifyingHost(600.0f);
            p.setUnnormalisedValueNotifyingHost(700.0f);

            h.undoManager.undo();
            expectWithinAbsoluteError(p.getValueUnnormalised(), 440.0f, 1e-4f,
                "Single undo should restore all coalesced changes");
        }
    }
};
static FloatParameterUndoTest floatParamUndoTest;

// ============================================================================
// BooleanParameter Undo/Redo
// ============================================================================

class BooleanParameterUndoTest : public juce::UnitTest {
public:
    BooleanParameterUndoTest() : juce::UnitTest("BooleanParameter Undo/Redo", "Undo") {}

    void runTest() override {
        beginTest("Bool undo/redo round-trip");
        {
            UndoTestHarness h;
            osci::BooleanParameter p("Enabled", "enabled", 2, true, "");
            h.bind(p);

            p.setBoolValueNotifyingHost(false);
            expect(!p.getBoolValue(), "Should be false after set");
            expect(h.undoManager.canUndo(), "Should have undo");

            h.undoManager.undo();
            expect(p.getBoolValue(), "Undo should restore true");

            h.undoManager.redo();
            expect(!p.getBoolValue(), "Redo should restore false");
        }

        beginTest("Bool toggle does not create transaction when undoSuppressed");
        {
            UndoTestHarness h;
            osci::BooleanParameter p("Mute", "mute", 2, false, "");
            h.bind(p);

            h.undoSuppressed = true;
            p.setBoolValueNotifyingHost(true);
            h.undoSuppressed = false;

            expect(!h.undoManager.canUndo(), "No undo when undoSuppressed was true");
            expect(p.getBoolValue(), "Value should still have changed");
        }
    }
};
static BooleanParameterUndoTest boolParamUndoTest;

// ============================================================================
// IntParameter Undo/Redo
// ============================================================================

class IntParameterUndoTest : public juce::UnitTest {
public:
    IntParameterUndoTest() : juce::UnitTest("IntParameter Undo/Redo", "Undo") {}

    void runTest() override {
        beginTest("Int undo/redo round-trip");
        {
            UndoTestHarness h;
            osci::IntParameter p("Voices", "voices", 2, 4, 1, 16);
            h.bind(p);

            p.setUnnormalisedValueNotifyingHost(8);
            expect(p.getValueUnnormalised() == 8, "Should be 8 after set");

            h.undoManager.undo();
            expect(p.getValueUnnormalised() == 4, "Undo should restore 4");

            h.undoManager.redo();
            expect(p.getValueUnnormalised() == 8, "Redo should restore 8");
        }
    }
};
static IntParameterUndoTest intParamUndoTest;

// ============================================================================
// undoGrouping — multiple different parameters coalesce into one transaction
// ============================================================================

class UndoGroupingTest : public juce::UnitTest {
public:
    UndoGroupingTest() : juce::UnitTest("Undo Grouping", "Undo") {}

    void runTest() override {
        beginTest("undoGrouping prevents beginNewTransaction for different params");
        {
            UndoTestHarness h;
            osci::FloatParameter p1("X", "x", 2, 0.0f, -1.0f, 1.0f);
            osci::FloatParameter p2("Y", "y", 2, 0.0f, -1.0f, 1.0f);
            osci::FloatParameter p3("Z", "z", 2, 0.0f, -1.0f, 1.0f);
            h.bind(p1);
            h.bind(p2);
            h.bind(p3);

            p1.setUnnormalisedValueNotifyingHost(0.5f);
            h.undoGrouping = true;
            p2.setUnnormalisedValueNotifyingHost(0.6f);
            p3.setUnnormalisedValueNotifyingHost(0.7f);
            h.undoGrouping = false;

            h.undoManager.undo();
            expectWithinAbsoluteError(p1.getValueUnnormalised(), 0.0f, 1e-4f, "p1 undone");
            expectWithinAbsoluteError(p2.getValueUnnormalised(), 0.0f, 1e-4f, "p2 undone");
            expectWithinAbsoluteError(p3.getValueUnnormalised(), 0.0f, 1e-4f, "p3 undone");

            h.undoManager.redo();
            expectWithinAbsoluteError(p1.getValueUnnormalised(), 0.5f, 1e-4f, "p1 redone");
            expectWithinAbsoluteError(p2.getValueUnnormalised(), 0.6f, 1e-4f, "p2 redone");
            expectWithinAbsoluteError(p3.getValueUnnormalised(), 0.7f, 1e-4f, "p3 redone");
        }

        beginTest("Without undoGrouping, different params are separate transactions");
        {
            UndoTestHarness h;
            osci::FloatParameter p1("A", "a", 2, 0.0f, 0.0f, 1.0f);
            osci::FloatParameter p2("B", "b", 2, 0.0f, 0.0f, 1.0f);
            h.bind(p1);
            h.bind(p2);

            p1.setUnnormalisedValueNotifyingHost(0.5f);
            p2.setUnnormalisedValueNotifyingHost(0.5f);

            h.undoManager.undo();
            expectWithinAbsoluteError(p2.getValueUnnormalised(), 0.0f, 1e-4f, "p2 undone");
            expectWithinAbsoluteError(p1.getValueUnnormalised(), 0.5f, 1e-4f, "p1 unchanged");

            h.undoManager.undo();
            expectWithinAbsoluteError(p1.getValueUnnormalised(), 0.0f, 1e-4f, "p1 undone");
        }
    }
};
static UndoGroupingTest undoGroupingTest;

// ============================================================================
// undoSuppressed — no undo records at all
// ============================================================================

class UndoSuppressedTest : public juce::UnitTest {
public:
    UndoSuppressedTest() : juce::UnitTest("Undo Suppressed", "Undo") {}

    void runTest() override {
        beginTest("Float changes suppressed create no undo");
        {
            UndoTestHarness h;
            osci::FloatParameter p("Vol", "vol", 2, 0.5f, 0.0f, 1.0f);
            h.bind(p);

            h.undoSuppressed = true;
            p.setUnnormalisedValueNotifyingHost(0.8f);
            h.undoSuppressed = false;

            expect(!h.undoManager.canUndo(), "No undo when suppressed");
            expectWithinAbsoluteError(p.getValueUnnormalised(), 0.8f, 1e-4f,
                "Value should still change");
        }

        beginTest("Int changes suppressed create no undo");
        {
            UndoTestHarness h;
            osci::IntParameter p("Mode", "mode", 2, 1, 0, 10);
            h.bind(p);

            h.undoSuppressed = true;
            p.setUnnormalisedValueNotifyingHost(5);
            h.undoSuppressed = false;

            expect(!h.undoManager.canUndo(), "No undo when suppressed");
            expect(p.getValueUnnormalised() == 5, "Value should still change");
        }
    }
};
static UndoSuppressedTest undoSuppressedTest;

// ============================================================================
// Excluded parameters — bound without UndoManager
// ============================================================================

class UndoExcludedTest : public juce::UnitTest {
public:
    UndoExcludedTest() : juce::UnitTest("Undo Excluded Parameters", "Undo") {}

    void runTest() override {
        beginTest("Excluded FloatParameter creates no undo");
        {
            UndoTestHarness h;
            osci::FloatParameter p("Viz", "viz", 2, 0.5f, 0.0f, 1.0f);
            h.bindExcluded(p);

            p.setUnnormalisedValueNotifyingHost(1.0f);
            expect(!h.undoManager.canUndo(), "Excluded param should not create undo");
            expectWithinAbsoluteError(p.getValueUnnormalised(), 1.0f, 1e-4f);
        }

        beginTest("Excluded BooleanParameter creates no undo");
        {
            UndoTestHarness h;
            osci::BooleanParameter p("Flag", "flag", 2, false, "");
            h.bindExcluded(p);

            p.setBoolValueNotifyingHost(true);
            expect(!h.undoManager.canUndo(), "Excluded bool should not create undo");
            expect(p.getBoolValue());
        }
    }
};
static UndoExcludedTest undoExcludedTest;

// ============================================================================
// Linked parameters — slider.onValueChange coalescing
// ============================================================================

class LinkedParameterUndoTest : public juce::UnitTest {
public:
    LinkedParameterUndoTest() : juce::UnitTest("Linked Parameter Undo", "Undo") {}

    void runTest() override {
        beginTest("Linked params with grouping: single undo reverts all");
        {
            UndoTestHarness h;
            osci::FloatParameter p0("ScaleX", "scaleX", 2, 1.0f, 0.0f, 5.0f);
            osci::FloatParameter p1("ScaleY", "scaleY", 2, 1.0f, 0.0f, 5.0f);
            osci::FloatParameter p2("ScaleZ", "scaleZ", 2, 1.0f, 0.0f, 5.0f);
            h.bind(p0);
            h.bind(p1);
            h.bind(p2);

            // Simulate slider.onValueChange: first param, then linked with grouping
            p0.setUnnormalisedValueNotifyingHost(2.5f);
            h.undoGrouping = true;
            p1.setUnnormalisedValueNotifyingHost(2.5f);
            p2.setUnnormalisedValueNotifyingHost(2.5f);
            h.undoGrouping = false;

            h.undoManager.undo();
            expectWithinAbsoluteError(p0.getValueUnnormalised(), 1.0f, 1e-4f, "p0 undone");
            expectWithinAbsoluteError(p1.getValueUnnormalised(), 1.0f, 1e-4f, "p1 undone");
            expectWithinAbsoluteError(p2.getValueUnnormalised(), 1.0f, 1e-4f, "p2 undone");
        }

        beginTest("Linked drag coalesces across multiple onValueChange calls");
        {
            UndoTestHarness h;
            osci::FloatParameter p0("X", "lx", 2, 0.0f, 0.0f, 10.0f);
            osci::FloatParameter p1("Y", "ly", 2, 0.0f, 0.0f, 10.0f);
            h.bind(p0);
            h.bind(p1);

            for (int v = 1; v <= 5; v++) {
                p0.setUnnormalisedValueNotifyingHost((float)v);
                h.undoGrouping = true;
                p1.setUnnormalisedValueNotifyingHost((float)v);
                h.undoGrouping = false;
                // Restore lastUndoParamId to the primary slider's param (mirrors app code)
                h.lastUndoParamId = p0.paramID;
            }

            h.undoManager.undo();
            expectWithinAbsoluteError(p0.getValueUnnormalised(), 0.0f, 1e-4f, "p0 undone to initial");
            expectWithinAbsoluteError(p1.getValueUnnormalised(), 0.0f, 1e-4f, "p1 undone to initial");
        }
    }
};
static LinkedParameterUndoTest linkedParamUndoTest;

// ============================================================================
// Effect undo — setValue and enabled toggle through SimpleEffect
// ============================================================================

class EffectUndoTest : public juce::UnitTest {
public:
    EffectUndoTest() : juce::UnitTest("Effect setValue Undo", "Undo") {}

    void runTest() override {
        beginTest("Effect.setValue undo/redo via SimpleEffect");
        {
            UndoTestHarness h;
            osci::EffectParameter param("Depth", "", "depth", 2, 0.5f, 0.0f, 1.0f, 0.01f);
            h.bind(param);

            osci::SimpleEffect effect(&param);

            effect.setValue(0, 0.8f);
            expectWithinAbsoluteError(param.getValueUnnormalised(), 0.8f, 1e-4f);

            h.undoManager.undo();
            expectWithinAbsoluteError(param.getValueUnnormalised(), 0.5f, 1e-4f,
                "Effect.setValue should be undoable");

            h.undoManager.redo();
            expectWithinAbsoluteError(param.getValueUnnormalised(), 0.8f, 1e-4f,
                "Effect.setValue should be redoable");

            testutil::cleanupSubParams(param);
        }

        beginTest("Effect enabled toggle undo/redo");
        {
            UndoTestHarness h;
            osci::EffectParameter param("Amount", "", "amount", 2, 0.5f, 0.0f, 1.0f, 0.01f);
            h.bind(param);

            osci::SimpleEffect effect(&param);
            effect.markEnableable(true);
            h.bind(*effect.enabled);

            expect(effect.enabled->getBoolValue(), "Should start enabled");

            effect.enabled->setBoolValueNotifyingHost(false);
            expect(!effect.enabled->getBoolValue(), "Should be disabled");

            h.undoManager.undo();
            expect(effect.enabled->getBoolValue(), "Undo should re-enable");

            h.undoManager.redo();
            expect(!effect.enabled->getBoolValue(), "Redo should re-disable");

            delete effect.enabled;
            effect.enabled = nullptr;
            testutil::cleanupSubParams(param);
        }
    }
};
static EffectUndoTest effectUndoTest;

// ============================================================================
// PrecedenceChangeAction undo/redo — effect ordering
// ============================================================================

class PrecedenceUndoTest : public juce::UnitTest {
public:
    PrecedenceUndoTest() : juce::UnitTest("Precedence Change Undo", "Undo") {}

    static void applyOrder(std::vector<std::shared_ptr<osci::Effect>>& effects,
                           const std::vector<juce::String>& order) {
        std::unordered_map<juce::String, std::shared_ptr<osci::Effect>> idMap;
        for (auto& effect : effects)
            idMap[effect->getId()] = effect;

        int idx = 0;
        for (auto& id : order) {
            auto it = idMap.find(id);
            if (it != idMap.end()) {
                it->second->setPrecedence(idx++);
                idMap.erase(it);
            }
        }

        for (auto& effect : effects) {
            auto it = idMap.find(effect->getId());
            if (it != idMap.end()) {
                effect->setPrecedence(idx++);
                idMap.erase(it);
            }
        }

        std::sort(effects.begin(), effects.end(), [](const auto& a, const auto& b) {
            return a->getPrecedence() < b->getPrecedence();
        });
    }

    struct LocalPrecedenceChangeAction : public juce::UndoableAction {
        LocalPrecedenceChangeAction(std::vector<std::shared_ptr<osci::Effect>>& effects,
                                    std::vector<juce::String> before,
                                    std::vector<juce::String> after)
            : effects(effects), beforeOrder(std::move(before)), afterOrder(std::move(after)) {}

        bool perform() override {
            PrecedenceUndoTest::applyOrder(effects, afterOrder);
            return true;
        }

        bool undo() override {
            PrecedenceUndoTest::applyOrder(effects, beforeOrder);
            return true;
        }

        std::vector<std::shared_ptr<osci::Effect>>& effects;
        std::vector<juce::String> beforeOrder;
        std::vector<juce::String> afterOrder;
    };

    void runTest() override {
        beginTest("Precedence action perform and undo");
        {
            osci::EffectParameter p1("E1", "", "e1", 2, 0.0f, 0.0f, 1.0f, 0.01f);
            osci::EffectParameter p2("E2", "", "e2", 2, 0.0f, 0.0f, 1.0f, 0.01f);
            osci::EffectParameter p3("E3", "", "e3", 2, 0.0f, 0.0f, 1.0f, 0.01f);
            osci::EffectParameter p4("E4", "", "e4", 2, 0.0f, 0.0f, 1.0f, 0.01f);

            auto e1 = std::make_shared<osci::SimpleEffect>(&p1);
            auto e2 = std::make_shared<osci::SimpleEffect>(&p2);
            auto e3 = std::make_shared<osci::SimpleEffect>(&p3);
            auto omitted = std::make_shared<osci::SimpleEffect>(&p4);

            std::vector<std::shared_ptr<osci::Effect>> effects { e1, e2, e3, omitted };

            e1->setPrecedence(0);
            e2->setPrecedence(1);
            e3->setPrecedence(2);
            omitted->setPrecedence(1);

            const auto beforeOrder = std::vector<juce::String>{ e1->getId(), e2->getId(), e3->getId() };
            const auto afterOrder = std::vector<juce::String>{ e3->getId(), e1->getId(), e2->getId() };

            auto positionOf = [&](const juce::String& effectId) {
                for (int i = 0; i < (int)effects.size(); ++i) {
                    if (effects[(size_t)i]->getId() == effectId)
                        return i;
                }
                return -1;
            };

            juce::UndoManager undoManager;

            undoManager.beginNewTransaction("Reorder Effects");
            undoManager.perform(new LocalPrecedenceChangeAction(effects, beforeOrder, afterOrder));

            expect(positionOf(e3->getId()) < positionOf(e1->getId()), "E3 should move before E1");
            expect(positionOf(e1->getId()) < positionOf(e2->getId()), "E1 should move before E2");
            expect(positionOf(e2->getId()) < positionOf(omitted->getId()), "Omitted effects should be appended after ordered ones");

            undoManager.undo();
            expect(positionOf(e1->getId()) < positionOf(e2->getId()), "Undo should restore E1 before E2");
            expect(positionOf(e2->getId()) < positionOf(e3->getId()), "Undo should restore E2 before E3");
            expect(positionOf(e3->getId()) < positionOf(omitted->getId()), "Undo should keep omitted effects after the ordered subset");

            undoManager.redo();
            expect(positionOf(e3->getId()) < positionOf(e1->getId()), "Redo should restore E3 before E1");
            expect(positionOf(e1->getId()) < positionOf(e2->getId()), "Redo should restore E1 before E2");

            for (auto* ep : { &p1, &p2, &p3, &p4 }) {
                testutil::cleanupSubParams(*ep);
            }
        }
    }
};
static PrecedenceUndoTest precedenceUndoTest;

// ============================================================================
// Mixed scenario: interleaved parameter changes and undos
// ============================================================================

class MixedUndoScenarioTest : public juce::UnitTest {
public:
    MixedUndoScenarioTest() : juce::UnitTest("Mixed Undo Scenarios", "Undo") {}

    void runTest() override {
        beginTest("Interleaved float and bool changes undo correctly");
        {
            UndoTestHarness h;
            osci::FloatParameter fp("Level", "lvl", 2, 0.5f, 0.0f, 1.0f);
            osci::BooleanParameter bp("Bypass", "byp", 2, false, "");
            h.bind(fp);
            h.bind(bp);

            fp.setUnnormalisedValueNotifyingHost(0.7f);  // transaction 1
            bp.setBoolValueNotifyingHost(true);           // transaction 2
            fp.setUnnormalisedValueNotifyingHost(0.9f);   // transaction 3

            h.undoManager.undo(); // undo 3
            expectWithinAbsoluteError(fp.getValueUnnormalised(), 0.7f, 1e-4f, "fp back to 0.7");

            h.undoManager.undo(); // undo 2
            expect(!bp.getBoolValue(), "bp back to false");

            h.undoManager.undo(); // undo 1
            expectWithinAbsoluteError(fp.getValueUnnormalised(), 0.5f, 1e-4f, "fp back to 0.5");

            expect(!h.undoManager.canUndo(), "No more undo");
        }

        beginTest("Redo after undo, then new change clears redo stack");
        {
            UndoTestHarness h;
            osci::FloatParameter p("Val", "val", 2, 0.0f, 0.0f, 10.0f);
            h.bind(p);

            p.setUnnormalisedValueNotifyingHost(5.0f);
            h.undoManager.beginNewTransaction();
            p.setUnnormalisedValueNotifyingHost(8.0f);

            h.undoManager.undo();
            expectWithinAbsoluteError(p.getValueUnnormalised(), 5.0f, 1e-4f);
            expect(h.undoManager.canRedo(), "Should be able to redo");

            // New change on a different param should clear redo
            osci::FloatParameter p2("Other", "oth", 2, 0.0f, 0.0f, 1.0f);
            h.bind(p2);
            p2.setUnnormalisedValueNotifyingHost(0.3f);
            expect(!h.undoManager.canRedo(), "Redo cleared after new action");
        }

        beginTest("Grouped bool + float in single undo");
        {
            UndoTestHarness h;
            osci::BooleanParameter bp("Sel", "sel", 2, false, "");
            osci::FloatParameter fp("Str", "str", 2, 0.5f, 0.0f, 1.0f);
            h.bind(bp);
            h.bind(fp);

            h.undoManager.beginNewTransaction();
            h.undoGrouping = true;
            bp.setBoolValueNotifyingHost(true);
            fp.setUnnormalisedValueNotifyingHost(0.9f);
            h.undoGrouping = false;

            h.undoManager.undo();
            expect(!bp.getBoolValue(), "Bool reverted");
            expectWithinAbsoluteError(fp.getValueUnnormalised(), 0.5f, 1e-4f, "Float reverted");
        }
    }
};
static MixedUndoScenarioTest mixedUndoScenarioTest;

// ============================================================================
// ValueTree sync — applyValueFromTree
// ============================================================================

class ValueTreeSyncTest : public juce::UnitTest {
public:
    ValueTreeSyncTest() : juce::UnitTest("ValueTree Sync", "Undo") {}

    void runTest() override {
        beginTest("applyValueFromTree restores float parameter from tree");
        {
            UndoTestHarness h;
            osci::FloatParameter p("Test", "test", 2, 0.0f, 0.0f, 100.0f);
            h.bind(p);

            h.stateTree.setProperty(juce::Identifier("test"), 42.0f, nullptr);
            p.applyValueFromTree();
            expectWithinAbsoluteError(p.getValueUnnormalised(), 42.0f, 1e-4f);
        }

        beginTest("applyValueFromTree restores bool parameter from tree");
        {
            UndoTestHarness h;
            osci::BooleanParameter p("Flag", "flag2", 2, false, "");
            h.bind(p);

            h.stateTree.setProperty(juce::Identifier("flag2"), true, nullptr);
            p.applyValueFromTree();
            expect(p.getBoolValue());
        }
    }
};
static ValueTreeSyncTest valueTreeSyncTest;
