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
    {
        motion::Curve curve;
        const auto authored = 166.4 - 128.0;
        const auto restored = std::stod("38.40000000000001");
        curve.setKey({restored, 0, motion::Interpolation::cubic, 2, -3});
        curve.setKeyValue(authored, 90);
        check(curve.keyframes().size() == 1, "decimal round-trip edit replaces the existing source-time key");
        check(curve.keyframes().front().time == restored && curve.keyframes().front().value == 90,
            "round-trip edit retains the authored time and updates its value");
        check(curve.keyframes().front().interpolation == motion::Interpolation::cubic
            && curve.keyframes().front().incomingSlope == 2 && curve.keyframes().front().outgoingSlope == -3,
            "near-time value edit preserves interpolation and tangents");
        curve.setKey({std::nextafter(restored, 100.0), 45});
        check(curve.keyframes().size() == 1 && curve.keyframes().front().value == 45,
            "key insertion also replaces a floating-point alias");
        curve.setKey({restored + 1e-7, 30});
        check(curve.keyframes().size() == 2, "distinct subframe keys are not quantised together");
        check(curve.removeKey(authored) && curve.keyframes().size() == 1, "near-time deletion removes the intended key only");
        check(!curve.removeKey(std::numeric_limits<double>::quiet_NaN()), "invalid key deletion is harmless");
        motion::Curve origin;
        origin.setKey({0, 1}); origin.setKeyValue(1e-16, 2);
        check(origin.keyframes().size() == 1 && origin.keyframes().front().time == 0, "near-zero arithmetic residue does not add a duplicate key");
    }
    {
        // The parent plays composition seconds 4..24 at twice normal speed.
        // The child is active at 2..8 and starts its own content at 7.
        const motion::ClipTiming instance(10, 20, 4, 2);
        const motion::ClipTiming child(2, 8, 7, .5);
        const auto nested = child.nestedIn(instance);
        check(nested.has_value(), "partially trimmed child remains visible");
        check(near(nested->start, 10) && near(nested->end(), 12), "instance trim clips child interval");
        check(near(nested->offset, 8) && near(nested->rate, 1), "trim advances content and multiplies source speed");
        motion::Curve animation;
        animation.setKey({7, 0, motion::Interpolation::linear});
        animation.setKey({10, 30, motion::Interpolation::linear});
        for (int frame = 0; frame < 120; ++frame) {
            const auto time = 10 + frame / 60.0;
            check(near(nested->localTime(time), child.localTime(instance.localTime(time))), "nested clocks agree at every frame");
            check(near(animation.evaluate(nested->localTime(time)), animation.evaluate(child.localTime(instance.localTime(time)))), "child keys retain source-time meaning");
        }
        const motion::ClipTiming outer(30, 50, 9, .5);
        const auto twice = nested->nestedIn(outer);
        check(twice.has_value() && near(twice->start, 32) && near(twice->end(), 36), "second instance preserves inner visible interval");
        check(near(twice->localTime(34), child.localTime(instance.localTime(outer.localTime(34)))), "two nested speed mappings compose");
        check(!motion::ClipTiming(0, 4).nestedIn(instance).has_value(), "touching inactive child has no visible duration");
        check(!motion::ClipTiming(24, 26).nestedIn(instance).has_value(), "right boundary is exclusive");
        check(!motion::ClipTiming(0, 5, 0, 0).nestedIn(instance).has_value(), "invalid child speed rejects before mapping");
        check(!child.nestedIn(motion::ClipTiming(0, 5, 0, std::numeric_limits<double>::infinity())).has_value(), "invalid instance speed rejects");
        check(!motion::ClipTiming(0, 5, 0, 1e308).nestedIn(motion::ClipTiming(0, 5, 0, 1e308)).has_value(), "composed speed overflow rejects");
        motion::Clip musical; musical.id = 200; musical.start = 2; musical.duration = 6; musical.offset = 7; musical.rate = .5;
        check(musical.anchorToBeats(motion::Tempo(120)), "nested fixture has beat timing");
        const auto musicalNested = musical.timing(motion::Tempo(120)).nestedIn(instance);
        check(musicalNested.has_value() && near(musicalNested->localTime(11), nested->localTime(11)), "beat and second children use the same resolved mapping");
    }

    {
        motion::Clip first; first.id = 101; first.start = 2; first.duration = 2;
        first.offset = 7; first.properties["position.x"].setKey({7, 42});
        motion::Clip second = first; second.id = 102; second.start = 5;
        check(second.anchorToBeats(motion::Tempo(120)), "selection fixture includes beat-based clip");
        motion::Track track; track.id = 1;
        check(track.insert(first, motion::Tempo(120)) && track.insert(second, motion::Tempo(120)), "selection fixture placement");
        std::vector<motion::Track> tracks {track, motion::Track{}};
        tracks[1].id = 2;
        check(motion::moveClips(tracks, {101, 102}, 3, 1, motion::Tempo(120)), "selection moves across tracks atomically");
        check(tracks[0].clips.empty() && tracks[1].clips.size() == 2, "every selected clip moves");
        check(near(tracks[1].clips[0].timing(motion::Tempo(120)).start, 5) && near(tracks[1].clips[1].timing(motion::Tempo(120)).start, 8), "mixed time bases preserve shared second displacement");
        check(near(tracks[1].clips[0].localTime(6, motion::Tempo(120)), first.localTime(3, motion::Tempo(120))), "move carries source timing and animation");
        check(tracks[1].clips[0].properties.at("position.x").evaluate(7) == 42, "move preserves authored curves");
        const auto reject = [&](const std::vector<motion::Id>& ids, double delta, int rows) {
            check(!motion::moveClips(tracks, ids, delta, rows, motion::Tempo(120)), "invalid selection move rejected");
            check(tracks[0].clips.empty() && tracks[1].clips.size() == 2
                && near(tracks[1].clips[0].timing(motion::Tempo(120)).start, 5), "rejected move leaves all tracks unchanged");
        };
        reject({101, 102}, -6, 0);
        reject({101, 102}, 0, 1);
        reject({101, 999}, 1, 0);
        reject({101, 101}, 1, 0);
        reject({101}, 3, 0); // collides with the unselected second clip
        reject({101}, std::numeric_limits<double>::infinity(), 0);
        tracks[0].locked = true;
        reject({101, 102}, 0, -1);
        tracks[0].locked = false;
        tracks[1].locked = true;
        reject({101, 102}, 1, 0);
        tracks[1].locked = false;
        tracks[0].kind = motion::TrackKind::audio;
        reject({101, 102}, 0, -1);
        auto adjacent = first; adjacent.id = 103; adjacent.start = first.end();
        motion::Track touching; touching.id = 3;
        check(touching.insert(first, motion::Tempo(120)) && touching.insert(adjacent, motion::Tempo(120)), "adjacent selection fixture");
        std::vector<motion::Track> pair {touching};
        check(motion::moveClips(pair, {103, 101}, 1, 0, motion::Tempo(120)), "adjacent selected clips do not collide with their old positions");
        check(pair[0].clips[0].id == 101 && pair[0].clips[1].id == 103
            && pair[0].clips[0].end() == pair[0].clips[1].start, "batch insertion remains ordered and exactly adjacent");

    }
    {
        const auto sameTrack = [](const motion::Track& left, const motion::Track& right) {
            if (left.locked != right.locked || left.clips.size() != right.clips.size()) { return false; }
            for (std::size_t index = 0; index < left.clips.size(); ++index) {
                const auto& a = left.clips[index];
                const auto& b = right.clips[index];
                if (a.id != b.id || a.start != b.start || a.duration != b.duration || a.offset != b.offset
                    || a.rate != b.rate || a.timeBase != b.timeBase || a.contentBpm != b.contentBpm) { return false; }
            }
            return true;
        };
        motion::Track track;
        track.id = 4;
        motion::Clip earlier; earlier.id = 201; earlier.start = 0; earlier.duration = 2;
        motion::Clip selected; selected.id = 202; selected.start = 5; selected.duration = 2; selected.offset = .25; selected.rate = 1.5;
        selected.properties["position.x"].setKey({.25, 1, motion::Interpolation::cubic, 2, -3});
        motion::Clip downstream; downstream.id = 203; downstream.start = 10; downstream.duration = 2; downstream.offset = 3; downstream.rate = .5;
        downstream.properties["position.x"].setKey({3, 4, motion::Interpolation::linear});
        auto final = downstream; final.id = 204; final.start = 15;
        check(track.insert(earlier, motion::Tempo(120)) && track.insert(selected, motion::Tempo(120)) && track.insert(downstream, motion::Tempo(120)) && track.insert(final, motion::Tempo(120)), "ripple trim fixture placement");
        const auto original = track;
        check(motion::rippleTrim(track, 202, true, 1, motion::Tempo(120)), "leading ripple trim succeeds");
        check(near(track.clips[0].start, 0) && near(track.clips[1].start, 5) && near(track.clips[1].duration, 1)
            && near(track.clips[1].offset, 1.75), "leading trim keeps placement and advances source offset");
        check(near(track.clips[2].start, 9) && near(track.clips[3].start, 14), "leading trim shifts downstream clips left");
        check(near(track.clips[2].timing(motion::Tempo(120)).start - track.clips[1].timing(motion::Tempo(120)).end(), 3), "leading trim preserves the downstream gap");
        check(track.clips[0].start == original.clips[0].start && track.clips[2].offset == original.clips[2].offset
            && track.clips[2].rate == original.clips[2].rate && track.clips[2].properties.at("position.x").keyframes()[0].time == 3,
            "ripple movement leaves earlier and downstream source clocks and keys intact");
        check(track.clips[1].properties.at("position.x").keyframes()[0].time == .25,
            "ripple trim does not alter selected animation keys");

        track = original;
        check(motion::rippleTrim(track, 202, false, 2, motion::Tempo(120)), "trailing ripple trim succeeds");
        check(near(track.clips[1].start, 5) && near(track.clips[1].duration, 4) && near(track.clips[1].offset, .25),
            "trailing trim changes only selected duration");
        check(near(track.clips[2].start, 12) && near(track.clips[3].start, 17), "trailing trim shifts downstream clips right");
        check(near(track.clips[2].timing(motion::Tempo(120)).start - track.clips[1].timing(motion::Tempo(120)).end(), 3), "trailing trim preserves the downstream gap");

        track = original;
        check(motion::rippleTrim(track, 202, true, -.5, motion::Tempo(120)), "leading ripple extension accepts a negative source offset");
        check(near(track.clips[1].start, 5) && near(track.clips[1].duration, 2.5) && near(track.clips[1].offset, -.5),
            "leading ripple extension keeps placement and permits a negative source offset");
        check(near(track.clips[2].start, 10.5) && near(track.clips[3].start, 15.5), "leading extension shifts downstream clips right");

        motion::Track mixed;
        mixed.id = 5;
        auto absolute = selected; absolute.id = 211; absolute.start = 3; absolute.duration = 2; absolute.offset = .5; absolute.rate = 1.25;
        auto musical = downstream; musical.id = 212; musical.start = 7; musical.duration = 2; musical.offset = .75; musical.rate = .8;
        check(musical.anchorToBeats(motion::Tempo(137)), "ripple trim fixture includes a beat-anchored downstream clip");
        check(mixed.insert(earlier, motion::Tempo(137)) && mixed.insert(absolute, motion::Tempo(137)) && mixed.insert(musical, motion::Tempo(137)), "mixed-timebase ripple fixture placement");
        const auto musicalStart = mixed.clips[2].timing(motion::Tempo(137)).start;
        const auto musicalOffset = mixed.clips[2].offset;
        const auto musicalRate = mixed.clips[2].rate;
        check(motion::rippleTrim(mixed, 211, true, .4, motion::Tempo(137)), "mixed-timebase leading ripple trim succeeds");
        check(near(mixed.clips[1].start, 3) && near(mixed.clips[1].duration, 1.6) && near(mixed.clips[1].offset, 1),
            "mixed leading trim uses resolved seconds for content advance");
        check(mixed.clips[2].timeBase == motion::ClipTimeBase::beats && near(mixed.clips[2].timing(motion::Tempo(137)).start, musicalStart - .4)
            && mixed.clips[2].offset == musicalOffset && mixed.clips[2].rate == musicalRate,
            "mixed ripple preserves beat authoring and downstream source clock");

        motion::Track touchingMusical;
        auto beatHead = selected; beatHead.id = 220; beatHead.timeBase = motion::ClipTimeBase::beats;
        beatHead.start = 2; beatHead.duration = 3; beatHead.offset = .5; beatHead.contentBpm = 90;
        auto touchingTail = downstream; touchingTail.id = 221; touchingTail.start = beatHead.timing(motion::Tempo(137)).end();
        check(touchingMusical.insert(beatHead, motion::Tempo(137)) && touchingMusical.insert(touchingTail, motion::Tempo(137)), "touching musical head fixture");
        const auto headTiming = beatHead.timing(motion::Tempo(137));
        check(motion::rippleTrim(touchingMusical, 220, true, .2, motion::Tempo(137)), "leading trim of a musical clip with a different content tempo");
        check(near(touchingMusical.clips[0].timing(motion::Tempo(137)).start, headTiming.start)
            && near(touchingMusical.clips[0].timing(motion::Tempo(137)).duration(), headTiming.duration() - .2)
            && near(touchingMusical.clips[0].timing(motion::Tempo(137)).offset, headTiming.offset + .2 * headTiming.rate),
            "musical head converts placement and source offset independently");
        check(touchingMusical.clips[1].timing(motion::Tempo(137)).start >= touchingMusical.clips[0].timing(motion::Tempo(137)).end()
            && near(touchingMusical.clips[1].timing(motion::Tempo(137)).start, touchingMusical.clips[0].timing(motion::Tempo(137)).end()),
            "rounding keeps the musical head and absolute tail adjacent without overlap");
        check(motion::rippleTrim(touchingMusical, 220, false, -.3, motion::Tempo(137))
            && near(touchingMusical.clips[0].timing(motion::Tempo(137)).duration(), headTiming.duration() - .5),
            "trailing ripple shortening retains valid touching boundaries");

        const auto unchanged = original;
        track = unchanged;
        check(!motion::rippleTrim(track, 999, false, 1, motion::Tempo(120)) && sameTrack(track, unchanged), "missing ripple trim target is atomic");
        check(!motion::rippleTrim(track, 202, true, 2, motion::Tempo(120)) && sameTrack(track, unchanged), "nonpositive ripple trim duration is atomic");
        check(!motion::rippleTrim(track, 202, false, std::numeric_limits<double>::infinity(), motion::Tempo(120)) && sameTrack(track, unchanged), "nonfinite ripple trim is atomic");
        track.locked = true;
        const auto locked = track;
        check(!motion::rippleTrim(track, 202, false, 1, motion::Tempo(120)) && sameTrack(track, locked), "locked track rejects ripple trim atomically");
        track = unchanged;
        track.clips[2].start = 6;
        const auto overlapping = track;
        check(!motion::rippleTrim(track, 202, false, 1, motion::Tempo(120)) && sameTrack(track, overlapping), "overlapping track rejects ripple trim atomically");
        track = unchanged;
        track.clips[2].rate = 0;
        const auto invalid = track;
        check(!motion::rippleTrim(track, 202, false, 1, motion::Tempo(120)) && sameTrack(track, invalid), "invalid clip rejects ripple trim atomically");
        track = unchanged;
        check(!motion::rippleTrim(track, 202, false, 1, motion::Tempo(0)) && sameTrack(track, unchanged), "invalid tempo rejects ripple trim atomically");
    }
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
    check(clip.trim(12, 18, motion::Tempo(120)), "trim accepts a positive range");
    check(near(clip.localTime(15, motion::Tempo(120)), original.localTime(15, motion::Tempo(120))), "trimming retains content time");
    check(near(clip.properties.at("position.x").evaluate(clip.localTime(15, motion::Tempo(120))), 50), "trimming does not restart animation");
    const auto parts = original.split(15, 2, motion::Tempo(120));
    check(parts.has_value(), "split produces two instances");
    check(parts->first.asset == parts->second.asset, "split retains shared asset identity");
    check(!parts->first.contains(15, motion::Tempo(120)) && parts->second.contains(15, motion::Tempo(120)), "split boundary has exactly one active clip");
    check(near(parts->second.localTime(17, motion::Tempo(120)), original.localTime(17, motion::Tempo(120))), "split preserves source timing");
    auto independent = parts->second;
    independent.properties["position.x"].setKey({5, 999});
    check(near(parts->first.properties.at("position.x").evaluate(5), 50), "split curves are independently editable");
    auto stretched = original;
    check(stretched.stretch(20, motion::Tempo(120)), "stretch accepts a longer duration");
    check(near(stretched.localTime(20, motion::Tempo(120)), original.localTime(15, motion::Tempo(120))), "stretch preserves normalized content position");
    motion::Track track;
    check(track.insert(parts->second, motion::Tempo(120)) && track.insert(parts->first, motion::Tempo(120)), "sequential clips insert in any order");
    check(track.at(14, motion::Tempo(120))->id == 1 && track.at(15, motion::Tempo(120))->id == 2 && track.at(20, motion::Tempo(120)) == nullptr, "lookup follows half-open clip intervals");
    auto collision = original;
    collision.id = 3;
    check(!track.insert(collision, motion::Tempo(120)) && track.clips.size() == 2, "overlap rejection is non-destructive");
    check(!original.split(10, 3, motion::Tempo(120)) && !original.split(20, 3, motion::Tempo(120)), "split rejects zero-length results");
    check(!clip.stretch(0, motion::Tempo(120)) && !clip.trim(4, 4, motion::Tempo(120)), "invalid edits are rejected");
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
    // The oscillator shared by modulators: pure functions of time.
    motion::Modulation modulation;
    modulation.enabled = true;
    check(near(modulation.value(0.25), 1) && near(modulation.value(0.75), -1), "a sine cycles once per second");
    modulation.tempoSync = true;
    check(near(modulation.value(0.125, 120), 1) && near(modulation.value(0.25, 60), 1), "tempo sync follows project beats");
    modulation.beatsPerCycle = 2;
    check(near(modulation.value(0.25, 120), 1), "beat division controls the cycle duration");
    modulation.tempoSync = false;
    modulation.waveform = motion::ModulationWaveform::noiseHold;
    modulation.seed = 123;
    check(modulation.value(-0.75) == modulation.value(-0.25), "held noise is constant inside negative cycles");
    modulation.waveform = motion::ModulationWaveform::noiseSmooth;
    const auto smoothNoise = modulation.value(-0.375);
    for (int index = 20; index >= -20; --index) {
        check(std::isfinite(modulation.value(index * 0.17)), "noise remains finite while reverse scrubbing");
    }
    check(smoothNoise == modulation.value(-0.375), "noise evaluation is independent of playback history");
    auto changedSeed = modulation;
    changedSeed.seed = 124;
    check(changedSeed.value(-0.375) != smoothNoise, "noise seeds produce independent motion");
    check(near(modulation.value(1 - 1.0e-10), modulation.value(1 + 1.0e-10)), "smooth noise joins adjacent cycles continuously");
    for (int waveform = 0; waveform <= 5; ++waveform) {
        modulation.waveform = static_cast<motion::ModulationWaveform>(waveform);
        for (int index = -20; index <= 20; ++index) {
            const auto value = modulation.value(index * 0.137);
            check(std::isfinite(value) && value >= -1 && value <= 1, "every waveform stays bounded at positive and negative times");
        }
    }
    modulation.waveform = motion::ModulationWaveform::sine;
    modulation.phase = 0.25;
    check(near(modulation.value(0), 1), "phase is measured in cycles");
    modulation.rateHz = 0;
    check(!modulation.valid() && modulation.value(0) == 0, "invalid settings fail safely to no movement");
    modulation.rateHz = 1000;
    check(std::isfinite(modulation.value(std::numeric_limits<double>::max())), "extreme time products do not emit non-finite samples");
    {
        // Frame-aligned splits whose start + (split - start) rounds past the
        // split point must still produce two adjacent, insertable halves.
        int failures = 0;
        for (int startFrame = 0; startFrame < 400; startFrame += 7) {
            for (int splitFrame = startFrame + 1; splitFrame < startFrame + 2000; splitFrame += 13) {
                motion::Clip clip;
                clip.id = 1; clip.asset = 1; clip.start = startFrame / 30.0; clip.duration = 2000 / 30.0 + .5;
                motion::Track track;
                track.id = 2;
                motion::Clip after = clip;
                after.id = 3; after.start = clip.end();
                if (!track.insert(clip, motion::Tempo(120)) || !track.insert(after, motion::Tempo(120))) { ++failures; continue; }
                const auto parts = clip.split(splitFrame / 30.0, 4, motion::Tempo(120));
                if (!parts) { ++failures; continue; }
                track.clips.erase(track.clips.begin());
                if (!track.insert(parts->first, motion::Tempo(120)) || !track.insert(parts->second, motion::Tempo(120))) { ++failures; }
                if (parts->first.end() > parts->second.start || parts->second.end() > clip.end()) { ++failures; }
            }
        }
        check(failures == 0, "splits never overlap their halves or the following clip");
        check(motion::spanUntil(8.7, 59.6) > 0 && 8.7 + motion::spanUntil(8.7, 59.6) <= 59.6, "spanUntil never passes its end");
    }
    std::cout << "Motion timeline contracts passed\n";
}
