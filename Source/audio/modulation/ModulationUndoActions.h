#pragma once

#include <JuceHeader.h>
#include "ModAssignment.h"
#include "ModulationAssignmentStore.h"
#include "LfoState.h"

// UndoableAction for adding a modulation assignment.
struct AddAssignmentAction : public juce::UndoableAction {
    ModulationAssignmentStore<ModAssignment>& store;
    ModAssignment assignment;
    std::optional<ModAssignment> previousAssignment;

    AddAssignmentAction(ModulationAssignmentStore<ModAssignment>& s, const ModAssignment& a)
        : store(s), assignment(a), previousAssignment(s.find(a.sourceIndex, a.paramId)) {}

    bool perform() override {
        store.add(assignment);
        return true;
    }

    bool undo() override {
        if (previousAssignment.has_value()) {
            store.add(*previousAssignment);
        } else {
            store.remove(assignment.sourceIndex, assignment.paramId);
        }
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
    std::atomic<bool>& customState;
    bool oldCustom;
    bool newCustom;
    juce::AudioProcessorParameter& presetParameter;

    LfoWaveformChangeAction(LfoWaveform* waveforms, juce::SpinLock& lock, int idx, const LfoWaveform& oldWf, const LfoWaveform& newWf,
                           std::atomic<bool>& custom, bool wasCustom, bool isCustom, juce::AudioProcessorParameter& preset)
        : waveforms(waveforms), waveformLock(lock), index(idx),
          oldWaveform(oldWf), newWaveform(newWf), customState(custom), oldCustom(wasCustom), newCustom(isCustom), presetParameter(preset) {}

    bool perform() override {
        return apply(newWaveform, newCustom);
    }

    bool undo() override {
        return apply(oldWaveform, oldCustom);
    }

private:
    bool apply(const LfoWaveform& waveform, bool custom) {
        {
            juce::SpinLock::ScopedLockType l(waveformLock);
            waveforms[index] = waveform;
            customState.store(custom, std::memory_order_relaxed);
        }
        // Refresh any currently open editor, including one created after this action.
        presetParameter.sendValueChangedMessageToListeners(presetParameter.getValue());
        return true;
    }
};
