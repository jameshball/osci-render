#pragma once

#include "PreparedPointFrames.h"

namespace motion {
struct BakeSettings {
    double duration = 5, frameRate = 60, bpm = 120;
    std::size_t pointsPerFrame = 1024;
    std::uint32_t seed = 0;

    std::uint64_t frameCount() const {
        const auto count = std::ceil(duration * frameRate);
        return std::isfinite(count) && count > 0 && count <= PreparedPointFrames::maximumFrames
            ? static_cast<std::uint64_t>(count) : 0;
    }
    std::string validate() const {
        if (!std::isfinite(duration) || duration <= 0) { return "Bake duration must be finite and positive."; }
        if (!std::isfinite(bpm) || bpm < 1 || bpm > 1000) { return "Bake tempo must be between 1 and 1000 BPM."; }
        return PreparedPointFrames::validate(frameRate, frameCount(), pointsPerFrame);
    }
};

}
