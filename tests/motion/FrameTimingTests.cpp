#include "../../Source/motion/model/FrameTiming.h"
#include <cstdlib>
#include <iostream>
#include <limits>

static void check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

int main() {
    const auto timing = motion::FrameTiming::create({100, 300, 100});
    check(static_cast<bool>(timing), "Variable frame timing prepares");
    check(timing.timing->duration() == 0.5 && timing.timing->averageFrameRate() == 6, "Duration is sum of delays, not a mean frame interval");
    for (const auto [time, frame] : {std::pair {0.0, 0u}, {0.099, 0u}, {0.1, 1u}, {0.399, 1u}, {0.4, 2u}, {0.499, 2u}, {0.5, 0u}, {-0.05, 2u}, {-0.5, 0u}}) {
        check(timing.timing->frameIndex(time) == frame, "Half-open frame intervals and negative loop times");
    }
    for (const auto time : {2.15, -8.85, 0.15, 0.15}) {
        check(timing.timing->frameIndex(time) == 1, "Seeking order does not affect timing");
    }
    check(timing.timing->frameIndex(std::numeric_limits<double>::max()) < 3, "Extreme finite seek remains bounded");
    check(timing.timing->frameIndex(std::numeric_limits<double>::quiet_NaN()) == 0, "Nonfinite seek has defined frame index");
    check(!motion::FrameTiming::create({}), "Empty timing rejected");
    check(!motion::FrameTiming::create({0}), "Zero delay must be normalized by decoder");
    check(!motion::FrameTiming::create({3600001}), "Oversized frame delay rejected");
    check(!motion::FrameTiming::create(std::vector<std::uint32_t>(100001, 1)), "Frame limit enforced");
    check(!motion::FrameTiming::create(std::vector<std::uint32_t>(25, 3600000)), "Total duration limit enforced");
    const auto fractional = motion::FrameTiming::create({2010, 100});
    check(fractional.timing->frameIndex(2.01) == 1, "Millisecond boundary must not round into preceding frame");
    check(fractional.timing->frameIndex(std::nextafter(2.01, 0.0)) == 0, "Immediately before a millisecond boundary remains previous frame");
    check(fractional.timing->frameIndex(std::nextafter(2.01, 3.0)) == 1, "Immediately after a millisecond boundary remains next frame");
    std::cout << "Variable frame timing contracts passed\n";
}
