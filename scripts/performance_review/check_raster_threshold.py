#!/usr/bin/env python3
"""Check production raster threshold caching, float bits and RNG consumption.

Extracts the current method/cache entry and compares with the original uncached
pow predicate. The deterministic RNG stand-in checks identical draw order/state;
it is not a statistical test of JUCE's RNG. Compile and execute with --run.
"""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PREFIX = r'''
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
struct Rng {
    uint64_t seed = 1234567;
    float nextFloat() {
        seed = seed * 6364136223846793005ULL + 1;
        return (seed >> 40) * (1.0f / 16777216.0f);
    }
};
struct Before {
    Rng rng;
    bool isOverThreshold(double pixel, double power) {
        float threshold = std::pow(pixel, power);
        return pixel > 0.2 && rng.nextFloat() < threshold;
    }
};
'''
TEST = r'''
int main() {
    Candidate after;
    Before before;
    std::mt19937 gen(123);
    std::uniform_real_distribution<float> powers(1, 11);
    uint64_t checks = 0;
    for (int round = 0; round < 2000; ++round) {
        const float power = round % 2 == 0 ? 6.f : powers(gen);
        for (bool inverted: {false, true}) {
            for (int byte = 0; byte < 256; ++byte) {
                float pixel = byte / 255.0f;
                if (inverted && pixel > 0) { pixel = 1 - pixel; }
                const bool expected = before.isOverThreshold(pixel, power);
                if (expected != after.isOverThreshold(pixel, power) || before.rng.seed != after.rng.seed) {
                    std::fprintf(stderr, "Predicate/RNG mismatch: byte=%d inverted=%d power=%g\n", byte, inverted, power);
                    return 1;
                }
                if (pixel > 0.2) {
                    const float threshold = std::pow(static_cast<double>(pixel), static_cast<double>(power));
                    const float actual = after.thresholdCache[static_cast<int>(pixel * 255.0 + 0.5)].threshold;
                    if (std::memcmp(&threshold, &actual, sizeof(float)) != 0) {
                        std::fprintf(stderr, "Threshold bits mismatch\n");
                        return 1;
                    }
                }
                ++checks;
            }
        }
    }
    std::printf("PASS %llu predicate/RNG checks and eligible threshold bits; cache bytes %zu\n",
                static_cast<unsigned long long>(checks), sizeof(after.thresholdCache));
}
'''


def declaration(source, marker):
    start = source.index(marker)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--compiler', default='clang++')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/performance-review/raster-threshold-check')
    args = parser.parse_args()
    header = (ROOT / 'Source/parser/img/ImageParser.h').read_text()
    source = (ROOT / 'Source/parser/img/ImageParser.cpp').read_text()
    entry = declaration(header, 'struct ThresholdEntry')
    cache_start = header.index('std::array<ThresholdEntry,')
    cache = header[cache_start:header.index(';', cache_start) + 1]
    method = declaration(source, 'bool ImageParser::isOverThreshold')
    signature = method[:method.index('{')].replace('ImageParser::', '').strip()
    fixture = PREFIX + 'struct Candidate { Rng rng;\n' + entry + ';\n' + cache + '\n' + signature + ';\n};\n'
    fixture += method.replace('ImageParser::', 'Candidate::') + '\n' + TEST
    args.output.mkdir(parents=True, exist_ok=True)
    cpp, executable = args.output / 'check.cpp', args.output / 'check'
    cpp.write_text(fixture)
    if args.run:
        subprocess.run([args.compiler, '-std=c++17', '-O2', '-ffp-contract=off', str(cpp), '-o', str(executable)], check=True)
        subprocess.run([str(executable.resolve())], check=True)
    else:
        print(cpp)


if __name__ == '__main__':
    main()
