#include "../../Source/motion/render/MidiTakeNotes.h"
#include <cstdlib>
#include <iostream>

using motion::MidiRecording;
using motion::MidiTakeNotes;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
MidiRecording::Take take() {
    MidiRecording::Take t;
    t.config.token = 1; t.config.target = 1;
    t.config.firstSample = 100; t.config.endSample = 1100;
    t.config.sampleRate = 100; t.config.sourceBpm = 60;
    t.firstSample = 100; t.endSample = 1100;
    return t;
}
void event(MidiRecording::Take& t, std::uint64_t sample, int status, int key, int value = 0) {
    const int kind = status & 0xf0;
    t.events.push_back({sample, {static_cast<std::uint8_t>(status), static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(value)}, static_cast<std::uint8_t>(kind == 0xc0 || kind == 0xd0 ? 2 : 3)});
}
int main() {
    {
        auto t = take();
        t.config.sourceOffset = 2; t.config.sourceRate = 2; t.config.sourceBpm = 120;
        event(t, 100, 0x90, 60, 80); event(t, 200, 0x90, 60, 90);
        event(t, 300, 0x80, 60); event(t, 400, 0x90, 60, 0);
        auto r = MidiTakeNotes::convert(t);
        check(r && r.addedCount == 2, "overlapping notes survive");
        const auto& n = r.source->notes();
        check(n[0].start == 4 && n[0].duration == 8 && n[0].velocity == 80, "FIFO first note and source mapping");
        check(n[1].start == 8 && n[1].duration == 8 && n[1].velocity == 90, "velocity-zero FIFO release");
    }
    {
        auto t = take();
        event(t, 100, 0xb0, 64, 127); event(t, 100, 0x90, 60, 100);
        event(t, 150, 0x91, 60, 90); event(t, 200, 0x80, 60);
        event(t, 300, 0xb0, 121); event(t, 400, 0x81, 60);
        auto r = MidiTakeNotes::convert(t);
        check(r && r.source->notes()[0].duration == 2, "CC121 releases sustained note");
        check(r.source->notes()[1].duration == 2.5 && r.source->notes()[1].channel == 2, "channels independent");
    }
    {
        auto t = take();
        event(t, 100, 0xb0, 64, 127); event(t, 100, 0x90, 60, 100);
        event(t, 200, 0xb0, 123); event(t, 300, 0xb0, 64);
        event(t, 400, 0x90, 61, 100); event(t, 500, 0xb0, 120);
        event(t, 600, 0xe0, 0, 64); event(t, 600, 0xc0, 3); event(t, 600, 0xd0, 30);
        event(t, 600, 0xb0, 1, 20); event(t, 600, 0xa0, 61, 30);
        event(t, 700, 0x90, 62, 100);
        auto r = MidiTakeNotes::convert(t);
        check(r && r.addedCount == 3 && r.ignoredControllerCount == 4, "unsupported program/aftertouch/hardcut warnings");
        check(r.source->controls().size() == 2, "pitch bend and CC 1 persist");
        check(r.source->notes()[0].duration == 2 && r.source->notes()[1].duration == 1, "CC123 sustain and CC120 immediate close");
        check(r.source->notes()[2].duration == 4, "held note closes at actual stop");
    }
    {
        auto t = take(); event(t, 100, 0x90, 60, 100); event(t, 100, 0x80, 60);
        event(t, 200, 0x90, 61, 100);
        auto base = motion::MidiNotes::create({{1, 0, 1, 50, 100, 1}, {UINT64_MAX, 1, 1, 51, 100, 1}}).source;
        auto r = MidiTakeNotes::convert(t, base);
        check(r && r.addedCount == 1 && r.source->notes().size() == 3, "zero-duration discarded and max ID collision avoided");
        check(base->notes().size() == 2, "base remains immutable");
    }
    {
        auto t = take(); event(t, 200, 0x90, 60, 100); event(t, 150, 0x80, 60);
        check(!MidiTakeNotes::convert(t), "unsorted rejected");
        t.events.clear(); event(t, 1100, 0x90, 60, 100);
        check(!MidiTakeNotes::convert(t), "end exclusive");
        t.events[0].sample = 100; t.events[0].bytes[2] = 255;
        check(!MidiTakeNotes::convert(t), "bad data byte rejected");
        t.events.clear(); t.failure = MidiRecording::Failure::overflow;
        check(!MidiTakeNotes::convert(t), "failed take rejected");
        t = take(); std::atomic<bool> cancel{true};
        check(!MidiTakeNotes::convert(t, {}, &cancel), "cancel rejected");
    }
    {
        auto t = take();
        for (std::size_t i = 0; i <= motion::MidiNotes::maximumNotes; ++i) { event(t, 100, 0x90, 60, 100); }
        check(!MidiTakeNotes::convert(t), "held-note finalization enforces global note bound");
    }
    std::cout << "MidiTakeNotes tests passed\n";
}
