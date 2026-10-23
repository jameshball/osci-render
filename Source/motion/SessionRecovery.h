#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>

namespace motion {
// The open session, written every little while to a file of its own (never
// to the project's), so a crash loses seconds of work rather than all since
// osci-motion opened. A clean quit removes it: one found at launch means the
// last session ended unexpectedly, and it is what to reopen.
class SessionRecovery {
public:
    explicit SessionRecovery(const juce::File& folder) : state(std::make_shared<State>(folder.getChildFile("osci-motion recovery.osci-motion"))) {}
    ~SessionRecovery() { finish(); }

    juce::File file() const { return state->file; }
    // Writes `session` on a thread of its own; a write still going on is let
    // finish and this one skipped (the next takes its place).
    bool write(juce::MemoryBlock session) {
        if (state->writing.exchange(true)) {
            return false;
        }
        juce::Thread::launch([shared = state, bytes = std::move(session)] {
            const juce::TemporaryFile temporary(shared->file);
            const auto written = temporary.getFile().replaceWithData(bytes.getData(), bytes.getSize());
            {
                const juce::SpinLock::ScopedLockType lock(shared->guard);
                if (written && !shared->finished) { temporary.overwriteTargetFileWithTemporary(); }
            }
            shared->writing.store(false);
        });
        return true;
    }
    // The session closed as it should: nothing to recover.
    void finish() {
        const juce::SpinLock::ScopedLockType lock(state->guard);
        state->finished = true;
        state->file.deleteFile();
    }

private:
    struct State {
        explicit State(juce::File target) : file(std::move(target)) {}
        const juce::File file;
        juce::SpinLock guard;
        bool finished = false;
        std::atomic<bool> writing {false};
    };
    std::shared_ptr<State> state;
};
}
