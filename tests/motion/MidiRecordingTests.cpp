#include "../../Source/motion/render/MidiRecording.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
using motion::MidiRecording;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

MidiRecording::Config config(std::uint64_t token = 1, std::uint64_t first = 100, std::uint64_t end = 200, double rate = 100) {
    MidiRecording::Config value;
    value.token = token;
    value.target = 42;
    value.generation = 7;
    value.revision = 11;
    value.firstSample = first;
    value.endSample = end;
    value.sampleRate = rate;
    value.sourceOffset = .5;
    value.sourceRate = 2;
    value.sourceBpm = 150;
    return value;
}

bool begin(MidiRecording& recording, std::uint64_t sample, std::uint32_t size = 10, bool running = true, bool timing = true, bool seek = false, double rate = 100) {
    return recording.beginBlock(rate, sample, size, running, timing, seek);
}

void event(MidiRecording& recording, int offset, std::initializer_list<unsigned char> bytes) {
    recording.event(offset, bytes.begin(), static_cast<int>(bytes.size()));
}

MidiRecording::Take collect(MidiRecording& recording) {
    const auto result = recording.collect();
    check(result.has_value(), "terminal recorder state collects exactly one take");
    return *result;
}
}

int main() {
    {
        MidiRecording recording;
        const auto takeConfig = config();
        check(recording.arm(takeConfig), "valid target configuration arms recording");
        recording.requestStop(takeConfig.token);
        check(!begin(recording, 110), "stop acknowledged before the first audio callback prevents recording");
        const auto take = collect(recording);
        check(take.failure == MidiRecording::Failure::none && take.events.empty() && take.firstSample == 110 && take.endSample == 110,
            "stop-before-callback produces an acknowledged empty take at the callback position");
    }

    {
        MidiRecording recording;
        const auto takeConfig = config();
        check(recording.arm(takeConfig), "event filtering take arms");
        check(begin(recording, 100), "first block starts at configured clip boundary");
        event(recording, -4, {0x90, 60, 100});
        event(recording, 1, {0xb1, 64, 127});
        event(recording, 2, {0xe2, 0, 64});
        event(recording, 3, {0xa3, 61, 70});
        event(recording, 4, {0xd4, 55});
        event(recording, 5, {0xc5, 10});
        event(recording, 6, {0xf0, 1, 2});
        event(recording, 7, {0x90, 60});
        event(recording, 10, {0x80, 60, 0});
        recording.endBlock();
        recording.requestStop(takeConfig.token);
        check(!begin(recording, 110), "next callback acknowledges requested stop");
        const auto take = collect(recording);
        check(take.failure == MidiRecording::Failure::none && take.events.size() == 6, "channel short messages survive while SysEx and malformed messages do not");
        check(take.events[0].sample == 100 && take.events[0].size == 3 && take.events[0].bytes[0] == 0x90,
            "negative offsets clamp to the first callback sample");
        check(take.events[1].bytes[0] == 0xb1 && take.events[2].bytes[0] == 0xe2 && take.events[3].bytes[0] == 0xa3,
            "controller, pitch and poly pressure retain their exact channel bytes");
        check(take.events[4].size == 2 && take.events[4].bytes[0] == 0xd4 && take.events[4].bytes[2] == 0,
            "two-byte channel pressure retains its length without fabricated data");
        check(take.events[5].size == 2 && take.events[5].bytes[0] == 0xc5,
            "program changes retain their short-message form");
    }

    {
        MidiRecording recording;
        const auto takeConfig = config(1, 100, 120);
        check(recording.arm(takeConfig) && begin(recording, 95, 10), "a block can straddle the target clip start");
        event(recording, -3, {0x90, 60, 100});
        event(recording, 4, {0x90, 61, 100});
        event(recording, 5, {0x90, 62, 100});
        event(recording, 10, {0x90, 63, 100});
        recording.endBlock();
        check(begin(recording, 105, 15), "a second block spans the target clip end");
        event(recording, 14, {0x80, 62, 0});
        event(recording, 15, {0x80, 63, 0});
        recording.endBlock();
        const auto take = collect(recording);
        check(take.failure == MidiRecording::Failure::none && take.events.size() == 2 && take.events[0].sample == 100 && take.events[1].sample == 119,
            "only events within the half-open target clip interval are retained");
    }

    {
        MidiRecording recording;
        auto takeConfig = config(1, 1000, 2000, 100);
        takeConfig.sourceOffset = .5;
        takeConfig.sourceRate = 2;
        takeConfig.sourceBpm = 150;
        check(recording.arm(takeConfig) && begin(recording, 1000, 100), "mapping take arms and starts");
        event(recording, 50, {0x90, 60, 100});
        recording.endBlock();
        recording.requestStop(1);
        begin(recording, 1010);
        const auto take = collect(recording);
        check(std::abs(take.config.beatAt(1000) - 1.25) < 1.0e-12 && std::abs(take.config.beatAt(1050) - 3.75) < 1.0e-12,
            "take configuration maps project samples through source offset, rate and tempo");
        check(take.events.size() == 1 && take.events.front().sample == 1050, "recorded event preserves its exact project sample for later mapping");
    }

    {
        MidiRecording recording;
        check(recording.arm(config()) && begin(recording, 100), "seek discontinuity take starts");
        recording.endBlock();
        check(!begin(recording, 110, 10, true, true, true), "same-position seek ends recording even with contiguous samples");
        const auto discontinuous = collect(recording);
        check(discontinuous.failure == MidiRecording::Failure::none && discontinuous.reason == MidiRecording::EndReason::discontinuity,
            "seek discontinuity preserves the valid captured prefix and records its end reason");

        check(recording.arm(config(2)) && begin(recording, 100), "sample-rate change take starts");
        check(!begin(recording, 110, 10, true, true, false, 101), "sample-rate change is rejected before event storage");
        check(collect(recording).failure == MidiRecording::Failure::deviceChanged, "sample-rate changes terminate the take atomically");

        check(recording.arm(config(3)) && begin(recording, 100), "device stopped take starts");
        event(recording, 1, {0x90, 60, 100});
        recording.deviceStopped();
        const auto stopped = collect(recording);
        check(stopped.failure == MidiRecording::Failure::deviceChanged && stopped.events.empty(), "device stop does not expose partial events");

        check(recording.arm(config(4)), "not-running take arms");
        check(!begin(recording, 100, 10, false), "transport stopped before recording reports unavailable");
        check(collect(recording).failure == MidiRecording::Failure::unavailable, "unavailable timing produces no partial take");

        check(recording.arm(config(5, 100, 110)) && begin(recording, 100, 10), "boundary take starts");
        event(recording, 1, {0x90, 64, 100});
        recording.endBlock();
        recording.requestCancel(5);
        const auto cancelledAtBoundary = collect(recording);
        check(cancelledAtBoundary.failure == MidiRecording::Failure::cancelled && cancelledAtBoundary.events.empty()
            && cancelledAtBoundary.reason == MidiRecording::EndReason::boundary,
            "cancel after automatic boundary completion wins before collection and hides events");
    }

    {
        MidiRecording recording;
        check(recording.arm(config(1)), "zero-sized callback take arms");
        check(!begin(recording, 100, 0), "a zero-sized callback does not open an event block");
        check(recording.state() == MidiRecording::State::armed, "a zero-sized callback leaves an armed take active");
        recording.skippedBlock(false);
        check(recording.state() == MidiRecording::State::armed, "an ordinary outer callback gate does not invalidate an armed take");
        recording.requestStop(1);
        recording.skippedBlock(false);
        const auto stoppedWhileGated = collect(recording);
        check(stoppedWhileGated.failure == MidiRecording::Failure::none && stoppedWhileGated.events.empty(),
            "outer-gated callbacks acknowledge a pending stop before internal processing runs");

        check(recording.arm(config(2)) && begin(recording, 100), "active zero-sized callback take starts");
        event(recording, 2, {0x90, 60, 100});
        recording.endBlock();
        check(!begin(recording, 110, 0), "zero-sized callback leaves an active pass open for future callbacks");
        check(recording.state() == MidiRecording::State::recording, "zero-sized callback does not end an active pass");
        recording.requestCancel(2);
        recording.skippedBlock(false);
        const auto cancelledWhileGated = collect(recording);
        check(cancelledWhileGated.failure == MidiRecording::Failure::cancelled && cancelledWhileGated.events.empty(),
            "outer-gated callbacks acknowledge cancellation and hide captured events");

        check(recording.arm(config(3)), "armed suspension take starts");
        recording.skippedBlock(true);
        check(collect(recording).failure == MidiRecording::Failure::unavailable,
            "suspension or an illegal outer callback gate fails an armed take");

        check(recording.arm(config(4)) && begin(recording, 100), "active suspension take starts");
        event(recording, 1, {0x90, 61, 100});
        recording.endBlock();
        recording.skippedBlock(true);
        const auto suspended = collect(recording);
        check(suspended.failure == MidiRecording::Failure::unavailable && suspended.events.empty(),
            "suspension atomically rejects an active take instead of publishing a partial pass");
    }

    {
        MidiRecording recording(2);
        check(recording.arm(config()) && begin(recording, 100), "small-capacity take starts");
        event(recording, 0, {0x90, 60, 100});
        event(recording, 1, {0x90, 61, 100});
        event(recording, 2, {0x90, 62, 100});
        const auto overflow = collect(recording);
        check(overflow.failure == MidiRecording::Failure::overflow && overflow.events.empty(), "overflow fails atomically without partial event publication");

        check(recording.arm(config(2)) && begin(recording, 100), "out-of-order take starts");
        event(recording, 4, {0x90, 60, 100});
        event(recording, 3, {0x80, 60, 0});
        const auto unordered = collect(recording);
        check(unordered.failure == MidiRecording::Failure::discontinuity && unordered.events.empty(),
            "out-of-order callback events remain an atomic recorder failure");

        check(recording.arm(config(3)) && begin(recording, 100), "second take arms after collecting recorder failures");
        recording.requestStop(1);
        check(begin(recording, 110), "a stale stop token cannot stop a newer take");
        event(recording, 0, {0x90, 64, 100});
        recording.endBlock();
        recording.requestStop(3);
        check(!begin(recording, 120), "matching stop token finishes the newer take");
        const auto newer = collect(recording);
        check(newer.failure == MidiRecording::Failure::none && newer.events.size() == 1, "newer take survives delayed stale callbacks");
    }

    {
        auto invalid = config();
        invalid.endSample = invalid.firstSample;
        check(!invalid.valid(), "zero-length target interval cannot arm");
        invalid = config();
        invalid.sourceOffset = 1000000;
        invalid.sourceRate = 2;
        invalid.endSample = invalid.firstSample + 100;
        check(!invalid.valid(), "mapped end beyond one million beats cannot arm");
    }

    {
        MidiRecording recording(1024);
        std::atomic<bool> shutdown {false};
        std::atomic<std::uint64_t> command {0}, acknowledged {0}, stopped {0};
        std::thread audio([&] {
            std::uint64_t handled = 0;
            while (!shutdown.load(std::memory_order_acquire)) {
                const auto token = command.load(std::memory_order_acquire);
                if (token == 0 || token == handled) {
                    std::this_thread::yield();
                    continue;
                }
                if (begin(recording, 0, 16, true, true, false, 48000)) {
                    event(recording, 3, {0x90, 60, 100});
                    recording.endBlock();
                }
                acknowledged.store(token, std::memory_order_release);
                while (!shutdown.load(std::memory_order_acquire) && stopped.load(std::memory_order_acquire) != token) {
                    std::this_thread::yield();
                }
                if (!shutdown.load(std::memory_order_acquire)) {
                    begin(recording, 16, 16, true, true, false, 48000);
                    handled = token;
                }
            }
        });
        for (std::uint64_t token = 1; token <= 128; ++token) {
            auto takeConfig = config(token, 0, 48000, 48000);
            check(recording.arm(takeConfig), "owner arms each concurrent take after collection");
            command.store(token, std::memory_order_release);
            while (acknowledged.load(std::memory_order_acquire) != token) { std::this_thread::yield(); }
            recording.requestStop(token);
            stopped.store(token, std::memory_order_release);
            while (recording.state() != MidiRecording::State::ready) { std::this_thread::yield(); }
            const auto take = collect(recording);
            check(take.failure == MidiRecording::Failure::none && take.events.size() == 1 && take.events.front().sample == 3,
                "SPSC arm/event/stop/collect handshake retains exactly one event per take");
        }
        shutdown.store(true, std::memory_order_release);
        audio.join();
    }

    std::cout << "PASS: MIDI recording lifecycle contracts\n";
}
