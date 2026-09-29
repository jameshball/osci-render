#include <JuceHeader.h>
#include "../Source/motion/import/MidiSourcePreparer.h"

namespace motion_midi_test {
using Bytes = std::vector<std::uint8_t>;
static void big16(Bytes& bytes, unsigned value) { bytes.push_back(static_cast<std::uint8_t>(value >> 8)); bytes.push_back(static_cast<std::uint8_t>(value)); }
static void big32(Bytes& bytes, std::size_t value) { big16(bytes, static_cast<unsigned>(value >> 16)); big16(bytes, static_cast<unsigned>(value)); }
static void vlq(Bytes& bytes, std::uint32_t value) {
    std::array<std::uint8_t, 4> buffer {};
    auto index = buffer.size();
    buffer[--index] = static_cast<std::uint8_t>(value & 127);
    while ((value >>= 7) != 0) { buffer[--index] = static_cast<std::uint8_t>((value & 127) | 128); }
    bytes.insert(bytes.end(), buffer.begin() + static_cast<std::ptrdiff_t>(index), buffer.end());
}
static void event(Bytes& track, unsigned delta, std::initializer_list<std::uint8_t> data) { vlq(track, delta); track.insert(track.end(), data); }
static void end(Bytes& track, unsigned delta = 0) { event(track, delta, { 0xff, 0x2f, 0 }); }
static Bytes file(std::vector<Bytes> tracks, unsigned format = 0, unsigned division = 480) {
    Bytes bytes { 'M', 'T', 'h', 'd' }; big32(bytes, 6); big16(bytes, format); big16(bytes, static_cast<unsigned>(tracks.size())); big16(bytes, division);
    for (const auto& track : tracks) { bytes.insert(bytes.end(), { 'M', 'T', 'r', 'k' }); big32(bytes, track.size()); bytes.insert(bytes.end(), track.begin(), track.end()); }
    return bytes;
}
static motion::MidiSourcePreparer::Result prepare(const Bytes& bytes, double bpm = 120) { return motion::MidiSourcePreparer::prepare(bytes.data(), bytes.size(), bpm); }
static Bytes simpleTrack(unsigned length = 480) {
    Bytes track; event(track, 0, { 0x90, 60, 100 }); event(track, length, { 0x80, 60, 0 }); end(track); return track;
}
}
class MotionMidiImportTest : public juce::UnitTest {
public:
    MotionMidiImportTest() : juce::UnitTest("Motion MIDI import", "MotionMidi") {}
    void runTest() override {
        using namespace motion_midi_test;
        beginTest("PPQ notes retain beats, velocity/channel and initial suggested tempo");
        Bytes track;
        event(track, 0, { 0xff, 0x51, 3, 0x09, 0x27, 0xc0 }); // 600000us =>100 BPM.
        event(track, 0, { 0x92, 60, 10 });
        event(track, 120, { 60, 20 }); // Running-status overlapping note-on.
        event(track, 120, { 60, 0 }); // Velocity-zero off pairs first on FIFO.
        event(track, 120, { 0x82, 60, 0 });
        event(track, 0, { 0xff, 0x51, 3, 0x07, 0xa1, 0x20 }); // Later tempo ignored.
        end(track);
        const auto result = prepare(file({ track }), 170);
        expect(static_cast<bool>(result), juce::String(result.error));
        if (result) {
            expectWithinAbsoluteError(result.suggestedBpm, 100.0, 1e-12);
            expectEquals(result.ignoredEvents, 1);
            const auto& notes = result.source->notes();
            expectEquals(static_cast<int>(notes.size()), 2);
            if (notes.size() == 2) {
                expectWithinAbsoluteError(notes[0].start, 0.0, 1e-12);
                expectWithinAbsoluteError(notes[0].duration, 0.5, 1e-12);
                expectWithinAbsoluteError(notes[1].start, 0.25, 1e-12);
                expectWithinAbsoluteError(notes[1].duration, 0.5, 1e-12);
                expectEquals(notes[0].channel, 3); expectEquals(notes[0].velocity, 10); expectEquals(notes[1].velocity, 20);
                expect(notes[0].id != notes[1].id);
            }
            const auto repeat = prepare(file({ track }), 90);
            expect(static_cast<bool>(repeat));
            if (repeat) { expect(repeat.source->notes()[0].id == notes[0].id); expectWithinAbsoluteError(repeat.source->length(), result.source->length(), 1e-12); }
        }

        beginTest("Format1 simultaneous tracks flatten without stealing each other's note-offs");
        Bytes tempo; event(tempo, 0, { 0xff, 0x51, 3, 0x06, 0x1a, 0x80 }); end(tempo); //150BPM.
        const auto layered = prepare(file({ tempo, simpleTrack(480), simpleTrack(960) }, 1));
        expect(static_cast<bool>(layered), juce::String(layered.error));
        if (layered) {
            expectWithinAbsoluteError(layered.suggestedBpm, 150.0, 1e-12);
            expectEquals(static_cast<int>(layered.source->notes().size()), 2);
            expectWithinAbsoluteError(layered.source->notes()[0].duration, 1.0, 1e-12);
            expectWithinAbsoluteError(layered.source->notes()[1].duration, 2.0, 1e-12);
        }
        Bytes onOnly; event(onOnly, 0, { 0x90, 60, 100 }); end(onOnly, 480);
        Bytes offOnly; event(offOnly, 480, { 0x80, 60, 0 }); end(offOnly);
        expect(!prepare(file({ onOnly, offOnly }, 1)), "Independent track voices must not pair across tracks.");

        beginTest("SMPTE maps absolute time at project tempo including29-drop fractional frame rate");
        const auto smpte25 = prepare(file({ simpleTrack(2500) }, 0, (0xe7 << 8) | 100), 120);
        expect(static_cast<bool>(smpte25), juce::String(smpte25.error));
        if (smpte25) { expectWithinAbsoluteError(smpte25.source->notes()[0].duration, 2.0, 1e-12); }
        const auto drop = prepare(file({ simpleTrack(30000) }, 0, (0xe3 << 8) | 100), 120);
        expect(static_cast<bool>(drop), juce::String(drop.error));
        if (drop) { expectWithinAbsoluteError(drop.source->notes()[0].duration, 20.02, 1e-10); }
        expect(!prepare(file({ simpleTrack() }, 0, 0xe300)), "Zero ticks per SMPTE frame rejects.");
        expect(!prepare(file({ simpleTrack() }, 0, 0xe464)), "Unsupported SMPTE frame code rejects.");

        beginTest("Same-tick releases precede retriggers and SMPTE retains the conversion tempo");
        Bytes retrigger;
        event(retrigger, 1, { 0x90, 60, 100 });
        event(retrigger, 30, { 0x80, 60, 0 });
        event(retrigger, 0, { 0x90, 60, 100 });
        event(retrigger, 30, { 0x80, 60, 0 }); end(retrigger);
        for (const auto division : { 480U, (0xe7U << 8) | 100U }) {
            const auto tied = prepare(file({ retrigger }, 0, division), 170);
            expect(static_cast<bool>(tied), juce::String(tied.error));
            if (tied) {
                const auto& events = tied.source->events();
                expectEquals(static_cast<int>(events.size()), 4);
                if (events.size() == 4) {
                    expect(events[1].beat == events[2].beat);
                    expect(!events[1].on && events[2].on);
                }
                if ((division & 0x8000) != 0) { expectWithinAbsoluteError(tied.suggestedBpm, 170.0, 1e-12); }
            }
        }

        beginTest("Excluded performance/meta/SysEx events are counted explicitly");
        Bytes controls;
        event(controls, 0, { 0xb0, 64, 127 }); event(controls, 0, { 0xe0, 0, 64 }); event(controls, 0, { 0xc0, 10 });
        event(controls, 0, { 0xff, 3, 3, 'a', 'b', 'c' }); event(controls, 0, { 0xf0, 2, 1, 0xf7 });
        const auto noteTrack = simpleTrack(); controls.insert(controls.end(), noteTrack.begin(), noteTrack.end());
        const auto ignored = prepare(file({ controls }));
        expect(static_cast<bool>(ignored), juce::String(ignored.error));
        // Sustain, program change, meta text and SysEx are excluded; pitch bend is kept.
        if (ignored) {
            expectEquals(ignored.ignoredEvents, 4);
            expectEquals(static_cast<int>(ignored.source->controls().size()), 1);
        }

        beginTest("SMF extended headers and bounded unknown chunks are honoured");
        auto extendedHeader = file({ simpleTrack() });
        extendedHeader[7] = 10;
        extendedHeader.insert(extendedHeader.begin() + 14, { 0xde, 0xad, 0xbe, 0xef });
        const auto extended = prepare(extendedHeader);
        expect(static_cast<bool>(extended), juce::String(extended.error));
        if (extended) { expectWithinAbsoluteError(extended.source->notes()[0].duration, 1.0, 1e-12); }
        auto shortHeader = file({ simpleTrack() }); shortHeader[7] = 5;
        expect(!prepare(shortHeader), "Headers shorter than six bytes reject.");
        auto truncatedHeader = extendedHeader; truncatedHeader.resize(17);
        expect(!prepare(truncatedHeader), "Declared extended header must fit its input.");
        const Bytes unknown { 'T', 'E', 'S', 'T', 0, 0, 0, 3, 0xaa, 0xbb, 0xcc };
        auto containers = file({ simpleTrack(), simpleTrack(960) }, 1);
        const auto afterFirstTrack = 14 + 8 + simpleTrack().size();
        containers.insert(containers.begin() + static_cast<std::ptrdiff_t>(afterFirstTrack), unknown.begin(), unknown.end());
        containers.insert(containers.begin() + 14, unknown.begin(), unknown.end());
        containers.insert(containers.end(), unknown.begin(), unknown.end());
        // Zero-size unknown chunks still advance by their eight-byte headers.
        containers.insert(containers.end(), { 'Z', 'E', 'R', 'O', 0, 0, 0, 0 });
        const auto skipped = prepare(containers);
        expect(static_cast<bool>(skipped), juce::String(skipped.error));
        if (skipped) { expectEquals(static_cast<int>(skipped.source->notes().size()), 2); expectEquals(skipped.ignoredEvents, 4); }
        auto truncatedUnknown = file({ simpleTrack() });
        truncatedUnknown.insert(truncatedUnknown.end(), unknown.begin(), unknown.end() - 1);
        expect(!prepare(truncatedUnknown), "Unknown chunk payload must not run past input.");
        auto truncatedUnknownHeader = file({ simpleTrack() });
        truncatedUnknownHeader.insert(truncatedUnknownHeader.end(), unknown.begin(), unknown.begin() + 7);
        expect(!prepare(truncatedUnknownHeader), "Unknown chunk length field must be complete.");
        auto extraTrack = file({ simpleTrack() });
        const auto otherFile = file({ simpleTrack() });
        extraTrack.insert(extraTrack.end(), otherFile.begin() + 14, otherFile.end());
        expect(!prepare(extraTrack), "Additional MTrk chunks cannot hide as unknown chunks.");
        auto duplicateHeader = file({ simpleTrack() });
        duplicateHeader.insert(duplicateHeader.end(), otherFile.begin(), otherFile.begin() + 14);
        expect(!prepare(duplicateHeader), "Duplicate MThd chunks reject.");
        auto missingTrack = file({ simpleTrack(), simpleTrack() }, 1);
        missingTrack.resize(14 + 8 + simpleTrack().size());
        expect(!prepare(missingTrack), "Track count must still match after chunk skipping.");

        beginTest("Malformed chunks, VLQs, running status and unfinished/zero notes reject atomically");
        const auto valid = file({ simpleTrack() });
        for (std::size_t size = 0; size < valid.size(); ++size) { expect(!motion::MidiSourcePreparer::prepare(valid.data(), size, 120)); }
        const auto format2 = prepare(file({ simpleTrack() }, 2));
        expect(!format2 && format2.error.find("format 2") != std::string::npos);
        expect(!prepare(file({ simpleTrack(), simpleTrack() }, 0)));
        expect(!prepare(file({ simpleTrack() }, 0, 0)));
        expect(!prepare(file({ onOnly })));
        expect(!prepare(file({ offOnly })));
        expect(!prepare(file({ simpleTrack(0) })));
        Bytes missingEnd = simpleTrack(); missingEnd.resize(missingEnd.size() - 4); expect(!prepare(file({ missingEnd })));
        auto trailing = valid; trailing.push_back(0); expect(!prepare(trailing));
        expect(!prepare(file({ Bytes { 0x80, 0x80, 0x80, 0x80, 0, 0xff, 0x2f, 0 } })));
        expect(!prepare(file({ Bytes { 0x80, 0, 0xff, 0x2f, 0 } })));
        expect(!prepare(file({ Bytes { 0, 60, 100 } })));
        Bytes interrupted; event(interrupted, 0, { 0x90, 60, 100 }); event(interrupted, 0, { 0xff, 1, 0 }); event(interrupted, 480, { 60, 0 }); end(interrupted);
        expect(!prepare(file({ interrupted })), "Meta event cancels running status.");
        Bytes badData; event(badData, 0, { 0x90, 0x80, 100 }); end(badData); expect(!prepare(file({ badData })));
        Bytes badTempo; event(badTempo, 0, { 0xff, 0x51, 3, 0, 0, 0 }); end(badTempo); expect(!prepare(file({ badTempo })));
        Bytes trailingTrack; end(trailingTrack); trailingTrack.push_back(0); expect(!prepare(file({ trailingTrack })));
        Bytes tooLate; event(tooLate, 1000001, { 0x90, 60, 100 }); end(tooLate); expect(!prepare(file({ tooLate }, 0, 1)));
        Bytes tooMany;
        for (std::size_t i = 0; i <= motion::MidiSourcePreparer::maximumEvents; ++i) { event(tooMany, 0, { 0xc0, 0 }); }
        end(tooMany); expect(!prepare(file({ tooMany })));
        std::atomic<bool> cancel { true };
        expect(!motion::MidiSourcePreparer::prepare(valid.data(), valid.size(), 120, &cancel));
        expect(!motion::MidiSourcePreparer::prepare(nullptr, 1, 120));
        expect(!motion::MidiSourcePreparer::prepare(valid.data(), motion::MidiSourcePreparer::maximumEncodedBytes + 1, 120));
        expect(!prepare(valid, std::numeric_limits<double>::quiet_NaN()));
    }
};
static MotionMidiImportTest motionMidiImportTest;
