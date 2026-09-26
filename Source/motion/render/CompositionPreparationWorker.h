#pragma once

#include "CompositionRenderer.h"

namespace motion {
// One coalesced pending request, one running job, one completed result. All
// project copies and prepared-state destruction stay off the audio thread.
class CompositionPreparationWorker : private juce::Thread {
public:
    struct Result {
        std::uint64_t revision = 0;
        std::unique_ptr<PreparedComposition> composition;
        juce::String error;
    };
    explicit CompositionPreparationWorker(std::function<void()> ready) : juce::Thread("Motion preparation"), ready(std::move(ready)) { startThread(); }
    ~CompositionPreparationWorker() override {
        signalThreadShouldExit();
        {
            const juce::SpinLock::ScopedLockType lock(mutex);
            if (running) { running->cancel.store(true); }
            if (pending) { pending->cancel.store(true); }
        }
        wake.signal();
        waitForThreadToExit(-1);
    }
    // Publisher/message thread only; previous pending work is replaced.
    std::uint64_t request(const Project& project, double sampleRate) {
        auto next = std::make_shared<Request>(project, sampleRate, ++revision);
        std::shared_ptr<Request> replaced;
        std::unique_ptr<Result> obsolete;
        {
            const juce::SpinLock::ScopedLockType lock(mutex);
            if (running) { running->cancel.store(true); }
            replaced = std::move(pending);
            pending = std::move(next);
            obsolete = std::move(completed);
        }
        wake.signal();
        return revision;
    }
    std::unique_ptr<Result> take() {
        const juce::SpinLock::ScopedLockType lock(mutex);
        return std::move(completed);
    }

private:
    struct Request {
        Request(const Project& project, double rate, std::uint64_t revision) : project(project), rate(rate), revision(revision) {}
        Project project;
        double rate;
        std::uint64_t revision;
        std::atomic<bool> cancel {false};
    };
    void run() override {
        while (!threadShouldExit()) {
            wake.wait();
            std::shared_ptr<Request> job;
            {
                const juce::SpinLock::ScopedLockType lock(mutex);
                job = std::move(pending);
                running = job;
            }
            if (!job || threadShouldExit()) { continue; }
            auto result = std::make_unique<Result>();
            result->revision = job->revision;
            try {
                result->composition = std::make_unique<PreparedComposition>(job->project, job->rate, &job->cancel);
                result->error = result->composition->preparationError;
            } catch (...) {
                result->error = "Could not prepare the composition for playback.";
            }
            std::unique_ptr<Result> obsolete;
            bool notify = false;
            {
                const juce::SpinLock::ScopedLockType lock(mutex);
                running.reset(); // job retains ownership until outside this lock.
                if (!job->cancel.load() && !threadShouldExit()) {
                    obsolete = std::move(completed);
                    completed = std::move(result);
                    notify = true;
                }
            }
            if (notify) { ready(); }
        }
    }
    std::function<void()> ready;
    juce::SpinLock mutex;
    juce::WaitableEvent wake;
    std::shared_ptr<Request> pending, running;
    std::unique_ptr<Result> completed;
    std::uint64_t revision = 0;
};
}
