#pragma once

#include "Tempo.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace motion {
enum class TimeDisplay { seconds, frames, beats };

struct TimeGrid {
    TimeDisplay display = TimeDisplay::seconds;
    double bpm = 120, frameRate = 30;
    std::shared_ptr<const std::vector<TempoChange>> tempoChanges;
    int beatsPerBar = 4;
    double snapBeats = 0.25;
    bool snapping = true;

    double snap(double seconds) const {
        if (!snapping || !std::isfinite(seconds)) { return seconds; }
        const auto subdivision = std::isfinite(snapBeats) && snapBeats > 0 ? snapBeats : 0.25;
        if (display == TimeDisplay::beats && tempoChanges != nullptr && validTempo()) {
            // Snap in beats so the grid follows every tempo change.
            const auto tempo = clock();
            const auto result = tempo.seconds(std::round(tempo.beats(seconds) / subdivision) * subdivision);
            return std::isfinite(result) ? (result == 0 ? 0.0 : result) : seconds;
        }
        const auto step = display == TimeDisplay::beats ? positiveProduct(beatSeconds(), subdivision) : frameSeconds();
        const auto units = seconds / step;
        // At this magnitude the grid is finer than representable time. Keeping
        // the input also avoids overflowing intermediate frame/beat counts.
        if (!std::isfinite(units)) { return seconds; }
        const auto result = std::round(units) * step;
        return std::isfinite(result) ? (result == 0 ? 0.0 : result) : seconds;
    }

    double tickStep(double pixelsPerSecond) const {
        const auto scale = std::isfinite(pixelsPerSecond) && pixelsPerSecond > 0 ? pixelsPerSecond : 75.0;
        const auto target = positiveQuotient(75.0, scale);
        if (display == TimeDisplay::frames) {
            const auto period = frameSeconds();
            const auto units = target / period;
            if (!std::isfinite(units)) { return niceStep(target); }
            return positiveProduct(std::ceil(niceStep(std::max(1.0, units))), period);
        }
        if (display == TimeDisplay::beats) {
            const auto period = beatSeconds();
            const auto beats = target / period;
            if (!std::isfinite(beats)) { return niceStep(target); }
            const auto bar = static_cast<double>(meter());
            const auto power = powerOfTwo(std::max(beats, std::numeric_limits<double>::denorm_min()));
            const auto step = power <= bar ? power : positiveProduct(bar, powerOfTwo(std::max(1.0, beats / bar)));
            return positiveProduct(step, period);
        }
        return niceStep(target);
    }

    // Calls visit(seconds) for each grid line from `from` to `to`, `stride`
    // seconds apart at the base tempo. In beats the lines fall on beats, so
    // they follow every tempo change. Capped so no zoom can stall a paint.
    template <typename Visit>
    void forEachTick(double from, double to, double stride, Visit&& visit) const {
        const auto musical = display == TimeDisplay::beats && tempoChanges != nullptr && validTempo();
        const auto tempo = clock();
        const auto unit = musical ? stride / beatSeconds() : stride;
        const auto start = musical ? tempo.beats(from) : from;
        if (!std::isfinite(unit) || unit <= 0 || !std::isfinite(start)) { return; }
        const auto first = std::ceil(start / unit);
        for (int index = 0; index < maximumTicks; ++index) {
            const auto position = (first + index) * unit;
            const auto time = musical ? tempo.seconds(position) : position;
            if (!std::isfinite(time) || time > to) { return; }
            visit(time);
        }
    }
    static constexpr int maximumTicks = 4000;

