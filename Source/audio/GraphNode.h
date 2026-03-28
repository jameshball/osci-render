#pragma once

#include <vector>
#include <algorithm>
#include "modulation/DahdsrEnvelope.h"

// A single node in a breakpoint graph (used by both the visual NodeGraphComponent and LFO/envelope data).
struct GraphNode {
    double time = 0.0;   // Domain position
    double value = 0.0;  // Value position
    float curve = 0.0f;  // Curvature for incoming segment (0 = linear, range depends on mode)
};

// Evaluate a piecewise breakpoint curve at a given domain position.
// Uses binary search for O(log n) segment lookup.
// Nodes must be sorted by time.
// When smooth=true, applies sinusoidal S-curve before power scaling (curves into nodes).
// When smooth=false, uses power scaling only (straight lines at power=0).
// Both modes use the same Vital-style powerScale function.
inline float evaluateGraphCurve(const std::vector<GraphNode>& nodes, float time, bool smooth = false) {
    if (nodes.empty()) return 0.0f;
    if (nodes.size() == 1) return (float)nodes[0].value;

    if (time <= (float)nodes.front().time) return (float)nodes.front().value;
    if (time >= (float)nodes.back().time) return (float)nodes.back().value;

    auto it = std::upper_bound(nodes.begin(), nodes.end(), time,
        [](float t, const GraphNode& n) { return t < (float)n.time; });

    if (it == nodes.begin()) return (float)nodes.front().value;

    const auto& prev = *(it - 1);
    const auto& next = *it;
    double elapsed = (double)time - prev.time;
    double duration = next.time - prev.time;
    return osci_audio::evalSmoothPowerSegment((float)prev.value, (float)next.value, elapsed, duration, next.curve, smooth);
}
