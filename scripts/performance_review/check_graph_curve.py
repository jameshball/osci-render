#!/usr/bin/env python3
"""Check production graph lookup against a linear oracle; no product build required.

Generated fixture uses current production interpolation arithmetic and JUCE's
clamp/constants semantics. This checks lookup and boundary equivalence, not an
independent mathematical specification of smoothing. Compile/run with --run.
"""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PREFIX = r'''
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>
namespace juce {
template<class T> T jlimit(T low, T high, T value) {
    return value < low ? low : (high < value ? high : value);
}
template<class T> struct MathConstants {
    static constexpr T pi = static_cast<T>(3.141592653589793238L);
};
}
'''
TEST = r'''
float oracle(const std::vector<GraphNode>& nodes, float time, bool smooth) {
    if (nodes.empty()) { return 0; }
    if (nodes.size() == 1 || time <= (float)nodes.front().time) { return (float)nodes.front().value; }
    if (time >= (float)nodes.back().time) { return (float)nodes.back().value; }
    size_t index = 1;
    while (index < nodes.size() - 1 && time >= (float)nodes[index].time) { ++index; }
    const auto& previous = nodes[index - 1];
    const auto& next = nodes[index];
    return osci_audio::evalSmoothPowerSegment((float)previous.value, (float)next.value,
        (double)time - previous.time, next.time - previous.time, next.curve, smooth);
}
int main() {
    std::mt19937 rng(19182);
    std::uniform_real_distribution<float> values(-1, 2), times(-.1f, 1.1f), powers(-20, 20);
    std::vector<std::vector<GraphNode>> curves = {
        {}, {{0, .7, 0}}, {{0, 0, 0}, {1, 1, 0}},
        {{0, 0, 0}, {.5, 1, 0}, {1, 0, 0}},
        {{0, 0, 0}, {0, 1, 0}, {.5, 1, 0}, {.5, 0, 0}, {1, 0, 0}}
    };
    for (int count: {3, 8, 32, 128}) {
        for (int trial = 0; trial < 20; ++trial) {
            std::vector<GraphNode> nodes;
            for (int i = 0; i < count; ++i) {
                nodes.push_back({double(i / 2) / double((count - 1) / 2), values(rng), trial % 2 ? powers(rng) : 0.f});
            }
            curves.push_back(std::move(nodes));
        }
    }
    uint64_t checks = 0;
    for (const auto& nodes: curves) {
        for (bool smooth: {false, true}) {
            std::vector<float> probes;
            for (int i = 0; i < 4096; ++i) { probes.push_back(times(rng)); }
            for (const auto& node: nodes) {
                probes.push_back((float)node.time);
                probes.push_back(std::nextafter((float)node.time, -INFINITY));
                probes.push_back(std::nextafter((float)node.time, INFINITY));
            }
            for (float time: probes) {
                float expected = oracle(nodes, time, smooth);
                float actual = evaluateGraphCurve(nodes, time, smooth);
                if (std::memcmp(&expected, &actual, sizeof(float)) != 0) {
                    std::cerr << "Mismatch: nodes=" << nodes.size() << " time=" << time << " smooth=" << smooth << '\n';
                    return 1;
                }
                ++checks;
            }
        }
    }
    std::cout << "PASS " << checks << " bit-identical graph lookup checks\n";
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--compiler', default='clang++')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/performance-review/graph-curve-check')
    args = parser.parse_args()
    interpolation = (ROOT / 'Source/audio/modulation/DahdsrEnvelope.h').read_text().split('struct DahdsrParams')[0]
    interpolation = interpolation.replace('#include <JuceHeader.h>', '').replace('#pragma once', '')
    graph = (ROOT / 'Source/audio/GraphNode.h').read_text().replace('#include "modulation/DahdsrEnvelope.h"', '').replace('#pragma once', '')
    args.output.mkdir(parents=True, exist_ok=True)
    source = args.output / 'check.cpp'
    executable = args.output / 'check'
    source.write_text(PREFIX + interpolation + graph + TEST)
    if args.run:
        subprocess.run([args.compiler, '-std=c++17', '-O2', '-ffp-contract=off', str(source), '-o', str(executable)], check=True)
        subprocess.run([str(executable.resolve())], check=True)
    else:
        print(source)


if __name__ == '__main__':
    main()
