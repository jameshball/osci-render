#pragma once

#include <array>
#include <cmath>
#include <string_view>

namespace motion {
// Timing calibration for the display the beam drives. Every jump holds the
// beam dark for the dwell at both ends, moves it at the travel rate per unit
// of screen distance, then waits the settle time before the next stroke
// lights, so slow scopes and galvos arrive before they draw.
struct ScopeProfile {
    static constexpr double maximumDwellMicros = 1000, maximumTravelMicrosPerUnit = 2000, maximumSettleMicros = 2000;
    double dwellMicros = 12;
    double travelMicrosPerUnit = 30;
    double settleMicros = 0;

    bool operator==(const ScopeProfile&) const = default;
    bool valid() const {
        const auto within = [](double value, double maximum) { return std::isfinite(value) && value >= 0 && value <= maximum; };
        return within(dwellMicros, maximumDwellMicros) && within(travelMicrosPerUnit, maximumTravelMicrosPerUnit) && within(settleMicros, maximumSettleMicros);
    }
};

struct ScopeProfilePreset {
    std::string_view name;
    ScopeProfile profile;
};

inline constexpr std::array<ScopeProfilePreset, 3> scopeProfilePresets {{
    {"Analog scope", {12, 30, 0}},
    {"Laser (slow galvo)", {100, 400, 150}},
    {"Fast vector", {4, 10, 0}},
}};
}