    std::string label(double seconds, double tickStepSeconds) const {
        if (!std::isfinite(seconds)) { return "\xE2\x80\x94"; }
        if (display == TimeDisplay::frames) { return frameLabel(seconds); }
        if (display == TimeDisplay::beats) {
            return beatLabel(seconds, tickStepSeconds < beatSeconds());
        }
        const auto step = std::isfinite(tickStepSeconds) && tickStepSeconds > 0 ? tickStepSeconds : 1.0;
        const auto digits = std::clamp(std::ceil(-std::log10(step)), 0.0, 9.0);
        return number(seconds, static_cast<int>(digits), step < 1.0e-9 || std::abs(seconds) >= 1.0e12) + "s";
    }

    std::string positionLabel(double seconds) const {
        if (!std::isfinite(seconds)) { return "\xE2\x80\x94"; }
        if (display == TimeDisplay::frames) { return frameLabel(seconds); }
        if (display == TimeDisplay::beats) { return beatLabel(seconds, true); }
        return number(seconds, 3, std::abs(seconds) >= 1.0e12) + "s";
    }

    // A span in the ruler's units: seconds, frames, or bars.beats.ticks of
    // length (a one-bar span reads 1.0.000) measured on the tempo map.
    std::string durationLabel(double start, double end) const {
        if (!std::isfinite(start) || !std::isfinite(end)) { return "\xE2\x80\x94"; }
        if (display == TimeDisplay::frames) { return number(std::round((end - start) / frameSeconds()), 0) + "f"; }
        if (display != TimeDisplay::beats) { return number(end - start, 3, std::abs(end - start) >= 1.0e12) + "s"; }
        const auto tempo = clock();
        const auto beats = tempo.beats(end) - tempo.beats(start);
        if (!std::isfinite(beats)) { return "\xE2\x80\x94"; }
        const auto parts = beatParts(beats);
        return number(parts.bars, 0) + "." + number(parts.beats, 0) + "." + tickText(parts.ticks);
    }

