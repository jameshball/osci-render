#pragma once

#include <JuceHeader.h>
#include <vector>
#include <algorithm>

// Generic thread-safe store for modulation assignments (LFO, Random, Sidechain, etc.).
// Eliminates duplicated add/remove/get/removeIf logic across parameter structs.
template<typename AssignmentType>
struct ModulationAssignmentStore {
    std::vector<AssignmentType> items;
    mutable juce::SpinLock lock;

    // Add or update an assignment (updates depth/bipolar if source+param already exists)
    void add(const AssignmentType& assignment) {
        juce::SpinLock::ScopedLockType scopedLock(lock);
        for (auto& a : items) {
            if (a.sourceIndex == assignment.sourceIndex && a.paramId == assignment.paramId) {
                a.depth = assignment.depth;
                a.bipolar = assignment.bipolar;
                return;
            }
        }
        items.push_back(assignment);
    }

    // Remove specific assignment by source index + parameter ID
    void remove(int sourceIndex, const juce::String& paramId) {
        juce::SpinLock::ScopedLockType scopedLock(lock);
        items.erase(
            std::remove_if(items.begin(), items.end(),
                [&](const AssignmentType& a) { return a.sourceIndex == sourceIndex && a.paramId == paramId; }),
            items.end());
    }

    // Snapshot copy all assignments (safe from any thread)
    std::vector<AssignmentType> getAll() const {
        juce::SpinLock::ScopedLockType scopedLock(lock);
        return items;
    }

    // Remove all assignments matching a predicate
    template<typename Pred>
    void removeIf(Pred pred) {
        juce::SpinLock::ScopedLockType scopedLock(lock);
        items.erase(std::remove_if(items.begin(), items.end(), pred), items.end());
    }

    void clear() {
        juce::SpinLock::ScopedLockType scopedLock(lock);
        items.clear();
    }
};
