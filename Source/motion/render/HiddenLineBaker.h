#pragma once

#include "HiddenLineFingerprint.h"
#include "HiddenLines.h"
#include <atomic>
#include <functional>
#include <thread>

namespace motion::hidden {
// Bakes a composition's hidden lines on worker threads, each with its own
// prepared copy of the project (a prepared composition is rendered by one
// thread at a time), starting at a frame and spreading out from it.
class Baker {
public:
    Baker() = default;
    ~Baker() {
        stop();
        for (auto& worker : retired) { worker.thread->waitForThreadToExit(-1); }
    }

    // Replaces any bake in progress. Its workers run at low priority, so they
    // can use every core but two without the editor, the Scope or the audio
    // waiting on them.
    void start(std::shared_ptr<Bake> bake, double sampleRate, std::size_t firstFrame) {
        stop();
        if (bake == nullptr || bake->project == nullptr || bake->complete()) { return; }
        cancel = std::make_shared<std::atomic<bool>>(false);
        const auto cores = std::thread::hardware_concurrency();
        const auto threads = std::clamp<unsigned>(cores > 2 ? cores - 2 : 1u, 1u, 8u);
        for (unsigned index = 0; index < threads; ++index) {
            auto done = std::make_shared<std::atomic<bool>>(false);
            auto thread = std::make_unique<WorkerThread>([bake, sampleRate, firstFrame, flag = cancel, done] {
                run(*bake, sampleRate, firstFrame, *flag);
                done->store(true);
            });
            thread->startThread(juce::Thread::Priority::low);
            workers.push_back({std::move(thread), done});
        }
    }
    // Stops the bake without waiting: its workers finish the step they are
    // on and are collected later, so an edit never waits for a frame.
    void stop() {
        if (cancel != nullptr) { cancel->store(true); }
        for (auto& worker : workers) { retired.push_back(std::move(worker)); }
        workers.clear();
        std::erase_if(retired, [](Worker& worker) {
            if (!worker.done->load()) { return false; }
            worker.thread->waitForThreadToExit(-1);
            return true;
        });
    }

    // Every frame, on this thread and helpers, before an export renders.
    static void bakeAll(Bake& bake, double sampleRate, const std::atomic<bool>& cancel) {
        const auto threads = std::clamp<unsigned>(std::thread::hardware_concurrency(), 1u, 8u);
        std::vector<std::thread> helpers;
        for (unsigned index = 1; index < threads; ++index) { helpers.emplace_back([&] { run(bake, sampleRate, 0, cancel); }); }
        run(bake, sampleRate, 0, cancel);
        for (auto& helper : helpers) { helper.join(); }
    }

private:
    class WorkerThread final : public juce::Thread {
    public:
        explicit WorkerThread(std::function<void()> work) : juce::Thread("Hidden line bake"), body(std::move(work)) {}
        void run() override { body(); }

    private:
        std::function<void()> body;
    };
    struct Worker {
        std::unique_ptr<WorkerThread> thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

    static void run(Bake& bake, double sampleRate, std::size_t firstFrame, const std::atomic<bool>& cancel) {
        if (cancel.load()) { return; }
        // A worker that fails gives its frames up, drawn whole, so the bake
        // still finishes.
        const auto giveUp = [&bake, firstFrame](std::optional<std::size_t> frame) {
            for (; frame.has_value(); frame = bake.claim(firstFrame)) { bake.skip(); }
        };
        std::unique_ptr<PreparedComposition> composition;
        try {
            composition = std::make_unique<PreparedComposition>(*bake.project, sampleRate, &cancel, CompositionPurpose::culling, bake.reach);
        } catch (...) {
            if (!cancel.load()) { giveUp(bake.claim(firstFrame)); }
            return;
        }
        auto workspace = std::make_unique<Workspace>();
        // The last frame this worker culled and what it depended on: a frame
        // that depends on just the same is the same, and is copied.
        Fingerprint lastPrint, print;
        std::unique_ptr<Frame> last;
        while (!cancel.load(std::memory_order_relaxed)) {
            const auto frame = bake.claim(firstFrame);
            if (!frame.has_value()) { return; }
            try {
                const auto time = bake.timeOf(*frame);
                print.take(*composition, time);
                if (last != nullptr && print == lastPrint) {
                    bake.publish(*frame, std::make_unique<Frame>(*last));
                    continue;
                }
                auto result = cull(*composition, time, *workspace, &cancel);
                if (result == nullptr) { return; }
                last = std::make_unique<Frame>(*result);
                std::swap(lastPrint, print);
                bake.publish(*frame, std::move(result));
            } catch (...) {
                last.reset();
                giveUp(frame);
                return;
            }
        }
    }

    std::shared_ptr<std::atomic<bool>> cancel;
    std::vector<Worker> workers, retired;
};
}
