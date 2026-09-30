#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace motion {
// Estimates a steady tempo, beat phase and first downbeat from mono audio.
// Onsets come from half-wave rectified log-energy flux in three bands at
// 200 frames per second; the period is chosen by autocorrelation under a
// log-tempo prior, then refined by a fine comb search over the whole piece.
// The downbeat is the beat phase (of `beatsPerBar`) whose beats carry the
// most low-band onset (kick drums), which suits most popular music.
struct TempoEstimate {
    double bpm = 0;
    double firstBeat = 0;     // seconds: earliest beat at or after 0
    double firstDownbeat = 0; // seconds: earliest bar start at or after 0
    double confidence = 0;    // 0..1: how much the chosen comb stands out
};

class TempoDetection {
public:
    static constexpr double frameRate = 200;
    static constexpr double minimumBpm = 60, maximumBpm = 200;

    static std::optional<TempoEstimate> estimate(std::span<const float> mono, double sampleRate, int beatsPerBar = 4, const std::atomic<bool>* cancel = nullptr) {
        if (!(sampleRate >= 1000) || mono.size() < static_cast<std::size_t>(sampleRate * 4) || beatsPerBar < 1) { return std::nullopt; }
        const auto flux = onsets(mono, sampleRate, cancel);
        if (flux.all.size() < 800 || cancelled(cancel)) { return std::nullopt; }
        const auto coarse = coarsePeriod(flux.all, cancel);
        if (!coarse.has_value() || cancelled(cancel)) { return std::nullopt; }
        // Fine search around the coarse period, +/- 3%.
        double bestPeriod = *coarse, bestPhase = 0, bestScore = -1;
        double total = 0;
        int count = 0;
        for (double period = *coarse * 0.97; period <= *coarse * 1.03; period += *coarse * 0.0005) {
            if (cancelled(cancel)) { return std::nullopt; }
            const auto [phase, score] = bestComb(flux.all, period);
            total += score;
            ++count;
            if (score > bestScore) { bestScore = score; bestPeriod = period; bestPhase = phase; }
        }
        auto bpm = 60 * frameRate / bestPeriod;
        // Musicians expect whole numbers: prefer one when it fits as well.
        const auto rounded = std::round(bpm);
        if (std::abs(bpm - rounded) < 0.25) {
            const auto [phase, score] = bestComb(flux.all, 60 * frameRate / rounded);
            if (score >= bestScore * 0.98) { bpm = rounded; bestPeriod = 60 * frameRate / rounded; bestPhase = phase; bestScore = std::max(bestScore, score); }
        }
        // Downbeat: the beat phase whose beats carry the most low-band onset.
        int bestBar = 0;
        double barScore = -1;
        for (int shift = 0; shift < beatsPerBar; ++shift) {
            const auto score = combSum(flux.low, bestPhase + shift * bestPeriod, bestPeriod * beatsPerBar) + 0.25 * combSum(flux.all, bestPhase + shift * bestPeriod, bestPeriod * beatsPerBar);
            if (score > barScore) { barScore = score; bestBar = shift; }
        }
        TempoEstimate result;
        result.bpm = bpm;
        const auto beatSeconds = 60 / bpm;
        result.firstBeat = std::fmod(bestPhase / frameRate, beatSeconds);
        const auto barSeconds = beatSeconds * beatsPerBar;
        result.firstDownbeat = std::fmod((bestPhase + bestBar * bestPeriod) / frameRate, barSeconds);
        const auto mean = count > 0 ? total / count : 0;
        result.confidence = bestScore > 0 ? std::clamp((bestScore - mean) / bestScore * 4, 0.0, 1.0) : 0;
        return result;
    }

private:
    struct Flux { std::vector<double> all, low; };
    static bool cancelled(const std::atomic<bool>* cancel) { return cancel != nullptr && cancel->load(std::memory_order_relaxed); }

