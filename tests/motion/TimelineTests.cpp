#include "../../Source/motion/model/Timeline.h"
#include <cstdlib>
#include <iostream>

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
    std::cout << "Motion timeline contracts passed\n";
}
