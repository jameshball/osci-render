#pragma once

#include <vector>
#include <algorithm>
#include "DahdsrEnvelope.h"

// A single node in a breakpoint graph (used by both the visual NodeGraphComponent and LFO/envelope data).
struct GraphNode {
    double time = 0.0;   // Domain position
    double value = 0.0;  // Value position
    float curve = 0.0f;  // Exponential curvature for incoming segment (-50 to +50, 0 = linear)
};

// Evaluate a piecewise breakpoint curve at a given domain position.
// Uses binary search for O(log n) segment lookup.
// Nodes must be sorted by time.
inline float evaluateGraphCurve(const std::vector<GraphNode>& nodes, float time) {
    if (nodes.empty()) return 0.0f;
    if (nodes.size() == 1) return (float)nodes[0].value;

    // Before first node
    if (time <= (float)nodes.front().time) return (float)nodes.front().value;
    // After last node
    if (time >= (float)nodes.back().time) return (float)nodes.back().value;

    // Binary search: find first node with time > input
    auto it = std::upper_bound(nodes.begin(), nodes.end(), time,
        [](float t, const GraphNode& n) { return t < (float)n.time; });

    if (it == nodes.begin()) return (float)nodes.front().value;

    const auto& prev = *(it - 1);
    const auto& next = *it;
    double elapsed = (double)time - prev.time;
    double duration = next.time - prev.time;
    return osci_audio::evalSegment((float)prev.value, (float)next.value, elapsed, duration, next.curve);
}