    // One-pole low-passes split the signal at 150 Hz and 2 kHz.
    static Flux onsets(std::span<const float> mono, double sampleRate, const std::atomic<bool>* cancel) {
        const auto hop = std::max<std::size_t>(1, static_cast<std::size_t>(std::round(sampleRate / frameRate)));
        const auto coefficient = [sampleRate](double hertz) { return 1 - std::exp(-2 * 3.141592653589793 * hertz / sampleRate); };
        const auto lowCut = coefficient(150), midCut = coefficient(2000);
        double low1 = 0, low2 = 0, mid1 = 0, mid2 = 0;
        const auto frames = mono.size() / hop;
        std::vector<double> energy[3];
        for (auto& band : energy) { band.resize(frames); }
        for (std::size_t frame = 0; frame < frames; ++frame) {
            if ((frame & 1023) == 0 && cancelled(cancel)) { return {}; }
            double sums[3] {0, 0, 0};
            for (std::size_t i = frame * hop; i < (frame + 1) * hop; ++i) {
                const auto x = std::isfinite(mono[i]) ? static_cast<double>(mono[i]) : 0.0;
                low1 += lowCut * (x - low1); low2 += lowCut * (low1 - low2);
                mid1 += midCut * (x - mid1); mid2 += midCut * (mid1 - mid2);
                const auto bands = std::array<double, 3> {low2, mid2 - low2, x - mid2};
                for (int b = 0; b < 3; ++b) { sums[b] += bands[static_cast<std::size_t>(b)] * bands[static_cast<std::size_t>(b)]; }
            }
            for (int b = 0; b < 3; ++b) { energy[b][frame] = std::log(1e-9 + sums[b] / static_cast<double>(hop)); }
        }
        Flux flux;
        flux.all.assign(frames, 0);
        flux.low.assign(frames, 0);
        for (std::size_t frame = 1; frame < frames; ++frame) {
            for (int b = 0; b < 3; ++b) {
                const auto rise = std::max(0.0, energy[b][frame] - energy[b][frame - 1]);
                flux.all[frame] += rise;
                if (b == 0) { flux.low[frame] = rise; }
            }
        }
        for (auto* series : {&flux.all, &flux.low}) { adaptiveThreshold(*series); }
        return flux;
    }

    // Subtract a one-second moving mean and keep what rises above it.
    static void adaptiveThreshold(std::vector<double>& series) {
        const auto radius = static_cast<std::size_t>(frameRate / 2);
        std::vector<double> prefix(series.size() + 1, 0);
        for (std::size_t i = 0; i < series.size(); ++i) { prefix[i + 1] = prefix[i] + series[i]; }
        std::vector<double> result(series.size());
        for (std::size_t i = 0; i < series.size(); ++i) {
            const auto first = i > radius ? i - radius : 0;
            const auto last = std::min(series.size(), i + radius + 1);
            const auto mean = (prefix[last] - prefix[first]) / static_cast<double>(last - first);
            result[i] = std::max(0.0, series[i] - mean);
        }
        series = std::move(result);
    }

    // Autocorrelation with harmonic reinforcement under a log-tempo prior
    // centred on 120 BPM; returns the period in frames.
    static std::optional<double> coarsePeriod(const std::vector<double>& flux, const std::atomic<bool>* cancel) {
        const auto shortest = static_cast<std::size_t>(std::floor(60 * frameRate / maximumBpm));
        const auto longest = static_cast<std::size_t>(std::ceil(60 * frameRate / minimumBpm));
        const auto size = flux.size();
        if (size <= longest * 4) { return std::nullopt; }
        std::vector<double> correlation(longest * 4 + 2, 0);
        for (std::size_t lag = 1; lag < correlation.size(); ++lag) {
            if (cancelled(cancel)) { return std::nullopt; }
            double sum = 0;
            for (std::size_t i = lag; i < size; ++i) { sum += flux[i] * flux[i - lag]; }
            correlation[lag] = sum / static_cast<double>(size - lag);
        }
        double best = -1;
        std::size_t bestLag = 0;
        for (auto lag = shortest; lag <= longest; ++lag) {
            const auto bpm = 60 * frameRate / static_cast<double>(lag);
            const auto octave = std::log2(bpm / 120);
            const auto prior = std::exp(-0.5 * octave * octave / (0.8 * 0.8));
            const auto score = prior * (correlation[lag] + 0.5 * correlation[lag * 2] + 0.25 * correlation[lag * 4]);
            if (score > best) { best = score; bestLag = lag; }
        }
        if (bestLag == 0 || !(best > 0)) { return std::nullopt; }
        // Parabolic refinement between neighbouring lags.
        const auto left = correlation[bestLag - 1], centre = correlation[bestLag], right = correlation[bestLag + 1];
        const auto denominator = left - 2 * centre + right;
        const auto shift = denominator < 0 ? std::clamp(0.5 * (left - right) / denominator, -0.5, 0.5) : 0.0;
        return static_cast<double>(bestLag) + shift;
    }

    static double sampleAt(const std::vector<double>& series, double position) {
        if (position < 0) { return 0; }
        const auto index = static_cast<std::size_t>(position);
        if (index + 1 >= series.size()) { return 0; }
        const auto fraction = position - static_cast<double>(index);
        return series[index] * (1 - fraction) + series[index + 1] * fraction;
    }
    // Onset strength summed at every beat of a comb (with a little slack).
    static double combSum(const std::vector<double>& series, double phase, double period) {
        double sum = 0;
        for (auto position = phase; position < static_cast<double>(series.size()); position += period) {
            sum += std::max({sampleAt(series, position - 1), sampleAt(series, position), sampleAt(series, position + 1)});
        }
        return sum;
    }
    static std::pair<double, double> bestComb(const std::vector<double>& series, double period) {
        double bestPhase = 0, best = -1;
        for (double phase = 0; phase < period; phase += 0.5) {
            const auto score = combSum(series, phase, period);
            if (score > best) { best = score; bestPhase = phase; }
        }
        return {bestPhase, best};
    }
};
}
