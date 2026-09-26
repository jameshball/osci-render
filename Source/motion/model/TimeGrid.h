#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

namespace motion {
enum class TimeDisplay { seconds, frames, beats };

struct TimeGrid {
    TimeDisplay display = TimeDisplay::seconds;
    double bpm = 120, frameRate = 30;
    int beatsPerBar = 4;
    double snapBeats = 0.25;
    bool snapping = true;

    double snap(double seconds) const {
        if (!snapping || !std::isfinite(seconds)) { return seconds; }
        const auto subdivision = std::isfinite(snapBeats) && snapBeats > 0 ? snapBeats : 0.25;
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
    std::string beatLabel(double seconds, bool showTicks) const {
        const auto beats = seconds / beatSeconds();
        if (!std::isfinite(beats)) { return "\xE2\x80\x94"; }
        // Round the fractional beat before decomposition, allowing a carry at
        // the next beat/bar without converting unbounded time to an integer.
        auto whole = std::floor(beats);
        auto ticks = std::round((beats - whole) * 960.0);
        if (ticks >= 960) { whole += 1; ticks = 0; }
        const auto bar = std::floor(whole / meter());
        auto beat = std::fmod(whole, static_cast<double>(meter()));
        if (beat < 0) { beat += meter(); }
        auto text = number(bar + 1, 0) + "." + number(beat + 1, 0);
        if (showTicks || ticks != 0) {
            auto fraction = number(ticks, 0);
            text += "." + std::string(3 - fraction.size(), '0') + fraction;
        }
        return text;
    }
};
}
