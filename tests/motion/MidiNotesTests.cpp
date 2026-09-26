#include "../../Source/motion/model/MidiNotes.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>

static void check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    using motion::MidiNote;
    using motion::MidiNotes;
    const auto pattern = MidiNotes::create({{3, 2, 1, 67, 110, 2}, {1, 0, 1, 60, 100, 1}, {2, 1, 2, 60, 80, 1}});
    check(static_cast<bool>(pattern), "Valid musical notes prepare");
    check(pattern.source->length() == 3 && pattern.source->notes().front().id == 1, "Length and note ordering follow content beats");
    const auto events = pattern.source->eventsInRange(0, 3);
    check(events.size() == 4 && events[0].on && !events[1].on && events[2].on, "Releases precede retriggers at one beat");
    check(events[1].beat == 1 && events[2].beat == 1 && events[3].channel == 2, "Note channel and identity survive preparation");
    check(pattern.source->eventsInRange(3, 4).size() == 2, "End events belong to exactly the next half-open interval");
    std::size_t count = 0;
    for (int i = 0; i < 400; ++i) { count += pattern.source->eventsInRange(i / 100.0, (i + 1) / 100.0).size(); }
    check(count == 6, "Adjacent scheduling ranges do not duplicate or lose events");
    for (const auto [begin, end] : {std::pair {1.0, 0.0}, {0.0, 0.0}, {0.0, std::numeric_limits<double>::infinity()}}) {
        check(pattern.source->eventsInRange(begin, end).empty(), "Invalid interval is empty");
    }
    const std::array<std::uint64_t, 2> pair {1, 3};
    const auto moved = pattern.source->moveNotes(pair, 0.5, 12);
    check(static_cast<bool>(moved) && moved.source->length() == 3.5, "Group movement preserves lengths and adjusts pitch");
    check(pattern.source->notes()[0].start == 0 && pattern.source->notes()[0].pitch == 60, "Old shared instances remain unchanged");
    check(!pattern.source->moveNotes(pair, -0.5, 0) && !pattern.source->moveNotes(pair, 0, 100), "Out-of-range group movement fails atomically");
    const std::array<std::uint64_t, 2> duplicates {1, 1};
    const std::array<std::uint64_t, 1> stale {99};
    check(!pattern.source->moveNotes(duplicates, 1, 0) && !pattern.source->withoutNotes(stale), "Duplicate or stale selections reject");
    const auto erased = pattern.source->withoutNotes(pair);
    check(static_cast<bool>(erased) && erased.source->notes().size() == 1 && erased.source->notes()[0].id == 2, "Deletion only changes selected notes");
    auto edit = pattern.source->notes()[0];
    edit.velocity = 42; edit.duration = 0.25;
    const auto changed = pattern.source->withNote(edit);
    check(static_cast<bool>(changed) && changed.source->notes()[0].velocity == 42 && changed.source->events()[1].beat == .25, "Note resize and velocity edit rebuild scheduling");
    check(static_cast<bool>(pattern.source->withNote({4, 4, 1, 72, 90, 16})), "Adding a note uses a new identity");
    check(!pattern.source->withNote({0, 4, 1, 72, 90, 16}), "Zero identity rejects");
    check(!MidiNotes::create({{1, 0, 1, 60, 100, 1}, {1, 2, 1, 61, 100, 1}}), "Duplicate pattern IDs reject");
    for (const MidiNote bad : {MidiNote {1, -1, 1}, {1, 0, 0}, {1, 1, 1e-320}, {1, 1000000, 1},
            {1, 0, 1, 128}, {1, 0, 1, 60, 0}, {1, 0, 1, 60, 100, 17},
            {1, std::numeric_limits<double>::quiet_NaN(), 1}}) {
        check(!MidiNotes::create({bad}), "Invalid notes never publish a partial pattern");
    }
    auto sparse = std::vector<MidiNote>();
    sparse.reserve(MidiNotes::maximumNotes * 2);
    sparse.push_back({1, 0, 1});
    const auto compact = MidiNotes::create(std::move(sparse));
    check(static_cast<bool>(compact) && compact.source->notes().capacity() <= MidiNotes::maximumNotes, "Sparse oversized allocation is compacted before publication");
    const auto overlap = MidiNotes::create({{3, 0, 1, 60}, {1, 0, 2, 60}, {4, 1, 1, 62}, {2, 1, 2, 64}});
    check(static_cast<bool>(overlap), "Overlapping same-pitch notes retain independent identities");
    const auto retrigger = overlap.source->eventsInRange(1, 2);
    check(retrigger.size() == 3 && retrigger[0].note == 3 && !retrigger[0].on
        && retrigger[1].note == 2 && retrigger[1].on && retrigger[2].note == 4 && retrigger[2].on,
        "Same-beat releases precede deterministic ID-ordered note-ons");
    const auto releases = overlap.source->eventsInRange(2, 3);
    check(releases.size() == 2 && releases[0].note == 1 && releases[1].note == 4 && !releases[0].on && !releases[1].on,
        "Overlapping notes finish at their own end times");
    const auto empty = MidiNotes::create({});
    check(static_cast<bool>(empty) && empty.source->length() == 0 && empty.source->eventsInRange(0, 1).empty(), "Empty editable patterns are valid");
    std::vector<MidiNote> many;
    many.reserve(MidiNotes::maximumNotes);
    for (std::uint64_t i = 0; i < MidiNotes::maximumNotes; ++i) { many.push_back({i + 1, i / 10.0, 1}); }
    const auto large = MidiNotes::create(many);
    check(static_cast<bool>(large) && large.source->events().size() == MidiNotes::maximumNotes * 2, "Bounded large patterns prepare");
    many.push_back({MidiNotes::maximumNotes + 1, 0, 1});
    check(!MidiNotes::create(std::move(many)), "Note budget enforced before extra event storage");
    std::cout << "MIDI note model contracts passed\n";
}
