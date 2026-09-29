#pragma once

#include "MidiRecording.h"
#include "SampleClock.h"
#include "../model/Document.h"
#include "../model/MidiTakeNotes.h"
#include <functional>

namespace motion {
// Message-thread session owner. It outlives the Notes component, so closing an
// editor can request cancellation without abandoning an unacknowledged take.
class MidiRecordingSession final : private juce::Timer {
public:
    // arm must check device readiness and arm the recorder atomically with the
    // device lifecycle, so a stop cannot slip between the check and the arm and
    // leave a take that no callback will ever acknowledge. Playback started by a
    // take is stopped by the audio thread when that take ends.
    struct Transport {
        std::function<double()> rate, position;
        std::function<bool()> ready;
        std::function<void(Id)> monitor;
        std::function<bool(const MidiRecording::Config&)> arm;
    };
    MidiRecordingSession(Document& document, MidiRecording& recorder, Transport transport)
        : document(document), recorder(recorder), transport(std::move(transport)) { startTimerHz(30); }
    ~MidiRecordingSession() override {
        stopTimer();
        if (token != 0) { recorder.requestCancel(token); }
        if (job != nullptr) { job->cancelled.store(true); }
        worker.removeAllJobs(true, -1);
    }
    bool busy() const { return token != 0 || job != nullptr; }
    bool stopping() const { return stopRequested || job != nullptr; }
    Id targetId() const { return target; }
    const juce::String& message() const { return status; }
    bool hasError() const { return error; }

