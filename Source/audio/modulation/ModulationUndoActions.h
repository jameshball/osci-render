#pragma once

#include <JuceHeader.h>
#include "ModAssignment.h"
#include "ModulationAssignmentStore.h"
#include "LfoState.h"

// UndoableAction for adding a modulation assignment.
struct AddAssignmentAction : public juce::UndoableAction {
    ModulationAssignmentStore<ModAssignment>& store;
    ModAssignment assignment;

    AddAssignmentAction(ModulationAssignmentStore<ModAssignment>& s, const ModAssignment& a)
        : store(s), assignment(a) {}

    bool perform() override {
        store.add(assignment);
        return true;
    }

    bool undo() override {
        store.remove(assignment.sourceIndex, assignment.paramId);
        return true;
    }
};

// UndoableAction for removing a modulation assignment.
struct RemoveAssignmentAction : public juce::UndoableAction {
    ModulationAssignmentStore<ModAssignment>& store;
    ModAssignment assignment;

    RemoveAssignmentAction(ModulationAssignmentStore<ModAssignment>& s, const ModAssignment& a)
        : store(s), assignment(a) {}

    bool perform() override {
        store.remove(assignment.sourceIndex, assignment.paramId);
        return true;
    }

    bool undo() override {
        store.add(assignment);
        return true;
    }
};

// UndoableAction for changing an LFO waveform shape.
struct LfoWaveformChangeAction : public juce::UndoableAction {
    // We store a pointer to the waveform array and lock, plus the index.
    LfoWaveform* waveforms;
    juce::SpinLock& waveformLock;
    int index;
    LfoWaveform oldWaveform;
    LfoWaveform newWaveform;

    LfoWaveformChangeAction(LfoWaveform* waveforms, juce::SpinLock& lock, int idx,
                            const LfoWaveform& oldWf, const LfoWaveform& newWf)
        : waveforms(waveforms), waveformLock(lock), index(idx),
          oldWaveform(oldWf), newWaveform(newWf) {}

    bool perform() override {
        juce::SpinLock::ScopedLockType l(waveformLock);
        waveforms[index] = newWaveform;
        return true;
    }

    bool undo() override {
        juce::SpinLock::ScopedLockType l(waveformLock);
        waveforms[index] = oldWaveform;
        return true;
    }
};
