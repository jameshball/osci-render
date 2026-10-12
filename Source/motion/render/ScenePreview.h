#pragma once

#include "HiddenLines.h"

namespace motion {
// Lighting and reflections as an editor's view sees them, worked out on a
// thread of its own so the view never waits: lines lit as the Scope lights
// them, mirrors' own lines given way, and what mirrors show from where the
// view is. One request is waiting at most (a newer one replaces it); one
// result is kept, the latest.
class ScenePreview : private juce::Thread {
public:
    struct Shown {
        // What it was worked out for: the project's revision as the caller
        // counts them, and the time.
        std::uint64_t revision = 0;
        double time = 0;
        hidden::Frame frame;
        // Each prepared clip's identity (it and the placements holding it,
        // inner to outer), in the frame's clip order.
        std::vector<std::vector<Id>> clips;
    };

    explicit ScenePreview(std::function<void()> ready) : juce::Thread("Motion scene preview"), ready(std::move(ready)) { startThread(); }
    ~ScenePreview() override {
        signalThreadShouldExit();
        cancel.store(true);
        wake.signal();
        waitForThreadToExit(-1);
    }

    // A prepared clip's identity, to find it among another preparation's.
    static std::vector<Id> identityOf(const PreparedClip& clip) {
        std::vector<Id> identity {clip.id};
        for (const auto& ancestor : clip.ancestors) { identity.push_back(ancestor.id); }
        return identity;
    }

    // Message thread: the frame of `project` (whose revision is `revision`)
    // at `time`, from `eye`, over `reach`.
    void request(std::shared_ptr<const Project> project, std::uint64_t revision, double time, hidden::OtherEye eye) {
        // The request replaced is let go of after the lock: it may hold the
        // last reference to a project.
        std::optional<Request> replaced = Request {std::move(project), revision, time, eye};
        {
            const juce::SpinLock::ScopedLockType lock(queueLock);
            if (revision != runningRevision) { cancel.store(true); }
            std::swap(pending, replaced);
        }
        wake.signal();
    }
    std::shared_ptr<const Shown> latest() const {
        const juce::SpinLock::ScopedLockType lock(resultLock);
        return shown;
    }

private:
    struct Request {
        std::shared_ptr<const Project> project;
        std::uint64_t revision;
        double time;
        hidden::OtherEye eye;
    };
    void run() override {
        // Prepared once for each revision; a workspace kept throughout.
        std::unique_ptr<PreparedComposition> composition;
        std::uint64_t preparedRevision = 0;
        std::vector<std::vector<Id>> identities;
        hidden::Workspace workspace;
        while (!threadShouldExit()) {
            wake.wait();
            std::optional<Request> job;
            {
                const juce::SpinLock::ScopedLockType lock(queueLock);
                job = std::move(pending);
                pending.reset();
                if (job.has_value()) { runningRevision = job->revision; }
                cancel.store(false);
            }
            if (!job.has_value() || job->project == nullptr || threadShouldExit()) { continue; }
            try {
                if (composition == nullptr || preparedRevision != job->revision) {
                    composition = std::make_unique<PreparedComposition>(*job->project, 48000, &cancel, CompositionPurpose::signal, job->eye.reach);
                    // A preparation cancelled part way is not kept for the revision.
                    if (cancel.load()) {
                        composition.reset();
                        continue;
                    }
                    preparedRevision = job->revision;
                    identities.clear();
                    for (const auto& clip : composition->clips) { identities.push_back(identityOf(clip)); }
                }
                if (cancel.load() || !composition->preparationError.isEmpty()) { continue; }
                auto frame = hidden::cull(*composition, job->time, workspace, &cancel, &job->eye);
                if (frame == nullptr || cancel.load()) { continue; }
                auto result = std::make_shared<Shown>();
                result->revision = job->revision;
                result->time = job->time;
                result->frame = std::move(*frame);
                result->clips = identities;
                // The result replaced is freed after the lock.
                std::shared_ptr<const Shown> replaced = std::move(result);
                {
                    const juce::SpinLock::ScopedLockType lock(resultLock);
                    std::swap(shown, replaced);
                }
                replaced.reset();
                if (ready) { ready(); }
            } catch (...) {
                composition.reset();
            }
        }
    }

    std::function<void()> ready;
    juce::WaitableEvent wake;
    std::atomic<bool> cancel {false};
    mutable juce::SpinLock queueLock, resultLock;
    std::optional<Request> pending;
    std::uint64_t runningRevision = 0;
    std::shared_ptr<const Shown> shown;
};
}
