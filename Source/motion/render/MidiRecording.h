#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace motion {
// Single message-thread owner and single audio-thread writer. Stop and Cancel
// only request a transition; the owner cannot reuse storage until acknowledged.
// Raw channel messages are retained so note editing need not discard expression.
class MidiRecording {
public:
    enum class State { idle, armed, recording, ready, failed };
    enum class EndReason { stopped, boundary, discontinuity };
    enum class Failure { none, cancelled, unavailable, deviceChanged, discontinuity, overflow };
    struct Config {
        std::uint64_t token = 0, target = 0, generation = 0, revision = 0;
        std::uint64_t firstSample = 0, endSample = 0;
        double sampleRate = 0, sourceOffset = 0, sourceRate = 1, sourceBpm = 120;
        std::optional<std::uint64_t> transportStart;
        bool valid() const {
            return token != 0 && target != 0 && (!transportStart || (*transportStart >= firstSample && *transportStart < endSample)) && std::isfinite(sampleRate) && sampleRate >= 1 && sampleRate <= 768000
                && endSample > firstSample && static_cast<double>(endSample - firstSample) / sampleRate <= 3600
                && std::isfinite(sourceOffset) && sourceOffset >= 0 && std::isfinite(sourceRate) && sourceRate > 0
                && std::isfinite(sourceBpm) && sourceBpm > 0
                && std::isfinite(beatAt(firstSample)) && std::isfinite(beatAt(endSample)) && beatAt(endSample) <= 1000000;
        }
        double beatAt(std::uint64_t sample) const {
            const auto relative = sample >= firstSample ? static_cast<double>(sample - firstSample) : -static_cast<double>(firstSample - sample);
            return (sourceOffset + relative / sampleRate * sourceRate) * sourceBpm / 60;
        }
    };
    struct Event {
        std::uint64_t sample = 0;
        std::array<std::uint8_t, 3> bytes{};
        std::uint8_t size = 0;
    };
    struct Take {
        Config config;
        std::uint64_t firstSample = 0, endSample = 0;
        std::vector<Event> events;
        Failure failure = Failure::none;
        EndReason reason = EndReason::stopped;
    };
    static constexpr std::size_t maximumEvents = 262144;
    explicit MidiRecording(std::size_t capacity = maximumEvents)
        : capacity(std::clamp<std::size_t>(capacity, 1, maximumEvents)), events(std::make_unique<Event[]>(this->capacity)) {}

    // Message-thread token source; arming still validates monotonicity.
    std::uint64_t nextToken() const { return lastToken == std::numeric_limits<std::uint64_t>::max() ? 0 : lastToken + 1; }
    // Audio-thread only. Start is applied together with the first armed block,
    // never by separate UI play/seek writes that can straddle a callback.
    std::optional<std::uint64_t> transportStart() const {
        if (state() != State::armed || stopToken.load(std::memory_order_acquire) == config.token
            || cancelToken.load(std::memory_order_acquire) == config.token) { return {}; }
        return config.transportStart;
    }
    State state() const { return published.load(std::memory_order_acquire); }
    // Message-thread only. The token must increase even after cancelled takes.
    bool arm(const Config& next) {
        if (state() != State::idle || !next.valid() || next.token <= lastToken) { return false; }
        config = next;
        lastToken = next.token;
        stopToken.store(0, std::memory_order_relaxed);
        cancelToken.store(0, std::memory_order_relaxed);
        published.store(State::armed, std::memory_order_release);
        return true;
    }
    // Message-thread only, including delayed callbacks from older takes.
    void requestStop(std::uint64_t token) { if (token == lastToken) { stopToken.store(token, std::memory_order_release); } }
    void requestCancel(std::uint64_t token) { if (token == lastToken) { cancelToken.store(token, std::memory_order_release); } }
    // Message-thread only; failed/cancelled takes never expose partial events.
    // Allocation failure leaves the terminal take available for another attempt.
    std::optional<Take> collect() {
        const auto current = state();
        if (current != State::ready && current != State::failed) { return std::nullopt; }
        const auto finalFailure = cancelToken.load(std::memory_order_acquire) == config.token ? Failure::cancelled : failure;
        Take result{config, first, end, {}, finalFailure, reason};
        if (finalFailure == Failure::none) { result.events.assign(events.get(), events.get() + count); }
        published.store(State::idle, std::memory_order_release);
        return result;
    }

