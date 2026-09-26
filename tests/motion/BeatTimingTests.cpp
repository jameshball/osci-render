#include "../../Source/motion/model/Timeline.h"
#include <cstdlib>
#include <iostream>

static void check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
static bool near(double a, double b) { return std::abs(a - b) < 1e-10; }
int main() {
    motion::Clip clip;
    clip.id = 1; clip.start = 3; clip.duration = 4; clip.offset = 0.5; clip.rate = 1.25;
    clip.properties["position.x"].setKey({0.5, 0, motion::Interpolation::cubic, 0, 2});
    clip.properties["position.x"].setKey({5.5, 10, motion::Interpolation::cubic, 2, 0});
    check(clip.anchorToBeats(150), "beat assignment succeeds");
    check(near(clip.start, 7.5) && near(clip.duration, 10) && near(clip.offset, 1.25), "canonical values are beats");
    const auto same = clip.timing(150), slow = clip.timing(75);
    check(near(same.start, 3) && near(same.duration(), 4) && near(same.offset, .5) && near(same.rate, 1.25), "assignment retains source timing");
    check(near(slow.start, 6) && near(slow.duration(), 8) && near(slow.rate, .625), "tempo resolves placement and playback speed");
    check(near(clip.localTime(10, 75), 3), "source clock follows musical position");
    check(clip.curveBpm(75) == 150, "clip modulation retains content tempo");
    const auto parts = clip.split(10, 2, 75);
    check(parts.has_value(), "split resolves seconds to beats");
    check(!parts->first.contains(10, 75) && parts->second.contains(10, 75), "split half-open boundary");
    check(near(parts->second.localTime(12, 75), clip.localTime(12, 75)), "split preserves animation and MIDI offset");
    auto trimmed = clip;
    check(trimmed.trim(8, 12, 75), "trim at changed tempo");
    check(near(trimmed.localTime(10, 75), clip.localTime(10, 75)), "trim preserves content clock");
    auto stretched = clip;
    check(stretched.stretch(16, 75), "stretch at changed tempo");
    check(near(stretched.localTime(14, 75), clip.localTime(10, 75)), "stretch preserves normalized content");
    auto moved = clip;
    auto timing = moved.timing(75); timing.moveTo(20);
    check(moved.setTiming(timing, 75), "UI edits resolved seconds");
    check(near(moved.start, 25) && near(moved.offset, clip.offset) && near(moved.rate, clip.rate), "move changes only musical placement");
    const auto key = clip.properties.at("position.x").keyframes()[0];
    for (int i = 0; i < 10000; ++i) { static_cast<void>(clip.timing(i % 2 ? 63.7 : 199.3)); }
    check(clip.start == 7.5 && clip.duration == 10 && key.outgoingSlope == 2 && key.time == .5, "tempo resolution never rewrites authored content");
    motion::Clip absolute; absolute.id = 3; absolute.start = 14; absolute.duration = 2;
    motion::Track track;
    check(track.insert(clip, 75) && track.insert(absolute, 75), "mixed time domains allow touching edges");
    check(track.at(10, 75)->id == 1 && track.at(14, 75)->id == 3, "mixed lookup uses resolved timing");
    check(!track.canPlace(clip, clip.id, 60), "slower tempo detects collision with absolute clip");
    const auto before = moved.start;
    timing.setDuration(-1);
    check(!moved.setTiming(timing, 75) && moved.start == before, "invalid resolved edit is atomic");
    check(!moved.anchorToBeats(0), "invalid project tempo rejects");
    motion::Clip adjacentA, adjacentB;
    adjacentA.id = 10; adjacentA.timeBase = motion::ClipTimeBase::beats; adjacentA.start = .2; adjacentA.duration = .3;
    adjacentB = adjacentA; adjacentB.id = 11; adjacentB.start = .5;
    motion::Track adjacent;
    check(adjacent.insert(adjacentA, 90) && adjacent.insert(adjacentB, 90), "fractional beat boundaries remain adjacent");
    const auto split = adjacentA.split(.3 * (60.0 / 90), 12, 90);
    check(split.has_value(), "fractional split succeeds");
    motion::Track splitTrack;
    check(splitTrack.insert(split->first, 90) && splitTrack.insert(split->second, 90), "fractional split pieces reinsert without overlap");
    auto precise = adjacentA; precise.contentBpm = 123; precise.offset = .1;
    auto preciseTiming = precise.timing(90); preciseTiming.moveTo(10);
    check(precise.setTiming(preciseTiming, 90), "fractional move succeeds");
    check(precise.rate == 1 && precise.offset == .1 && precise.duration == .3, "pure movement cannot perturb other canonical fields");
    auto huge = adjacentA; huge.duration = 1e307;
    motion::Track invalid;
    check(!invalid.insert(huge, 1), "resolved duration overflow rejects");
    huge.duration = 1; huge.rate = 1e308; huge.contentBpm = 1;
    check(!invalid.insert(huge, 1000), "resolved rate overflow rejects");
    auto nonRound = adjacentA;
    nonRound.start = 84.93030526342605; nonRound.duration = 174.1822300471203 - nonRound.start;
    const auto resolved = nonRound.timing(60 / .17976322142219237);
    check(resolved.end() == nonRound.end() * .17976322142219237, "resolved endpoint is not reconstructed from a rounded duration");
    for (const double bpm : {150.0, 170.0}) {
        motion::Clip seconds;
        seconds.id = 20; seconds.start = bpm == 150 ? .3 : .1; seconds.duration = seconds.start;
        const auto original = seconds.timing(bpm);
        auto following = seconds; following.id = 21; following.start = original.end();
        check(seconds.anchorToBeats(bpm), "fractional seconds clip anchors inward");
        const auto anchored = seconds.timing(bpm);
        check(anchored.start >= original.start && anchored.end() <= original.end(), "anchoring never expands the original interval");
        check(near(anchored.start, original.start) && near(anchored.end(), original.end()), "inward anchoring retains placement to floating precision");
        motion::Track touching;
        check(touching.insert(seconds, bpm) && touching.insert(following, bpm), "anchored clip accepts exact-touching seconds neighbour");
        following.start = anchored.end() - 1e-8;
        check(!touching.canPlace(following, following.id, bpm), "real overlap remains rejected without tolerance");
    }
    for (const double bpm : {1.0, 63.7, 123.0, 150.0, 170.0, 999.0}) {
        for (const double first : {.1, .3, 1.0, 1e6}) {
            for (const double length : {.1, .3, 1.0, 1e-8}) {
                motion::Clip seconds; seconds.id = 22; seconds.start = first; seconds.duration = length;
                const auto original = seconds.timing(bpm);
                check(seconds.anchorToBeats(bpm), "representable fractional interval anchors");
                const auto anchored = seconds.timing(bpm);
                check(anchored.start >= original.start && anchored.end() <= original.end(), "all converted boundaries stay inward");
            }
        }
    }
    motion::Clip tiny; tiny.id = 23; tiny.start = 0; tiny.duration = std::numeric_limits<double>::denorm_min();
    check(!tiny.anchorToBeats(1) && tiny.timeBase == motion::ClipTimeBase::seconds, "unrepresentable beat interval rejects atomically");
    std::cout << "Beat timing contracts passed\n";
}