    juce::Result start(Id id) {
        if (busy() || recorder.state() != MidiRecording::State::idle) { return juce::Result::fail("The previous recording is still finishing."); }
        if (!transport.ready()) { return juce::Result::fail("Start an audio output device and wait for source preparation before recording."); }
        const auto& project = document.project();
        const Clip* selected = nullptr;
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) {
                if (clip.id != id) { continue; }
                if (track.kind != TrackKind::visual || clip.composition != 0 || track.locked || !trackIsAudible(project, track)) {
                    return juce::Result::fail("Choose an unlocked, audible visual clip to record notes.");
                }
                selected = &clip;
            }
        }
        if (selected == nullptr) { return juce::Result::fail("Select a visual clip to record notes."); }
        const auto timing = selected->timing(project.tempo());
        const auto rate = transport.rate();
        auto first = sampleIndex(timing.start, rate);
        auto last = sampleIndex(timing.end(), rate);
        const auto projectEnd = sampleIndex(project.duration, rate);
        if (!first || !last || !projectEnd) { return juce::Result::fail("This clip has invalid recording timing."); }
        if (static_cast<double>(*first) / rate < timing.start) { ++*first; }
        if (static_cast<double>(*last) / rate < timing.end()) { ++*last; }
        *last = std::min(*last, *projectEnd);
        if (*last <= *first) { return juce::Result::fail("This clip has no recordable duration inside the composition."); }
        MidiRecording::Config config;
        config.token = recorder.nextToken(); config.target = id;
        config.generation = document.generation(); config.revision = document.revision();
        config.firstSample = static_cast<std::uint64_t>(*first); config.endSample = static_cast<std::uint64_t>(*last);
        config.sampleRate = rate; config.sourceRate = timing.rate;
        config.sourceOffset = timing.localTime(static_cast<double>(*first) / rate);
        config.sourceBpm = selected->curveBpm(project.tempo());
        const auto position = transport.position();
        const auto start = sampleIndex(position >= timing.start && position < timing.end() ? position : timing.start, rate);
        if (!start) { return juce::Result::fail("The playhead has invalid recording timing."); }
        config.transportStart = std::clamp<std::uint64_t>(static_cast<std::uint64_t>(*start), config.firstSample, config.endSample - 1);
        if (!config.valid()) { return juce::Result::fail("Recordings need a valid source clock and a clip of at most one hour."); }
        generation = config.generation; scope = document.editingComposition();
        target = id; base = selected->midi; mapping = mappingOf(project, *selected);
        status = "Starting recording..."; error = false; cancelled = stopRequested = false;
        transport.monitor(id);
        if (!transport.arm(config)) {
            transport.monitor(0);
            return juce::Result::fail("The audio device stopped before recording could start.");
        }
        token = config.token;
        return juce::Result::ok();
    }
    void stop() {
        if (token == 0 || job != nullptr) { return; }
        stopRequested = true; recorder.requestStop(token); status = "Finishing recording...";
    }
    void cancel(juce::String reason = {}) {
        if (!busy()) { return; }
        cancelled = true; stopRequested = true;
        if (token != 0) { recorder.requestCancel(token); }
        if (job != nullptr) { job->cancelled.store(true); }
        transport.monitor(0);
        status = reason.isEmpty() ? "Recording cancelled." : reason;
        error = reason.isNotEmpty();
    }
    void poll() {
        if (!busy()) { return; }
        // Unrelated edits keep recording. Only changes that move the take's
        // beat mapping, lock the clip or replace its notes invalidate it.
        if (!cancelled && (document.generation() != generation || document.editingComposition() != scope || !targetUnchanged())) {
            cancel("Recording cancelled because its clip changed.");
        }
        if (job != nullptr) {
            if (!job->finished.load(std::memory_order_acquire)) { return; }
            auto completed = std::move(job);
            if (cancelled || completed->cancelled.load()) { base.reset(); return; }
            if (!completed->result) { status = completed->result.error; error = true; base.reset(); return; }
            if (completed->result.addedCount == 0) { status = "No notes recorded."; base.reset(); return; }
            const auto result = document.recordMidiNotes(target, base, completed->result.source, generation);
            base.reset();
            if (result.failed()) { status = result.getErrorMessage(); error = true; return; }
            const auto added = static_cast<int>(completed->result.addedCount);
            status = "Recorded " + juce::String(added) + (added == 1 ? " note." : " notes.");
            if (completed->result.ignoredControllerCount != 0) { status += " Other controller messages were not applied."; }
            return;
        }
        auto take = recorder.collect();
        if (!take) {
            if (!stopRequested && recorder.state() == MidiRecording::State::recording) { status = "Recording notes and sustain..."; }
            return;
        }
        token = 0;
        transport.monitor(0);
        if (cancelled || take->failure == MidiRecording::Failure::cancelled) { base.reset(); return; }
        if (take->failure != MidiRecording::Failure::none) {
            status = take->failure == MidiRecording::Failure::overflow ? "Recording was too large; no partial take was saved."
                : "Recording stopped because audio timing became unavailable. No notes were changed.";
            error = true; base.reset(); return;
        }
        if (take->endSample <= take->firstSample || take->events.empty()) { status = "No notes recorded."; base.reset(); return; }
        status = "Preparing recorded notes...";
        job = std::make_shared<Job>();
        const auto task = job;
        auto captured = std::make_shared<const MidiRecording::Take>(std::move(*take));
        const auto previous = base;
        worker.addJob([task, captured, previous] {
            try { task->result = MidiTakeNotes::convert(*captured, previous, &task->cancelled); }
            catch (const std::exception& e) { task->result.error = e.what(); }
            task->finished.store(true, std::memory_order_release);
        });
    }
private:
    struct Job {
        std::atomic<bool> cancelled{false}, finished{false};
        MidiTakeNotes::Result result;
    };
    struct Mapping {
        double start = 0, duration = 0, offset = 0, rate = 0, contentBpm = 0, bpm = 0;
        ClipTimeBase timeBase = ClipTimeBase::seconds;
        bool operator==(const Mapping&) const = default;
    };
    static Mapping mappingOf(const Project& project, const Clip& clip) {
        return {clip.start, clip.duration, clip.offset, clip.rate, clip.contentBpm, project.bpm, clip.timeBase};
    }
    bool targetUnchanged() const {
        const auto& project = document.project();
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) {
                if (clip.id == target) { return !track.locked && clip.midi == base && mappingOf(project, clip) == mapping; }
            }
        }
        return false;
    }
    void timerCallback() override { poll(); }
    Document& document;
    MidiRecording& recorder;
    Transport transport;
    juce::ThreadPool worker{1};
    std::shared_ptr<Job> job;
    std::shared_ptr<const MidiNotes> base;
    std::uint64_t token = 0, generation = 0;
    Mapping mapping;
    Id target = 0, scope = 0;
    bool stopRequested = false, cancelled = false, error = false;
    juce::String status;
};
}
