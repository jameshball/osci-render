#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <vector>

namespace motion {
// Immutable PCM and waveform data, constructed off the realtime thread.
// One or two channels only. PCM and pyramid payload together are limited to
// 256 MiB; the small vector/object bookkeeping is additional. Decode callbacks
// run synchronously in blocks of at most 4096 frames and must fill every frame.
class PreparedAudio {
public:
    static constexpr std::size_t maximumBytes = 256 * 1024 * 1024;
    static constexpr std::uint64_t maximumFrames = 64 * 1024 * 1024;
    static constexpr double maximumSampleRate = 384000;
    static constexpr std::size_t peakBlockFrames = 64;
    static constexpr std::size_t decodeBlockFrames = 4096;
    struct Stereo { float left = 0, right = 0; };
    struct Peak { float minimum = 0, maximum = 0; };
    struct Result {
        std::shared_ptr<const PreparedAudio> audio;
        std::string error;
        explicit operator bool() const { return audio != nullptr; }
    };
    using Decoder = std::function<bool(float* const* destination, std::size_t channels, std::size_t firstFrame, std::size_t frames)>;

    // Validates all metadata and payload arithmetic before allocating PCM or
    // invoking the decoder. Decoder failure/exception never exposes partial data.
    static Result create(double sampleRate, std::size_t channels, std::uint64_t frames, const Decoder& decode) {
        const auto error = validate(sampleRate, channels, frames);
        if (!error.empty()) {
            return { nullptr, error };
        }
        if (!decode) {
            return { nullptr, "An audio decoder is required." };
        }
        try {
            auto result = std::shared_ptr<PreparedAudio>(new PreparedAudio(sampleRate, channels, static_cast<std::size_t>(frames)));
            std::array<float*, 2> destination {};
            for (std::size_t first = 0; first < result->frames; first += decodeBlockFrames) {
                const auto count = std::min(decodeBlockFrames, result->frames - first);
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    destination[channel] = result->pcm[channel].data() + first;
                }
                if (!decode(destination.data(), channels, first, count)) {
                    return { nullptr, "Audio decoding failed. No partial source was retained." };
                }
            }
            for (auto& channel : result->pcm) {
                for (auto& sample : channel) {
                    if (!std::isfinite(sample)) {
                        sample = 0;
                    }
                }
            }
            result->buildPeaks();
            return { std::move(result), {} };
        } catch (const std::bad_alloc&) {
            return { nullptr, "Not enough memory to prepare audio." };
        } catch (...) {
            return { nullptr, "The audio decoder or waveform preparation failed." };
        }
    }

    static Result fromPlanar(double sampleRate, std::span<const std::span<const float>> channels) {
        const auto frames = channels.empty() ? 0 : channels.front().size();
        const auto error = validate(sampleRate, channels.size(), frames);
        if (!error.empty()) {
            return { nullptr, error };
        }
        for (const auto channel : channels) {
            if (channel.size() != frames) {
                return { nullptr, "All audio channels must have the same frame count." };
            }
        }
        return create(sampleRate, channels.size(), frames, [&](float* const* destination, std::size_t count, std::size_t first, std::size_t length) {
            for (std::size_t channel = 0; channel < count; ++channel) {
                std::copy_n(channels[channel].data() + first, length, destination[channel]);
            }
            return true;
        });
    }

    double sampleRate() const { return rate; }
    std::size_t channelCount() const { return pcm.size(); }
    std::size_t frameCount() const { return frames; }
    double duration() const { return static_cast<double>(frames) / rate; }

    // Linear interpolation at source time, independent of output sample rate.
    // Valid time is [0,duration). Hold the final frame through its sample cell;
    // at duration and outside the source return silence. Mono duplicates to R.
    // Finite PCM is retained without clipping (including levels beyond +/-1).
    Stereo sample(double seconds) const {
        if (!std::isfinite(seconds) || seconds < 0 || seconds >= duration()) {
            return {};
        }
        const auto position = seconds * rate;
        // The time check bounds conversion; multiplication may round to frames
        // immediately before duration, so clamp the integer index explicitly.
        const auto first = std::min(frames - 1, static_cast<std::size_t>(position));
        const auto next = std::min(frames - 1, first + 1);
        const auto fraction = std::clamp(position - static_cast<double>(first), 0.0, 1.0);
        const auto interpolate = [&](std::size_t channel) {
            // Double arithmetic avoids overflow between finite float extremes.
            return static_cast<float>(static_cast<double>(pcm[channel][first]) * (1 - fraction)
                + static_cast<double>(pcm[channel][next]) * fraction);
        };
        return { interpolate(0), interpolate(pcm.size() == 1 ? 0 : 1) };
    }

