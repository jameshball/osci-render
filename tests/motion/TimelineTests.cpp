#include "../../Source/motion/model/Timeline.h"
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
bool near(double a, double b) { return std::abs(a - b) < 1.0e-9; }
}

int main() {
    motion::Clip clip;
    clip.id = 1;
    clip.asset = 5;
    clip.start = 10;
    clip.duration = 10;
    auto& position = clip.properties["position.x"];
    position.setKey({0, 0, motion::Interpolation::linear});
    position.setKey({10, 100});
    check(near(position.evaluate(5), 50), "linear animation is continuous");
    check(near(position.evaluate(-1), 0) && near(position.evaluate(11), 100), "end keys hold outside the keyed range");
    const auto original = clip;
    check(clip.trim(12, 18), "trim accepts a positive range");
    check(near(clip.localTime(15), original.localTime(15)), "trimming retains content time");
    check(near(clip.properties.at("position.x").evaluate(clip.localTime(15)), 50), "trimming does not restart animation");
    const auto parts = original.split(15, 2);
    check(parts.has_value(), "split produces two instances");
    check(parts->first.asset == parts->second.asset, "split retains shared asset identity");
    check(!parts->first.contains(15) && parts->second.contains(15), "split boundary has exactly one active clip");
    check(near(parts->second.localTime(17), original.localTime(17)), "split preserves source timing");
    auto independent = parts->second;
    independent.properties["position.x"].setKey({5, 999});
    check(near(parts->first.properties.at("position.x").evaluate(5), 50), "split curves are independently editable");
    auto stretched = original;
    check(stretched.stretch(20), "stretch accepts a longer duration");
    check(near(stretched.localTime(20), original.localTime(15)), "stretch preserves normalized content position");
    motion::Track track;
    check(track.insert(parts->second) && track.insert(parts->first), "sequential clips insert in any order");
    check(track.at(14)->id == 1 && track.at(15)->id == 2 && track.at(20) == nullptr, "lookup follows half-open clip intervals");
    auto collision = original;
    collision.id = 3;
    check(!track.insert(collision) && track.clips.size() == 2, "overlap rejection is non-destructive");
    check(!original.split(10, 3) && !original.split(20, 3), "split rejects zero-length results");
    check(!clip.stretch(0) && !clip.trim(4, 4), "invalid edits are rejected");
    motion::Curve smooth;
    smooth.setKey({0, 0, motion::Interpolation::smooth});
    smooth.setKey({1, 1});
    check(near(smooth.evaluate(0.25), 0.15625), "smooth interpolation eases both ends");
    smooth.setKey({0, 0, motion::Interpolation::hold});
    check(near(smooth.evaluate(0.9), 0) && near(smooth.evaluate(1), 1), "hold changes exactly at the next key");
    smooth.setKey({0, 0, motion::Interpolation::cubic, 0, 1});
    smooth.setKey({1, 1, motion::Interpolation::smooth, 1, 0});
    check(near(smooth.evaluate(0.25), 0.25), "cubic tangents use value-per-second slopes");
    check(smooth.keyframes().size() == 2, "editing a key replaces it instead of duplicating it");
    smooth.setKeyValue(0, 2);
    check(smooth.keyframes()[0].interpolation == motion::Interpolation::cubic
        && near(smooth.keyframes()[0].outgoingSlope, 1), "value edits retain the authored interpolation and tangents");
    motion::Curve modulated(2);
    auto& modulation = modulated.modulation;
    modulation.enabled = true;
    modulation.amount = 0.5;
    check(near(modulated.evaluateBase(0.25), 2) && near(modulated.evaluate(0.25), 2.5), "sine modulation preserves authored base");
    modulation.mode = motion::ModulationMode::multiply;
    check(near(modulated.evaluate(0.25), 3), "multiplicative modulation scales the authored value");
    modulation.mode = motion::ModulationMode::add;
    modulation.tempoSync = true;
    check(near(modulated.evaluate(0.125, 120), 2.5) && near(modulated.evaluate(0.25, 60), 2.5), "tempo sync follows project beats");
    modulation.beatsPerCycle = 2;
    check(near(modulated.evaluate(0.25, 120), 2.5), "beat division controls modulation cycle duration");
    modulation.tempoSync = false;
    modulation.waveform = motion::ModulationWaveform::noiseHold;
    modulation.seed = 123;
    const auto held = modulated.evaluate(-0.75);
    check(held == modulated.evaluate(-0.25), "held noise is constant inside negative cycles");
    modulation.waveform = motion::ModulationWaveform::noiseSmooth;
    const auto smoothNoise = modulated.evaluate(-0.375);
    for (int index = 20; index >= -20; --index) {
        const auto ignored = modulated.evaluate(index * 0.17);
        check(std::isfinite(ignored), "noise remains finite while reverse scrubbing");
    }
    check(smoothNoise == modulated.evaluate(-0.375), "noise evaluation is independent of playback history");
    auto changedSeed = modulated;
    changedSeed.modulation.seed = 124;
    check(changedSeed.evaluate(-0.375) != smoothNoise, "noise seeds produce independent motion");
    check(near(modulated.evaluate(1 - 1.0e-10), modulated.evaluate(1 + 1.0e-10)), "smooth noise joins adjacent cycles continuously");
    for (int waveform = 0; waveform <= 5; ++waveform) {
        modulation.waveform = static_cast<motion::ModulationWaveform>(waveform);
        for (int index = -20; index <= 20; ++index) {
            const auto value = modulation.value(index * 0.137);
            check(std::isfinite(value) && value >= -1 && value <= 1, "every waveform stays bounded at positive and negative times");
        }
    }
    modulation.waveform = motion::ModulationWaveform::sine;
    modulation.phase = 0.25;
    check(near(modulated.evaluate(0), 2.5), "phase is measured in cycles");
    modulation.rateHz = 0;
    check(!modulation.valid() && !modulated.valid(), "invalid modulation settings invalidate their curve");
    check(modulated.evaluate(0) == 2, "invalid modulation fails safely to authored output");
    modulation.rateHz = 1000;
    check(std::isfinite(modulated.evaluate(std::numeric_limits<double>::max())), "extreme time products do not emit non-finite samples");
    modulation.rateHz = 1;
    modulation.phase = 0.25;
    modulation.amount = 1000000;
    modulation.mode = motion::ModulationMode::multiply;
    modulated.base = std::numeric_limits<double>::max();
    check(std::isfinite(modulated.evaluate(0)), "multiplication overflow falls back to finite authored output");
    std::cout << "Motion timeline contracts passed\n";
}