    // Audio-thread only, before any processor early return. Returns true only
    // when event() calls for this block can append. A seek flag includes same-
    // position seeks; sample continuity alone cannot detect that action.
    bool beginBlock(double rate, std::uint64_t projectSample, std::uint32_t samples, bool running, bool timingAvailable, bool seek = false) {
        blockOpen = false;
        const auto current = state();
        if (current != State::armed && current != State::recording) { return false; }
        if (current == State::armed) {
            count = 0; failure = Failure::none; reason = EndReason::stopped;
            first = end = std::clamp(projectSample, config.firstSample, config.endSample);
            expected = projectSample;
        }
        if (cancelToken.load(std::memory_order_acquire) == config.token) { finish(Failure::cancelled); return false; }
        if (stopToken.load(std::memory_order_acquire) == config.token) { finish(Failure::none); return false; }
        if (!std::isfinite(rate) || rate != config.sampleRate) { finish(Failure::deviceChanged); return false; }
        if (!timingAvailable || samples > std::numeric_limits<std::uint64_t>::max() - projectSample) { finish(Failure::unavailable); return false; }
        if (current == State::recording && (seek || projectSample != expected)) { reason = EndReason::discontinuity; finish(Failure::none); return false; }
        if (!running) { finish(current == State::armed ? Failure::unavailable : Failure::none); return false; }
        if (projectSample >= config.endSample) { reason = EndReason::boundary; finish(Failure::none); return false; }
        blockFirst = projectSample;
        blockEnd = projectSample + samples;
        expected = blockEnd;
        if (samples == 0) { return false; }
        published.store(State::recording, std::memory_order_release);
        blockOpen = true;
        return true;
    }
    // Audio-thread only; offsets must be monotonically nondecreasing. Unsupported
    // system/SysEx messages are ignored, never truncated into channel messages.
    void event(int offset, const unsigned char* bytes, int size) {
        if (!blockOpen || bytes == nullptr || (size != 2 && size != 3)) { return; }
        const auto kind = bytes[0] & 0xf0;
        const auto required = kind == 0xc0 || kind == 0xd0 ? 2 : 3;
        if (kind < 0x80 || kind > 0xe0 || size != required || bytes[1] > 127 || (size == 3 && bytes[2] > 127)) { return; }
        const auto delta = static_cast<std::uint64_t>(std::max(0, offset));
        if (delta >= blockEnd - blockFirst) { return; }
        const auto sample = blockFirst + delta;
        if (sample < config.firstSample || sample >= config.endSample || sample >= blockEnd) { return; }
        if (count != 0 && sample < events[count - 1].sample) { finish(Failure::discontinuity); return; }
        if (count == capacity) { finish(Failure::overflow); return; }
        events[count++] = {sample, {bytes[0], bytes[1], size == 3 ? bytes[2] : static_cast<std::uint8_t>(0)}, static_cast<std::uint8_t>(size)};
    }
    void endBlock() {
        if (!blockOpen) { return; }
        end = std::clamp(blockEnd, config.firstSample, config.endSample);
        blockOpen = false;
        if (blockEnd >= config.endSample) { reason = EndReason::boundary; finish(Failure::none); }
    }
    // Audio-thread callback gates still service requests. A zero-sized callback
    // does not advance time; suspension/legal gating invalidates an active pass.
    void skippedBlock(bool timingUnavailable) {
        const auto current = state();
        if (current != State::armed && current != State::recording) { return; }
        if (current == State::armed) { count = 0; first = end = config.firstSample; reason = EndReason::stopped; }
        if (cancelToken.load(std::memory_order_acquire) == config.token) { finish(Failure::cancelled); }
        else if (stopToken.load(std::memory_order_acquire) == config.token) { finish(Failure::none); }
        else if (timingUnavailable) { finish(Failure::unavailable); }
    }

    // Device lifecycle only after callbacks and other lifecycle calls are excluded. No message-thread
    // cancellation is allowed to use this as a shortcut past the audio writer.
    void deviceStopped() {
        const auto current = state();
        if (current == State::armed) { count = 0; first = end = config.firstSample; }
        if (current == State::armed || current == State::recording) {
            finish(cancelToken.load(std::memory_order_acquire) == config.token ? Failure::cancelled : Failure::deviceChanged);
        }
    }
private:
    void finish(Failure reason) {
        blockOpen = false;
        failure = reason;
        published.store(reason == Failure::none ? State::ready : State::failed, std::memory_order_release);
    }
    const std::size_t capacity;
    const std::unique_ptr<Event[]> events;
    Config config;
    std::atomic<State> published{State::idle};
    std::atomic<std::uint64_t> stopToken{0}, cancelToken{0};
    std::uint64_t lastToken = 0; // Message-thread owned.
    std::uint64_t first = 0, end = 0, expected = 0, blockFirst = 0, blockEnd = 0;
    std::size_t count = 0;
    Failure failure = Failure::none;
    EndReason reason = EndReason::stopped;
    bool blockOpen = false;
};
}
