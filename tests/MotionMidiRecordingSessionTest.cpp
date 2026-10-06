#include <JuceHeader.h>
#include "../Source/motion/render/MidiRecordingSession.h"
#include "../Source/motion/import/SourceDecoding.h"

class MotionMidiRecordingSessionTest : public juce::UnitTest {
public:
    MotionMidiRecordingSessionTest() : juce::UnitTest("Motion MIDI recording session", "MotionMidi") {}
    // The session's poll timer requires the runner thread to be the message thread.
    void initialise() override { juce::MessageManager::getInstance(); }
    struct Fixture {
        juce::UndoManager undo;
        motion::Document document{undo};
        motion::MidiRecording recorder;
        double position = 0;
        bool ready = true, deviceRunning = true;
        motion::Id monitored = 0, clipId = 0;
        std::unique_ptr<motion::MidiRecordingSession> session;
        juce::Result initialise(std::shared_ptr<const motion::MidiNotes> base = {}) {
            auto asset = std::make_shared<motion::Asset>();
            asset->id = document.newId(); asset->name = "triangle.obj"; asset->extension = ".obj";
            const juce::String obj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
            asset->data.append(obj.toRawUTF8(), obj.getNumBytesAsUTF8());
            auto result = motion::decodeAsset(*asset);
            if (result.failed()) { return result; }
            auto clip = motion::Document::makeClip(document.newId(), *asset, 2);
            clipId = clip.id; clip.duration = 4; clip.offset = .5; clip.rate = 2; clip.midi = std::move(base);
            motion::Track track; track.id = document.newId(); track.clips.push_back(clip);
            motion::Project project; project.bpm = 120; project.duration = 10; project.assets = {asset}; project.tracks = {track};
            document.reset(std::move(project));
            session = std::make_unique<motion::MidiRecordingSession>(document, recorder, motion::MidiRecordingSession::Transport{
                [] { return 100.0; }, [this] { return position; }, [this] { return ready; },
                [this](motion::Id id) { monitored = id; }, [this](const auto& config) { return deviceRunning && recorder.arm(config); }});
            return juce::Result::ok();
        }
        const motion::Clip& clip() const { return document.project().tracks[0].clips[0]; }
        bool block(bool notes = true) {
            const auto start = recorder.transportStart();
            if (!start || !recorder.beginBlock(100, *start, 100, true, true)) { return false; }
            if (notes) {
                const unsigned char on[] {0x90, 60, 100}, off[] {0x80, 60, 0};
                recorder.event(10, on, 3); recorder.event(60, off, 3);
            }
            recorder.endBlock();
            return true;
        }
        void acknowledge() { recorder.beginBlock(100, 300, 100, true, true); }
        bool drain() {
            const auto limit = juce::Time::getMillisecondCounterHiRes() + 5000;
            while (session->busy() && juce::Time::getMillisecondCounterHiRes() < limit) {
                session->poll(); juce::Thread::sleep(1);
            }
            return !session->busy();
        }
    };
    void runTest() override {
        beginTest("Arm publishes one transport command; stop commits mapped notes in one undo transaction");
        {
            Fixture f; expect(f.initialise().wasOk());
            const auto revision = f.document.revision();
            expect(f.session->start(f.clipId).wasOk());
            expect(f.recorder.state() == motion::MidiRecording::State::armed && f.monitored == f.clipId);
            const auto start = f.recorder.transportStart(); expect(start && *start == 200);
            expect(f.document.revision() == revision && !f.undo.canUndo());
            expect(f.block()); f.session->stop(); f.acknowledge(); expect(f.drain());
            expect(!f.session->hasError(), f.session->message());
            expect(f.clip().midi != nullptr);
            if (f.clip().midi != nullptr) {
                expectEquals(static_cast<int>(f.clip().midi->notes().size()), 1);
                const auto& note = f.clip().midi->notes()[0];
                expectWithinAbsoluteError(note.start, 1.4, 1e-10);
                expectWithinAbsoluteError(note.duration, 2.0, 1e-10);
            }
            expect(f.monitored == 0);
            expect(f.undo.undo()); expect(f.clip().midi == nullptr); expect(!f.undo.canUndo());
            expect(f.undo.redo()); expect(f.clip().midi != nullptr);
        }
        beginTest("Existing notes merge immutably and undo restores original pattern");
        {
            const auto base = motion::MidiNotes::create({{1, 0, .25, 72, 90, 1}}).source;
            Fixture f; expect(f.initialise(base).wasOk()); f.position = 3;
            expect(f.session->start(f.clipId).wasOk());
            const auto start = f.recorder.transportStart(); expect(start && *start == 300);
            expect(f.block()); f.session->stop(); f.acknowledge(); expect(f.drain());
            expect(f.clip().midi != base && f.clip().midi != nullptr);
            if (f.clip().midi != nullptr) { expectEquals(static_cast<int>(f.clip().midi->notes().size()), 2); }
            expectEquals(static_cast<int>(base->notes().size()), 1);
            expect(f.undo.undo()); expect(f.clip().midi == base); expect(!f.undo.canUndo());
        }
        beginTest("Cancel before start acknowledgement and after conversion dispatch never edit history");
        for (const bool dispatched : {false, true}) {
            Fixture f; expect(f.initialise().wasOk()); const auto revision = f.document.revision();
            expect(f.session->start(f.clipId).wasOk());
            if (dispatched) { expect(f.block()); f.session->stop(); f.acknowledge(); f.session->poll(); }
            f.session->cancel(); f.acknowledge(); expect(f.drain());
            expect(f.clip().midi == nullptr && f.document.revision() == revision && !f.undo.canUndo());
            expect(f.recorder.state() == motion::MidiRecording::State::idle);
        }
        beginTest("Unrelated edits during a take keep it; the take commits after them");
        {
            Fixture f; expect(f.initialise().wasOk()); expect(f.session->start(f.clipId).wasOk());
            expect(f.block());
            f.document.edit("Other edit", [](motion::Project& p) { p.duration += 1; p.name = "Renamed"; });
            f.session->poll();
            expect(f.session->busy() && !f.session->hasError(), "An unrelated edit does not cancel recording");
            f.session->stop(); f.acknowledge(); expect(f.drain());
            expect(f.clip().midi != nullptr && f.undo.getUndoDescription() == "Record MIDI notes");
        }
        beginTest("Clip timing changes, target removal and project replacement invalidate pending completion");
        for (int change = 0; change < 3; ++change) {
            Fixture f; expect(f.initialise().wasOk()); expect(f.session->start(f.clipId).wasOk());
            expect(f.block()); f.session->stop(); f.acknowledge(); f.session->poll();
            if (change == 0) { f.document.edit("Move clip", [](motion::Project& p) { p.tracks.change(0).clips[0].start += .5; }); }
            else if (change == 1) { f.document.edit("Remove target", [](motion::Project& p) { p.tracks.change(0).clips.clear(); }); }
            else { auto project = f.document.mainProject(); f.document.reset(std::move(project)); }
            const auto revision = f.document.revision(); const auto description = f.undo.getUndoDescription();
            expect(f.drain()); expect(f.document.revision() == revision && f.undo.getUndoDescription() == description);
            if (change != 1) { expect(f.clip().midi == nullptr); }
        }
        beginTest("A device stop between the readiness check and arming refuses the take");
        {
            Fixture f; expect(f.initialise().wasOk()); f.deviceRunning = false;
            const auto revision = f.document.revision();
            expect(f.session->start(f.clipId).failed());
            expect(!f.session->busy() && f.monitored == 0 && f.recorder.state() == motion::MidiRecording::State::idle);
            f.deviceRunning = true;
            expect(f.session->start(f.clipId).wasOk(), "A refused arm does not consume the session");
            f.session->cancel(); f.acknowledge(); expect(f.drain());
            expect(f.document.revision() == revision && !f.undo.canUndo());
        }
        beginTest("Empty takes and unavailable transport leave the project untouched");
        {
            Fixture f; expect(f.initialise().wasOk()); f.ready = false;
            expect(f.session->start(f.clipId).failed()); expect(!f.session->busy());
            f.ready = true; const auto revision = f.document.revision();
            expect(f.session->start(f.clipId).wasOk()); expect(f.block(false));
            f.session->stop(); f.acknowledge(); expect(f.drain());
            expect(f.clip().midi == nullptr && f.document.revision() == revision && !f.undo.canUndo());
            expect(f.session->message() == "No notes recorded.");
        }
    }
};
static MotionMidiRecordingSessionTest motionMidiRecordingSessionTest;
