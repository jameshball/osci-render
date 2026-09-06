#!/usr/bin/env python3
"""Check the production visualiser resampling block against its bundled Lanczos filter."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true', help='Enable AddressSanitizer and UndefinedBehaviorSanitizer')
parser.add_argument('--prepare-only', action='store_true', help='Write the extracted fixture without compiling or running')
args = parser.parse_args()
output = ROOT / 'build/performance-review/resampling-check'
output.mkdir(parents=True, exist_ok=True)
# Keep a genuinely independent pre-optimization algorithm as the numerical oracle.
reference_revision = 'ffc70ba399f9afaeefb996eb14e55a1d487270b8'
reference_path = 'modules/dsp/chowdsp_dsp_utils/Resampling/chowdsp_LanczosResampler.h'
reference = subprocess.check_output(
    ['git', 'show', f'{reference_revision}:{reference_path}'],
    cwd=ROOT / 'modules/osci_gui/third_party/chowdsp_utils', text=True)
(output / 'baseline.h').write_text(reference.replace('namespace chowdsp::ResamplingTypes', 'namespace baseline'))

renderer = (ROOT / 'modules/osci_gui/visualiser/osci_VisualiserRenderer.cpp').read_text()
marker = '#if OSCI_GUI_ENABLE_CHOWDSP_RESAMPLING\n        if (parameters.getUpsamplingEnabled())'
start = renderer.index(marker) + len('#if OSCI_GUI_ENABLE_CHOWDSP_RESAMPLING\n')
block = renderer[start:renderer.index('\n#endif', start)]
source = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <memory>
#include <vector>
#include <xsimd/xsimd.hpp>
namespace juce {
    template<class T> T jmin(T a, T b) { return std::min(a, b); }
    template<class T> struct MathConstants { static constexpr T pi = 3.1415926535897932384626433832795; };
}
#define JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(T) T(const T&) = delete; T& operator=(const T&) = delete;
#include "modules/osci_gui/third_party/chowdsp_utils/modules/dsp/chowdsp_dsp_utils/Resampling/chowdsp_BaseResampler.h"
#include "modules/osci_gui/third_party/chowdsp_utils/modules/dsp/chowdsp_dsp_utils/Resampling/chowdsp_LanczosResampler.h"
using R = chowdsp::ResamplingTypes::LanczosResampler<2048, 8>;
namespace baseline { using BaseResampler = chowdsp::ResamplingTypes::BaseResampler; }
#include "baseline.h"
using Baseline = baseline::LanczosResampler<2048, 8>;
struct Harness {
    enum class RenderMode { XY, XYZ, XYRGB };
    struct Parameters {
        bool enabled = true, sweep = false;
        bool getUpsamplingEnabled() const { return enabled; }
        bool isSweepEnabled() const { return sweep; }
    } parameters;
    bool resamplingActive = false, resamplingSweep = false;
    RenderMode resamplingRenderMode = RenderMode::XYRGB;
    const double RESAMPLE_RATIO = 6;
    using VisualiserResampler = R;
    R xResampler, yResampler, zResampler, rResampler, gResampler, bResampler;
    std::vector<float> xSamples, ySamples, zSamples, rSamples, gSamples, bSamples;
    std::vector<float> smoothedXSamples, smoothedYSamples, smoothedZSamples, smoothedRSamples, smoothedGSamples, smoothedBSamples;
    Harness() {
        for (auto* r : { &xResampler, &yResampler, &zResampler, &rResampler, &gResampler, &bResampler }) {
            r->prepare(48000, 6);
        }
    }
    void run(RenderMode mode) {
''' + block + r'''
    }
};
int main() {
    Harness h;
    std::array<std::unique_ptr<Baseline>, 6> references;
    const std::array<std::vector<float>*, 6> inputs {
        &h.xSamples, &h.ySamples, &h.zSamples, &h.rSamples, &h.gSamples, &h.bSamples
    };
    const std::array<std::vector<float>*, 6> outputs {
        &h.smoothedXSamples, &h.smoothedYSamples, &h.smoothedZSamples,
        &h.smoothedRSamples, &h.smoothedGSamples, &h.smoothedBSamples
    };
    std::mt19937 rng(82);
    size_t frames = 0, channelValues = 0, transitions = 0;
    int oldMode = -1;
    bool oldSweep = false, oldEnabled = false;
    for (int pass = 0; pass < 1500; ++pass) {
        const int n = pass % 19 == 0 ? 0 : 1 + rng() % 5000;
        const int mode = (pass / 17) % 3;
        const bool sweep = (pass / 31) % 2;
        const bool enabled = pass % 47 != 0;
        for (size_t channel = 0; channel < inputs.size(); ++channel) {
            inputs[channel]->resize(n);
            for (int i = 0; i < n; ++i) {
                // Distinct signals expose swapped X/Y/Z/R/G/B inputs or outputs.
                (*inputs[channel])[i] = std::sin((pass * 31 + i) * 0.09 * (channel + 1)) + channel * 0.031;
            }
        }
        h.parameters.enabled = enabled;
        h.parameters.sweep = sweep;
        if (enabled && (!oldEnabled || oldMode != mode || oldSweep != sweep)) {
            // The old reset did not restore wp; fresh instances are the intended reset oracle.
            for (auto& reference: references) {
                reference = std::make_unique<Baseline>();
                reference->prepare(48000, 6);
            }
            ++transitions;
        }
        const auto channelActive = [mode](size_t channel) {
            return channel < 2 || (mode == 1 && channel == 2) || (mode == 2 && channel >= 3);
        };
        std::array<std::vector<float>, 6> expected;
        size_t count = 0;
        if (enabled) {
            for (size_t channel = 0; channel < inputs.size(); ++channel) {
                if (!channelActive(channel) || (sweep && channel == 0)) {
                    continue;
                }
                // Overallocate independently of the production sizing formula.
                expected[channel].resize(n * 7 + 64);
                const auto generated = references[channel]->process(inputs[channel]->data(), expected[channel].data(), n);
                expected[channel].resize(generated);
                if (channel == (sweep ? 1u : 0u)) {
                    count = generated;
                } else {
                    assert(generated == count);
                }
            }
            if (sweep) {
                // Historical piecewise sweep interpolation, extended by the last
                // input when the filter emits chunk headroom beyond n * 6.
                for (int i = 0; i < n && expected[0].size() < count; ++i) {
                    const double first = h.xSamples[i];
                    const double next = i + 1 < n ? h.xSamples[i + 1] : first;
                    for (int step = 0; step < 6 && expected[0].size() < count; ++step) {
                        expected[0].push_back(next > first ? first + step * (next - first) / 6.0 : first);
                    }
                }
                while (expected[0].size() < count) {
                    assert(!h.xSamples.empty());
                    expected[0].push_back(h.xSamples.back());
                }
            }
        }
        h.run(static_cast<Harness::RenderMode>(mode));
        if (enabled) {
            const int nominalPoints = n * 6;
            const int pointsPerResamplingChunk = 1024 * 6;
            const int allocatedPoints = nominalPoints + (nominalPoints + pointsPerResamplingChunk - 1) / pointsPerResamplingChunk;
            assert(count <= static_cast<size_t>(allocatedPoints));
            for (size_t channel = 0; channel < outputs.size(); ++channel) {
                if (channelActive(channel)) {
                    assert(outputs[channel]->size() == count);
                    if (count != 0) {
                        assert(std::memcmp(outputs[channel]->data(), expected[channel].data(), count * sizeof(float)) == 0);
                    }
                    channelValues += count;
                }
            }
            frames += count;
        } else {
            assert(!h.resamplingActive);
        }
        oldEnabled = enabled;
        oldMode = mode;
        oldSweep = sweep;
    }
    std::printf("PASS: 1500 blocks, %zu output frames, %zu channel values compared, %zu mode/sweep/enable transitions\n",
                frames, channelValues, transitions);
}
'''
fixture = output / 'check.cpp'
fixture.write_text(source)
command = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-O1' if args.sanitize else '-O3', '-g', '-I.',
           '-Imodules/osci_gui/third_party/chowdsp_utils/modules/dsp/chowdsp_simd/third_party/xsimd/include',
           str(fixture), '-o', str(output / 'check')]
if args.sanitize:
    command += ['-fsanitize=address,undefined']
if not args.prepare_only:
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run([str(output / 'check')], cwd=ROOT, check=True)
else:
    print(fixture)
