#include "../../Source/motion/model/TimeGrid.h"
#include <cassert>
#include <iostream>

namespace {
bool close(double a, double b) { return std::abs(a - b) <= 1.0e-10 * std::max({ 1.0, std::abs(a), std::abs(b) }); }
}

int main() {
    motion::TimeGrid grid;
    assert(close(grid.snap(0.049), 1.0 / 30));
    assert(close(grid.snap(0.051), 2.0 / 30));
    assert(close(grid.snap(-0.051), -2.0 / 30));
    assert(grid.label(1.5, 0.1) == "1.5s");
    assert(grid.positionLabel(-0.0001) == "0.000s");
    assert(grid.positionLabel(1.234) == "1.234s");
    assert(grid.tickStep(75) == 1);
    assert(grid.tickStep(30) == 5);

    grid.display = motion::TimeDisplay::frames;
    grid.frameRate = 24;
    assert(grid.positionLabel(1.5) == "36f");
    assert(grid.label(-0.5, 1) == "-12f");
    assert(close(grid.snap(0.0625), 2.0 / 24));
    assert(close(grid.snap(-0.0625), -2.0 / 24));
    assert(close(grid.tickStep(1.0e6), 1.0 / 24));
    for (double scale : { 0.1, 1.0, 30.0, 75.0, 150.0, 1000.0 }) {
        const auto step = grid.tickStep(scale);
        assert(close(step * grid.frameRate, std::round(step * grid.frameRate)));
        assert(step * scale >= 75.0 - 1.0e-9);
        assert(step * scale <= std::max(250.0, scale / grid.frameRate));
    }

    grid.display = motion::TimeDisplay::beats;
    assert(grid.snap(0.0625) == 0.125);
    assert(grid.snap(-0.0625) == -0.125);
    assert(grid.positionLabel(0) == "1.1.000");
    assert(grid.positionLabel(2) == "2.1.000");
    assert(grid.positionLabel(0.125) == "1.1.240");
    assert(grid.positionLabel(-0.5) == "0.4.000");
    assert(grid.positionLabel(-2) == "0.1.000");
    assert(grid.positionLabel(-2.5) == "-1.4.000");
    assert(grid.positionLabel(0.5 - 0.00001) == "1.2.000");
    assert(grid.label(0, 0.5) == "1.1");
    assert(grid.label(0, 0.125) == "1.1.000");
    grid.bpm = 60;
    grid.beatsPerBar = 3;
    assert(grid.positionLabel(3) == "2.1.000");
    assert(grid.positionLabel(-1) == "0.3.000");
    assert(grid.snap(0.125) == 0.25);
    assert(grid.tickStep(30) == 3);
    assert(grid.tickStep(15) == 6);
    assert(grid.tickStep(300) == 0.25);
    grid.snapping = false;
    for (auto display : { motion::TimeDisplay::seconds, motion::TimeDisplay::frames, motion::TimeDisplay::beats }) {
        grid.display = display;
        assert(grid.snap(-0.12345) == -0.12345);
    }

    grid.snapping = true;
    const auto huge = std::numeric_limits<double>::max();
    const auto tiny = std::numeric_limits<double>::denorm_min();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    for (auto display : { motion::TimeDisplay::seconds, motion::TimeDisplay::frames, motion::TimeDisplay::beats }) {
        grid.display = display;
        for (double clock : { -1.0, 0.0, tiny, 120.0, huge, nan, inf }) {
            grid.frameRate = clock;
            grid.bpm = clock;
            grid.beatsPerBar = std::numeric_limits<int>::max();
            for (double value : { -huge, -1.0, -tiny, 0.0, tiny, 1.0, huge }) {
                assert(std::isfinite(grid.snap(value)));
                assert(!grid.label(value, tiny).empty());
                assert(!grid.positionLabel(value).empty());
                const auto step = grid.tickStep(value);
                assert(std::isfinite(step) && step > 0);
            }
        }
        assert(std::isnan(grid.snap(nan)));
        assert(grid.snap(inf) == inf);
        assert(grid.positionLabel(nan) == "\xE2\x80\x94");
    }
    grid.display = motion::TimeDisplay::beats;
    grid.bpm = -1;
    grid.beatsPerBar = 0;
    grid.snapBeats = nan;
    assert(grid.positionLabel(2) == "2.1.000");
    assert(grid.snap(0.0625) == 0.125);
    std::cout << "TimeGrid tests passed\n";
}
