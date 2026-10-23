#pragma once

#include "HiddenLineDepth.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

// The order a drawing's strokes are drawn in on a display with no brightness
// input (XY only), where the beam can't go dark: every jump from one stroke
// to the next is drawn, faintly, so the jumps should be short. Drawn in the
// order they were made, a line cut in two by what hides it would be joined
// again by the jump across the gap, and the hidden part would show.
namespace motion::hidden {
// A stroke's ends on screen; none where it can't be placed.
struct TourStroke {
    std::optional<Screen> start, end;
};
// A stroke in the order drawn, and whether it is drawn end to start.
struct TourStep {
    std::size_t stroke;
    bool reversed;
};

// The strokes in an order that keeps the jumps between them short: from the
// first, each next the one with the nearest end, drawn from that end; then
// any run of the order that is shorter drawn backwards is reversed, weighing
// at most `work` jumps however many strokes there are. The first and last
// strokes stay where they are, as they are, so the beam enters and leaves
// the drawing where it always did and the jumps between drawings hold still.
inline std::vector<TourStep> tour(const std::vector<TourStroke>& strokes, std::int64_t work = 20000) {
    const auto total = strokes.size();
    std::vector<TourStep> order;
    order.reserve(total);
    for (std::size_t stroke = 0; stroke < total; ++stroke) { order.push_back({stroke, false}); }
    if (total < 3) { return order; }
    const auto entry = [&](const TourStep& step) { return step.reversed ? strokes[step.stroke].end : strokes[step.stroke].start; };
    const auto exit = [&](const TourStep& step) { return step.reversed ? strokes[step.stroke].start : strokes[step.stroke].end; };
    // A jump that can't be placed counts as across the screen.
    const auto jump = [](const std::optional<Screen>& a, const std::optional<Screen>& b) {
        return a.has_value() && b.has_value() ? std::hypot(b->x - a->x, b->y - a->y) : 4.0;
    };
    const auto last = total - 1;
    for (std::size_t next = 1; next < last; ++next) {
        const auto from = exit(order[next - 1]);
        auto best = TourStep {order[next].stroke, false};
        auto nearest = std::numeric_limits<double>::infinity();
        for (auto other = next; other < last; ++other) {
            for (const auto reversed : {false, true}) {
                const TourStep step {order[other].stroke, reversed};
                const auto length = jump(from, entry(step));
                if (length < nearest) {
                    nearest = length;
                    best = step;
                }
            }
        }
        const auto placed = std::find_if(order.begin() + static_cast<std::ptrdiff_t>(next), order.end(), [&](const TourStep& step) { return step.stroke == best.stroke; });
        std::iter_swap(order.begin() + static_cast<std::ptrdiff_t>(next), placed);
        order[next] = best;
    }
    auto improved = true;
    while (improved && work > 0) {
        improved = false;
        for (std::size_t first = 1; first < last && work > 0; ++first) {
            const auto before = exit(order[first - 1]);
            for (auto end = first; end < last && work > 0; ++end) {
                work -= 4;
                const auto after = entry(order[end + 1]);
                const auto change = jump(before, exit(order[end])) + jump(entry(order[first]), after) - jump(before, entry(order[first])) - jump(exit(order[end]), after);
                if (change < -1.0e-9) {
                    std::reverse(order.begin() + static_cast<std::ptrdiff_t>(first), order.begin() + static_cast<std::ptrdiff_t>(end) + 1);
                    for (auto index = first; index <= end; ++index) { order[index].reversed = !order[index].reversed; }
                    improved = true;
                }
            }
        }
    }
    return order;
}
}
