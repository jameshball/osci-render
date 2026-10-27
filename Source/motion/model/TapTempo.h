#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace motion {
// Tap tempo: the median of the last few tap intervals, to 0.1 BPM. A pause
// longer than two seconds starts a new count.
class TapTempo {
public:
    static constexpr std::size_t remembered = 8;
    std::optional<double> tap(double seconds) {
        if (!std::isfinite(seconds)) { return std::nullopt; }
        if (!taps.empty() && (seconds - taps.back() > 2.0 || seconds <= taps.back())) { taps.clear(); }
        taps.push_back(seconds);
        if (taps.size() > remembered) { taps.erase(taps.begin()); }
        if (taps.size() < 3) { return std::nullopt; }
        std::vector<double> intervals;
        for (std::size_t i = 1; i < taps.size(); ++i) { intervals.push_back(taps[i] - taps[i - 1]); }
        std::nth_element(intervals.begin(), intervals.begin() + static_cast<std::ptrdiff_t>(intervals.size() / 2), intervals.end());
        const auto bpm = 60 / intervals[intervals.size() / 2];
        if (!(bpm >= 1 && bpm <= 1000)) { return std::nullopt; }
        return std::round(bpm * 10) / 10;
    }
    std::size_t count() const { return taps.size(); }
    void reset() { taps.clear(); }

private:
    std::vector<double> taps;
};
}
