#include "../../Source/motion/render/SampleClock.h"
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void check(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}
}

int main() {
    check(motion::sampleIndex(180, 48000) == 8640000, "three-minute live and export clocks agree");
    check(motion::sampleIndex(8193.0 / 48000, 48000) == 8193, "partial export block retains its final frame");
    check(motion::sampleIndex(0, 48000) == 0, "seek to start is valid");
    check(!motion::sampleIndex(-1, 48000), "negative time is rejected");
    check(!motion::sampleIndex(1, 0), "unprepared sample rate is rejected");
    check(!motion::sampleIndex(std::numeric_limits<double>::max(), 48000), "finite input multiplication overflow is rejected before conversion");
    check(!motion::sampleIndex(1, std::numeric_limits<double>::infinity()), "infinite sample rate is rejected");
    check(!motion::sampleIndex(std::numeric_limits<double>::quiet_NaN(), 48000), "non-finite seek is rejected");
    check(motion::sampleIndex(9007199254740991.0, 1).has_value(), "largest exact clock index is accepted");
    check(!motion::sampleIndex(9007199254740992.0, 1), "inexact adjacent sample clock range is rejected");
    std::cout << "Motion sample clock contracts passed\n";
}