    // Parsing is an authoring operation. Invalid clock settings reject input;
    // unlike ruler rendering, entry must not silently substitute different units.
    std::optional<double> parsePosition(const std::string& input) const {
        if (input.size() > 128) { return std::nullopt; }
        auto text = std::string_view(input);
        const auto whitespace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
        const auto trim = [&]() {
            while (!text.empty() && whitespace(text.front())) { text.remove_prefix(1); }
            while (!text.empty() && whitespace(text.back())) { text.remove_suffix(1); }
        };
        trim();
        if (text.empty()) { return std::nullopt; }
        auto mode = display;
        if (text.back() == 's' || text.back() == 'f') {
            mode = text.back() == 's' ? TimeDisplay::seconds : TimeDisplay::frames;
            text.remove_suffix(1); trim();
        }
        const auto numeric = [](std::string_view part, bool integer, bool exponent = true) -> std::optional<long double> {
            if (part.empty()) { return std::nullopt; }
            std::size_t i = 0, digits = 0;
            const auto digit = [](char c) { return c >= '0' && c <= '9'; };
            while (i < part.size() && digit(part[i])) { ++i; ++digits; }
            if (!integer && i < part.size() && part[i] == '.') {
                ++i;
                while (i < part.size() && digit(part[i])) { ++i; ++digits; }
            }
            if (digits == 0) { return std::nullopt; }
            if (!integer && exponent && i < part.size() && (part[i] == 'e' || part[i] == 'E')) {
                ++i;
                if (i < part.size() && (part[i] == '+' || part[i] == '-')) { ++i; }
                const auto start = i;
                while (i < part.size() && digit(part[i])) { ++i; }
                if (i == start) { return std::nullopt; }
            }
            if (i != part.size()) { return std::nullopt; }
            std::istringstream stream{std::string(part)};
            stream.imbue(std::locale::classic());
            long double value = 0;
            stream >> std::noskipws >> value;
            if (stream.fail() || !std::isfinite(value) || value < 0) { return std::nullopt; }
            if (value == 0) {
                const auto mantissa = part.substr(0, part.find_first_of("eE"));
                if (mantissa.find_first_of("123456789") != std::string_view::npos) { return std::nullopt; }
            }
            return value;
        };
        const auto split = [](std::string_view value, char delimiter) {
            std::vector<std::string_view> parts;
            std::size_t first = 0;
            for (std::size_t i = 0; i <= value.size(); ++i) {
                if (i == value.size() || value[i] == delimiter) {
                    parts.push_back(value.substr(first, i - first)); first = i + 1;
                }
            }
            return parts;
        };
        long double seconds = 0;
        if (mode == TimeDisplay::seconds) {
            if (text.find(':') == std::string_view::npos) {
                const auto value = numeric(text, false);
                if (!value) { return std::nullopt; }
                seconds = *value;
            } else {
                const auto parts = split(text, ':');
                if (parts.size() < 2 || parts.size() > 3) { return std::nullopt; }
                for (std::size_t i = 0; i < parts.size(); ++i) {
                    const auto value = numeric(parts[i], i + 1 != parts.size(), false);
                    if (!value || (i != 0 && *value >= 60)) { return std::nullopt; }
                    seconds = seconds * 60 + *value;
                }
            }
        } else if (mode == TimeDisplay::frames) {
            const auto frames = numeric(text, true);
            if (!frames || !std::isfinite(frameRate) || frameRate <= 0) { return std::nullopt; }
            seconds = *frames / frameRate;
        } else if (mode == TimeDisplay::beats) {
            if (!std::isfinite(bpm) || bpm <= 0 || beatsPerBar <= 0) { return std::nullopt; }
            const auto parts = split(text, '.');
            if (parts.size() < 2 || parts.size() > 3) { return std::nullopt; }
            const auto bar = numeric(parts[0], true), beat = numeric(parts[1], true);
            const auto tick = parts.size() == 3 ? numeric(parts[2], true) : std::optional<long double>(0);
            if (!bar || !beat || !tick || *bar < 1 || *beat < 1 || *beat > beatsPerBar || *tick > 959) { return std::nullopt; }
            const auto beats = (*bar - 1) * beatsPerBar + (*beat - 1) + *tick / 960;
            seconds = tempoChanges != nullptr && validTempo() ? static_cast<long double>(Tempo(bpm, tempoChanges).seconds(static_cast<double>(beats))) : beats * (60.0L / bpm);
        } else { return std::nullopt; }
        if (!std::isfinite(seconds) || seconds > std::numeric_limits<double>::max()) { return std::nullopt; }
        const auto result = static_cast<double>(seconds);
        if (!std::isfinite(result) || (seconds > 0 && result == 0)) { return std::nullopt; }
        return result;
    }

    // The inverse of durationLabel: a length starting at `start`. Musical
    // lengths read bars.beats.ticks (1.2 is one bar and two beats) on the
    // tempo map; "s" or "f" suffixes and other displays read linear lengths.
    std::optional<double> parseDuration(const std::string& input, double start) const {
        std::string text;
        for (const auto c : input) { if (c != ' ' && c != '\t') { text += c; } }
        if (text.empty() || text.size() > 128) { return std::nullopt; }
        if (display != TimeDisplay::beats || text.back() == 's' || text.back() == 'f') { return parsePosition(text); }
        if (!validTempo() || meter() <= 0 || !std::isfinite(start)) { return std::nullopt; }
        std::vector<double> parts;
        std::string part;
        for (std::size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == '.') {
                if (part.empty() || part.size() > 9) { return std::nullopt; }
                parts.push_back(std::stod(part));
                part.clear();
            } else if (text[i] >= '0' && text[i] <= '9') {
                part += text[i];
            } else {
                return std::nullopt;
            }
        }
        if (parts.size() > 3 || (parts.size() == 3 && parts[2] > 959)) { return std::nullopt; }
        const auto beats = parts[0] * meter() + (parts.size() > 1 ? parts[1] : 0) + (parts.size() > 2 ? parts[2] / 960 : 0);
        if (!(beats > 0)) { return std::nullopt; }
        const auto tempo = clock();
        const auto length = tempo.seconds(tempo.beats(start) + beats) - start;
        return std::isfinite(length) && length > 0 ? std::optional<double>(length) : std::nullopt;
    }

