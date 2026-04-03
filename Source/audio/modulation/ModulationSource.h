#pragma once

#include <JuceHeader.h>
#include <vector>
#include "ModAssignment.h"
#include "ModulationAssignmentStore.h"
#include "ModulationUndoActions.h"

// Abstract base class for all modulation source types (LFO, Envelope, Random, Sidechain).
// Provides a unified interface for assignment management, block-buffer access,
// UI snapshots, and serialization.  The ModulationEngine operates on this
// interface so that adding a new modulation type requires only a new subclass.
class ModulationSource {
public:
    virtual ~ModulationSource() = default;

    // === Identity ===
    virtual juce::String getTypeId() const = 0;      // XML attr name: "lfo", "env", "rng", "sc"
    virtual juce::String getTypeLabel() const = 0;    // UI drag prefix: "LFO", "ENV", "RNG", "SC"
    virtual int getSourceCount() const = 0;           // NUM_LFOS, NUM_ENVELOPES, etc.

    // === Undo support ===
    void setUndoManager(juce::UndoManager* um) { undoManager = um; }
    void setUndoSuppressedFlag(bool* flag) { undoSuppressedFlag = flag; }
    void setUndoGroupingFlag(bool* flag) { undoGroupingFlag = flag; }

    // === Assignment management (non-virtual — delegates to the store) ===
    void addAssignment(const ModAssignment& a) {
        auto* um = (undoSuppressedFlag != nullptr && *undoSuppressedFlag) ? nullptr : undoManager;
        if (um != nullptr) {
            if (undoGroupingFlag == nullptr || !*undoGroupingFlag)
                um->beginNewTransaction("Add Modulation");
            um->perform(new AddAssignmentAction(assignments, a));
        } else {
            assignments.add(a);
        }
    }
    void removeAssignment(int sourceIndex, const juce::String& paramId) {
        auto* um = (undoSuppressedFlag != nullptr && *undoSuppressedFlag) ? nullptr : undoManager;
        if (um != nullptr) {
            // Capture the existing assignment for undo
            auto all = assignments.getAll();
            for (const auto& a : all) {
                if (a.sourceIndex == sourceIndex && a.paramId == paramId) {
                    if (undoGroupingFlag == nullptr || !*undoGroupingFlag)
                        um->beginNewTransaction("Remove Modulation");
                    um->perform(new RemoveAssignmentAction(assignments, a));
                    return;
                }
            }
        }
        assignments.remove(sourceIndex, paramId);
    }
    std::vector<ModAssignment> getAssignments() const { return assignments.getAll(); }

    // Allocation-free version for the audio thread. Reuses a pre-grown buffer.
    const std::vector<ModAssignment>& getAssignmentsAudioThread() {
        assignments.copyInto(cachedAssignments);
        return cachedAssignments;
    }

    template<typename Pred>
    void removeAssignmentsIf(Pred pred) { assignments.removeIf(pred); }

    // === Block buffers (filled by type-specific code each processBlock) ===
    virtual const std::vector<float>* getBlockBuffers() const = 0;

    // === Prepare for playback (pre-allocate block buffers) ===
    virtual void prepareToPlay(double /*sampleRate*/, int /*samplesPerBlock*/) {}

    // === UI snapshot ===
    virtual float getCurrentValue(int sourceIndex) const = 0;

    // === Colour callback for UI indicators ===
    using ColourFn = juce::Colour(*)(int);
    virtual ColourFn getColourFunction() const { return colourFunction; }
    void setColourFunction(ColourFn fn) { colourFunction = fn; }

    // === Parameters for processor adoption ===
    const std::vector<osci::FloatParameter*>& getFloatParameters() const { return floatParameters; }
    const std::vector<osci::IntParameter*>&   getIntParameters()   const { return intParameters; }

    // === Serialization ===
    virtual void saveToXml(juce::XmlElement* root) const = 0;
    virtual void loadFromXml(const juce::XmlElement* root) = 0;

    // Assignment store — public so the engine and serialization helpers can access it directly.
    ModulationAssignmentStore<ModAssignment> assignments;

protected:
    std::vector<osci::FloatParameter*> floatParameters;
    std::vector<osci::IntParameter*>   intParameters;
    ColourFn colourFunction = nullptr;
    juce::UndoManager* undoManager = nullptr;
    bool* undoSuppressedFlag = nullptr;
    bool* undoGroupingFlag = nullptr;

private:
    // Pre-allocated to avoid audio-thread heap allocation when assignments grow.
    // 32 is a generous upper bound for typical usage.
    static constexpr int kMaxExpectedAssignments = 32;
    std::vector<ModAssignment> cachedAssignments = [] {
        std::vector<ModAssignment> v;
        v.reserve(kMaxExpectedAssignments);
        return v;
    }();
};
