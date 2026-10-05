#pragma once

#include "LiveMidiPerformance.h"
#include <array>

namespace motion {
// Audio-thread-owned live MIDI routes. Each armed track plays its own voices
// through its clip, filtered to one channel (or any), and the beam planner
// draws them inside the composition alongside every other layer.
struct LiveMidiRoute {
    std::uint64_t track = 0;
    int channel = 0; // 0: any channel
    LiveMidiPerformance performance;
};

struct LiveMidiInputs {
    static constexpr std::size_t maximumRoutes = 8;
    std::array<LiveMidiRoute, maximumRoutes> routes;
    std::size_t count = 0;
    // Live sample clock of the oscillator sample the planner is drawing.
    std::int64_t clockOffset = 0;

    bool sounding(std::uint64_t clock) const {
        for (std::size_t index = 0; index < count; ++index) {
            if (routes[index].performance.activeCount(clock) > 0) { return true; }
        }
        return false;
    }
    void reset() {
        for (auto& route : routes) { route.performance.reset(); }
    }
    // Channel messages reach every route listening to their channel.
    bool handle(const unsigned char* data, int size, std::uint64_t sample) {
        if (data == nullptr || size < 1) { return false; }
        const auto channel = (data[0] & 15) + 1;
        bool changed = false;
        for (std::size_t index = 0; index < count; ++index) {
            auto& route = routes[index];
            if (route.channel == 0 || route.channel == channel) { changed = route.performance.handle(data, size, sample) || changed; }
        }
        return changed;
    }
};
}