private:
    static double positiveProduct(double a, double b) {
        const auto result = a * b;
        if (!std::isfinite(result)) { return std::numeric_limits<double>::max(); }
        return std::max(result, std::numeric_limits<double>::denorm_min());
    }
    static double positiveQuotient(double a, double b) {
        const auto result = a / b;
        if (!std::isfinite(result)) { return std::numeric_limits<double>::max(); }
        return std::max(result, std::numeric_limits<double>::denorm_min());
    }
    double frameSeconds() const {
        const auto value = 1.0 / frameRate;
        return std::isfinite(frameRate) && frameRate > 0 && std::isfinite(value) && value > 0 ? value : 1.0 / 30;
    }
    double beatSeconds() const {
        const auto value = 60.0 / bpm;
        return std::isfinite(bpm) && bpm > 0 && std::isfinite(value) && value > 0 ? value : 0.5;
    }
    int meter() const { return beatsPerBar > 0 ? beatsPerBar : 4; }
    static double powerOfTwo(double value) {
        const auto result = std::exp2(std::ceil(std::log2(value)));
        return std::isfinite(result) ? std::max(result, std::numeric_limits<double>::denorm_min()) : std::numeric_limits<double>::max();
    }
    static double niceStep(double target) {
        const auto decade = std::max(std::pow(10.0, std::floor(std::log10(target))), std::numeric_limits<double>::denorm_min());
        const auto units = target / decade;
        const auto multiplier = units <= 1 ? 1.0 : (units <= 2 ? 2.0 : (units <= 5 ? 5.0 : 10.0));
        return positiveProduct(decade, multiplier);
    }
    static std::string number(double value, int digits, bool scientific = false) {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        // Do not display negative zero after rounding to the visible precision.
        if (!scientific && std::abs(value) < 0.5 * std::pow(10.0, -digits)) { value = 0; }
        stream << (scientific ? std::scientific : std::fixed) << std::setprecision(digits) << value;
        return stream.str();
    }
    std::string frameLabel(double seconds) const {
        const auto frames = seconds / frameSeconds();
        return std::isfinite(frames) ? number(std::round(frames), 0) + "f" : "\xE2\x80\x94";
    }
    bool validTempo() const { return std::isfinite(bpm) && bpm > 0; }
    // The tempo map, or 120 BPM while the tempo is invalid.
    Tempo clock() const { return validTempo() ? Tempo(bpm, tempoChanges) : Tempo(120); }
    // Whole bars, beats and 960ths of a beat. The fraction rounds before the
    // breakdown, so a carry lands on the next beat or bar without converting
    // unbounded time to an integer.
    struct BeatParts {
        double bars, beats, ticks;
    };
    BeatParts beatParts(double beats) const {
        auto whole = std::floor(beats);
        auto ticks = std::round((beats - whole) * 960.0);
        if (ticks >= 960) { whole += 1; ticks = 0; }
        auto beat = std::fmod(whole, static_cast<double>(meter()));
        if (beat < 0) { beat += meter(); }
        return {std::floor(whole / meter()), beat, ticks};
    }
    static std::string tickText(double ticks) {
        const auto digits = number(ticks, 0);
        return std::string(3 - digits.size(), '0') + digits;
    }
    std::string beatLabel(double seconds, bool showTicks) const {
        const auto beats = clock().beats(seconds);
        if (!std::isfinite(beats)) { return "\xE2\x80\x94"; }
        const auto parts = beatParts(beats);
        auto text = number(parts.bars + 1, 0) + "." + number(parts.beats + 1, 0);
        if (showTicks || parts.ticks != 0) { text += "." + tickText(parts.ticks); }
        return text;
    }
};
}