    // Exact discrete min/max over [first,last), clamped to source frames.
    // Mono waveform channel 1 duplicates channel 0, just like stereo sampling.
    // Invalid/empty queries return {0,0}. Partial edge blocks scan <=126 PCM
    // samples; interior dyadic blocks use the prebuilt min/max pyramid.
    Peak queryFrames(std::size_t channel, std::size_t first, std::size_t last) const {
        if (channel > 1 || first >= frames || first >= last) {
            return {};
        }
        channel = pcm.size() == 1 ? 0 : channel;
        last = std::min(last, frames);
        Peak result { std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest() };
        const auto include = [&](Peak peak) {
            result.minimum = std::min(result.minimum, peak.minimum);
            result.maximum = std::max(result.maximum, peak.maximum);
        };
        while (first < last && first % peakBlockFrames != 0) {
            const auto sample = pcm[channel][first++];
            include({ sample, sample });
        }
        const auto fullEnd = last / peakBlockFrames * peakBlockFrames;
        auto block = first / peakBlockFrames;
        const auto endBlock = fullEnd / peakBlockFrames;
        while (block < endBlock) {
            std::size_t level = 0, width = 1;
            while (level + 1 < pyramid[channel].size() && block % (width * 2) == 0 && width * 2 <= endBlock - block) {
                ++level;
                width *= 2;
            }
            include(pyramid[channel][level][block / width]);
            block += width;
        }
        first = std::max(first, fullEnd);
        while (first < last) {
            const auto sample = pcm[channel][first++];
            include({ sample, sample });
        }
        return result;
    }

    // Waveform pixels cover sample cells overlapping [start,end), so fractional
    // boundaries conservatively include both edge cells and preserve transients.
    Peak querySeconds(std::size_t channel, double start, double end) const {
        if (!std::isfinite(start) || !std::isfinite(end) || start >= end || end <= 0 || start >= duration()) {
            return {};
        }
        const auto first = std::min(frames - 1, static_cast<std::size_t>(std::floor(std::clamp(start, 0.0, duration()) * rate)));
        const auto last = std::max(first + 1, static_cast<std::size_t>(std::ceil(std::clamp(end, 0.0, duration()) * rate)));
        return queryFrames(channel, first, last);
    }

private:
    static std::string validate(double sampleRate, std::size_t channels, std::uint64_t frames) {
        if (!std::isfinite(sampleRate) || sampleRate < 1 || sampleRate > maximumSampleRate) {
            return "Audio sample rate must be finite and between 1 and 384000 Hz.";
        }
        if (channels < 1 || channels > 2) {
            return "Only mono and stereo audio are supported; downmix additional channels explicitly.";
        }
        if (frames == 0 || frames > maximumFrames || frames > std::numeric_limits<std::size_t>::max()) {
            return "Audio requires 1 to 67108864 frames.";
        }
        std::uint64_t bytes = frames * channels * sizeof(float);
        auto nodes = (frames + peakBlockFrames - 1) / peakBlockFrames;
        while (true) {
            bytes += nodes * channels * sizeof(Peak);
            if (nodes == 1) {
                break;
            }
            nodes = (nodes + 1) / 2;
        }
        if (bytes > maximumBytes) {
            return "Decoded audio and waveform data exceed the 256 MiB preparation budget. Shorten the source.";
        }
        return {};
    }

    PreparedAudio(double sampleRate, std::size_t channels, std::size_t count) : rate(sampleRate), frames(count), pcm(channels), pyramid(channels) {
        for (auto& channel : pcm) {
            channel.resize(frames);
        }
    }

    void buildPeaks() {
        for (std::size_t channel = 0; channel < pcm.size(); ++channel) {
            auto& levels = pyramid[channel];
            levels.emplace_back((frames + peakBlockFrames - 1) / peakBlockFrames);
            for (std::size_t block = 0; block < levels[0].size(); ++block) {
                const auto first = block * peakBlockFrames;
                const auto last = std::min(frames, first + peakBlockFrames);
                Peak peak { pcm[channel][first], pcm[channel][first] };
                for (auto frame = first + 1; frame < last; ++frame) {
                    peak.minimum = std::min(peak.minimum, pcm[channel][frame]);
                    peak.maximum = std::max(peak.maximum, pcm[channel][frame]);
                }
                levels[0][block] = peak;
            }
            while (levels.back().size() > 1) {
                const auto& previous = levels.back();
                std::vector<Peak> next((previous.size() + 1) / 2);
                for (std::size_t index = 0; index < next.size(); ++index) {
                    const auto a = previous[index * 2];
                    const auto b = previous[std::min(previous.size() - 1, index * 2 + 1)];
                    next[index] = { std::min(a.minimum, b.minimum), std::max(a.maximum, b.maximum) };
                }
                levels.push_back(std::move(next));
            }
        }
    }

    const double rate;
    const std::size_t frames;
    std::vector<std::vector<float>> pcm;
    std::vector<std::vector<std::vector<Peak>>> pyramid;
};
}
