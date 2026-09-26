#include "../../Source/motion/render/PreparedMidiSchedule.h"
#include <cstdlib>
#include <iostream>
#include <random>
#include <limits>

static void check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    using namespace motion;
    Clip clip; clip.id = 1; clip.duration = 10;
    auto notes = MidiNotes::create({{1, 0, 2, 60, 100, 1}, {2, 1, 2, 60, 80, 1}, {3, 3, 1, 64, 127, 2}});
    check(bool(notes), "valid note fixture");
    auto prepared = PreparedMidiSchedule::prepare(*notes.source, clip, 120, 1000, 101);
    check(bool(prepared), "prepare overlapping same-pitch notes");
    const auto& schedule = *prepared.schedule;
    check(schedule.activeAt(-1).empty(), "silence before note-on");
    check(schedule.activeAt(0).size() == 1 && schedule.activeAt(500).size() == 2, "same pitch gets two voices");
    check(schedule.activeAt(1099).size() == 2 && schedule.activeAt(1100).size() == 1, "one release does not terminate its same-pitch sibling");
    check(schedule.voice(schedule.activeAt(1100)[0]).id == 2, "remaining voice keeps its identity");
    check(schedule.activeAt(1500).size() == 2 && schedule.activeAt(1600).size() == 1, "release tail and next note coexist");
    check(schedule.activeAt(2100).empty(), "silence after last release");

    // Seek randomly and in reverse; compare ownership against independent
    // intervals, not the schedule's event representation or mutable cursor.
    std::mt19937 random(1234);
    for (int query = 0; query < 20000; ++query) {
        const auto sample = query < 2200 ? 2199 - query : static_cast<int>(random() % 2400) - 100;
        std::vector<Id> expected;
        if (sample >= 0 && sample < 1100) { expected.push_back(1); }
        if (sample >= 500 && sample < 1600) { expected.push_back(2); }
        if (sample >= 1500 && sample < 2100) { expected.push_back(3); }
        const auto actual = schedule.activeAt(sample);
        check(actual.size() == expected.size(), "seek active voice count");
        for (std::size_t i = 0; i < actual.size(); ++i) { check(schedule.voice(actual[i]).id == expected[i], "seek note identity"); }
    }

    Clip musical = clip;
    musical.timeBase = ClipTimeBase::beats; musical.start = 4; musical.duration = 8;
    musical.offset = 1; musical.rate = 2; musical.contentBpm = 150;
    auto at120 = PreparedMidiSchedule::prepare(*notes.source, musical, 120, 1000, 101);
    auto at240 = PreparedMidiSchedule::prepare(*notes.source, musical, 240, 1000, 101);
    check(bool(at120) && bool(at240), "prepare musical placement at two tempi");
    const auto& first120 = at120.schedule->voice(0);
    const auto& first240 = at240.schedule->voice(0);
    check(first120.on == 1750 && first120.off == 2250 && first120.first == 2000, "beat offset/rate preserve pre-roll at 120 BPM");
    check(first240.on == 875 && first240.off == 1125 && first240.first == 1000, "tempo scales note start and hold");
    check(first120.end == 2350 && first240.end == 1225, "envelope release remains output seconds");
    check(first120.age(2000) == 250 && first120.heldSamples() == 500, "trim entry retains note age");

    auto split = musical.split(2.125, 2, 120);
    check(split.has_value(), "split musical clip during note");
    auto right = PreparedMidiSchedule::prepare(*notes.source, split->second, 120, 1000, 101);
    check(bool(right) && right.schedule->voice(0).on == first120.on && right.schedule->voice(0).off == first120.off, "split does not retrigger or change release");
    check(right.schedule->voice(0).first == 2125, "right split ownership begins at cut");

    Clip slipped = clip; slipped.offset = .75; slipped.duration = .5;
    auto slip = PreparedMidiSchedule::prepare(*notes.source, slipped, 120, 1000, 101);
    check(bool(slip), "prepare negative note-on after slip");
    check(slip.schedule->voice(0).on == -750 && slip.schedule->voice(0).age(0) == 750, "negative pre-roll keeps musical age");
    check(slip.schedule->activeAt(499).size() == 1 && slip.schedule->activeAt(500).empty(), "clip end cuts sustained voices and release tails");
    slipped.offset = 1.05;
    auto releaseEntry = PreparedMidiSchedule::prepare(*notes.source, slipped, 120, 1000, 101);
    check(bool(releaseEntry) && releaseEntry.schedule->activeAt(0).size() == 2, "can seek into release started before clip");
    check(releaseEntry.schedule->voice(0).off == -50 && releaseEntry.schedule->voice(0).end == 50, "release pre-roll retains correct exclusive end");

    std::vector<MidiNote> chord;
    for (Id id = 1; id <= 32; ++id) { chord.push_back({id, 0, 1, 60, 100, 1}); }
    auto full = MidiNotes::create(chord);
    auto max = PreparedMidiSchedule::prepare(*full.source, clip, 120, 1000, 101);
    check(bool(max) && max.schedule->activeAt(0).size() == 32, "32 voices accepted");
    chord.push_back({33, 1, 1, 60, 100, 1});
    auto tooMany = MidiNotes::create(chord);
    check(!PreparedMidiSchedule::prepare(*tooMany.source, clip, 120, 1000, 101), "release tails count toward limit");
    chord.back().start = 1.2;
    auto adjacent = MidiNotes::create(chord);
    auto handoff = PreparedMidiSchedule::prepare(*adjacent.source, clip, 120, 1000, 101);
    check(bool(handoff) && handoff.schedule->activeAt(600).size() == 1, "same-sample releases precede starts at capacity");

    std::vector<MidiNote> retriggers;
    for (Id id = 1; id <= 32; ++id) {
        retriggers.push_back({id, 0, 1, 60, 100, 1});
        retriggers.push_back({id + 32, 1, 1, 60, 100, 1});
    }
    auto retriggerNotes = MidiNotes::create(retriggers);
    auto retriggerSchedule = PreparedMidiSchedule::prepare(*retriggerNotes.source, clip, 120, 1000, 1);
    check(bool(retriggerSchedule), "zero-release full chords retrigger without stealing");
    check(retriggerSchedule.schedule->activeAt(499).size() == 32 && retriggerSchedule.schedule->activeAt(500).size() == 32, "zero-release ownership has no extra sample");
    check(retriggerSchedule.schedule->voice(retriggerSchedule.schedule->activeAt(500)[0]).id == 33, "retriggers own the boundary sample");
    auto terminal = PreparedMidiSchedule::prepare(*notes.source, clip, 120, 1000, 100);
    check(bool(terminal) && terminal.schedule->activeAt(1098).size() == 2 && terminal.schedule->activeAt(1099).size() == 1, "nonzero release ends at shared envelope Done sample");
    auto outsideNotes = MidiNotes::create({{1, 0, .0001, 60, 100, 1}, {2, 4, 1, 60, 100, 1}, {3, 100, .0001, 60, 100, 1}});
    Clip trimmed = clip; trimmed.offset = 2; trimmed.duration = 1;
    auto outside = PreparedMidiSchedule::prepare(*outsideNotes.source, trimmed, 120, 1000, 1);
    check(bool(outside) && outside.schedule->voiceCount() == 1, "inaudible tiny notes outside a trim do not reject playable content");
    check(first120.age(std::numeric_limits<std::int64_t>::min()) == 0, "age before note does not underflow");
    check(slip.schedule->voice(0).age(std::numeric_limits<std::int64_t>::max()) > 0, "extreme age does not overflow signed arithmetic");

    auto empty = MidiNotes::create({});
    auto silence = PreparedMidiSchedule::prepare(*empty.source, clip, 120, 48000, 1);
    check(bool(silence) && silence.schedule->activeAt(0).empty(), "empty authored pattern is silent");
    std::atomic<bool> cancelled {true};
    check(!PreparedMidiSchedule::prepare(*notes.source, clip, 120, 1000, 101, &cancelled), "cancel before work");
    check(!PreparedMidiSchedule::prepare(*notes.source, clip, 0, 1000, 100), "invalid tempo rejected");
    check(!PreparedMidiSchedule::prepare(*notes.source, clip, 120, 0, 100), "invalid sample rate rejected");
    check(!PreparedMidiSchedule::prepare(*notes.source, clip, 120, 1000, 0), "invalid release rejected");
    auto shortNote = MidiNotes::create({{1, 0, .0001, 60, 100, 1}});
    check(!PreparedMidiSchedule::prepare(*shortNote.source, clip, 120, 1000, 1), "unrepresentable note hold fails explicitly");

    // Worst note count remains bounded and lookup is independent of total
    // event history. Dense sequential ownership also exercises uint32 offsets.
    std::vector<MidiNote> many;
    for (Id id = 1; id <= MidiNotes::maximumNotes; ++id) { many.push_back({id, static_cast<double>(id), .5, 60, 100, 1}); }
    auto large = MidiNotes::create(std::move(many));
    Clip longClip = clip; longClip.duration = 100000;
    auto largeSchedule = PreparedMidiSchedule::prepare(*large.source, longClip, 120, 1000, 1);
    check(bool(largeSchedule) && largeSchedule.schedule->voiceCount() == MidiNotes::maximumNotes, "bounded 100000-note preparation");
    check(largeSchedule.schedule->voice(largeSchedule.schedule->activeAt(50000000)[0]).id == 100000, "seek to final note without traversing history");
    std::cout << "Prepared MIDI schedule tests passed\n";
}
