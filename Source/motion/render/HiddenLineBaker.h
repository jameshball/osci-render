#pragma once

#include "HiddenLineFingerprint.h"
#include "HiddenLines.h"
#include <atomic>
#include <functional>
#include <thread>

namespace motion::hidden {
// Bakes a composition's hidden lines on worker threads, each with its own
// prepared copy of the project (a prepared composition is rendered by one
// thread at a time), from the bake's focus on (see Bake::claim).
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
        bake->focusOn(firstFrame);
        cancel = std::make_shared<std::atomic<bool>>(false);
        const auto cores = std::thread::hardware_concurrency();
        const auto threads = std::clamp<unsigned>(cores > 2 ? cores - 2 : 1u, 1u, 8u);
        for (unsigned index = 0; index < threads; ++index) {
            auto done = std::make_shared<std::atomic<bool>>(false);
            auto thread = std::make_unique<WorkerThread>([bake, sampleRate, flag = cancel, done] {
                run(*bake, sampleRate, [&baking = *bake] { return baking.claim(); }, *flag);
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
        bake.focusOn(0);
        onThreads(std::thread::hardware_concurrency(), [&] { run(bake, sampleRate, [&bake] { return bake.claim(); }, cancel); });
    }
    // Only the frames `times` fall in, likewise: what a look at a few frames
    // needs. For a bake nothing else is baking.
    static void bakeFrames(Bake& bake, double sampleRate, const std::vector<double>& times, const std::atomic<bool>& cancel) {
        std::vector<std::size_t> frames;
        for (const auto time : times) { frames.push_back(bake.frameOf(time)); }
        std::sort(frames.begin(), frames.end());
        frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
        std::atomic<std::size_t> taken {0};
        const auto next = [&]() -> std::optional<std::size_t> {
            const auto index = taken.fetch_add(1, std::memory_order_relaxed);
            return index < frames.size() ? std::optional(frames[index]) : std::nullopt;
        };
        onThreads(static_cast<unsigned>(std::min<std::size_t>(frames.size(), std::thread::hardware_concurrency())), [&] { run(bake, sampleRate, next, cancel); });
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

    // `work` on up to eight threads, this one among them.
    template <typename Work>
    static void onThreads(unsigned wanted, Work&& work) {
        const auto threads = std::clamp<unsigned>(wanted, 1u, 8u);
        std::vector<std::thread> helpers;
        for (unsigned index = 1; index < threads; ++index) { helpers.emplace_back(work); }
        work();
        for (auto& helper : helpers) { helper.join(); }
    }
    // Bakes the frames `next()` hands out until it has none.
    template <typename Next>
    static void run(Bake& bake, double sampleRate, Next&& next, const std::atomic<bool>& cancel) {
        if (cancel.load()) { return; }
        // A worker that fails gives its frames up, drawn whole, so the bake
        // still finishes.
        const auto giveUp = [&bake, &next](std::optional<std::size_t> frame) {
            for (; frame.has_value(); frame = next()) { bake.skip(*frame); }
        };
        std::unique_ptr<PreparedComposition> composition;
        try {
            composition = std::make_unique<PreparedComposition>(*bake.project, sampleRate, &cancel, CompositionPurpose::culling, bake.reach, bake.loudness);
        } catch (...) {
            if (!cancel.load()) { giveUp(next()); }
            return;
        }
        auto workspace = std::make_unique<Workspace>();
        // The last frame this worker culled and what it depended on: a frame
        // that depends on just the same is the same, and shared; so is one
        // the bake took over from the bake before it.
        Fingerprint lastPrint, print;
        std::shared_ptr<const Frame> last;
        while (!cancel.load(std::memory_order_relaxed)) {
            const auto frame = next();
            if (!frame.has_value()) { return; }
            try {
                const auto time = bake.timeOf(*frame);
                print.take(*composition, time);
                const auto hash = print.hash();
                if (last != nullptr && print == lastPrint) {
                    bake.publish(*frame, last, hash);
                    continue;
                }
                auto inherited = bake.inherited(hash);
                if (inherited != nullptr) {
                    bake.publish(*frame, std::move(inherited), hash);
                    continue;
                }
                std::shared_ptr<const Frame> result = cull(*composition, time, *workspace, &cancel);
                if (result == nullptr) {
                    // Cancelled mid-frame: it is free for whoever bakes next.
                    bake.release(*frame);
                    return;
                }
                last = result;
                std::swap(lastPrint, print);
                bake.publish(*frame, std::move(result), hash);
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
