#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"

class MotionMidiDocumentTest : public juce::UnitTest {
public:
    MotionMidiDocumentTest() : juce::UnitTest("Motion MIDI document", "MotionMidi") {}
    void runTest() override {
        juce::UndoManager undo;
        motion::Document document(undo);
        auto source = std::make_shared<motion::Asset>();
        source->id = document.newId(); source->name = "triangle.obj"; source->extension = ".obj";
        const juce::String geometry("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        source->data.append(geometry.toRawUTF8(), geometry.getNumBytesAsUTF8());
        beginTest("MIDI source bytes, import context and independent edits survive XML text round trip");
        expect(motion::Document::decodeAsset(*source).wasOk());
        auto midi = std::make_shared<motion::Asset>();
        midi->id = document.newId(); midi->name = "phrase.mid"; midi->extension = ".mid"; midi->midiImportBpm = 170;
        // SMPTE 25 fps, 100 ticks/frame. One note lasts exactly one second;
        // include a controller to verify the unsupported-event warning persists.
        const unsigned char bytes[] { 'M','T','h','d',0,0,0,6,0,0,0,1,0xe7,100,
            'M','T','r','k',0,0,0,17,0,0xb0,64,127,0,0x90,60,100,0x93,0x44,0x80,60,0,0,0xff,0x2f,0 };
        midi->data.append(bytes, sizeof(bytes));
        const auto decoded = motion::Document::decodeAsset(*midi);
        expect(decoded.wasOk(), decoded.getErrorMessage());
        if (decoded.failed()) { return; }
        expectEquals(midi->midiIgnoredEvents, 1);
        auto clip = motion::Document::makeClip(document.newId(), *source, 0);
        clip.midiAsset = midi->id; clip.midi = midi->midi;
        auto sibling = clip; sibling.id = document.newId(); sibling.start = 10;
        auto note = clip.midi->notes()[0];
        note.id = std::numeric_limits<std::uint64_t>::max();
        note.start = 1.0 / 7; note.duration = 11.0 / 13; note.pitch = 71;
        const auto edited = motion::MidiNotes::create({ note });
        expect(static_cast<bool>(edited)); clip.midi = edited.source;
        motion::Track track; track.id = document.newId(); track.name = "Instrument";
        expect(track.insert(clip)); expect(track.insert(sibling));
        document.edit("Assign performance", [&](motion::Project& project) { project.assets = { source, midi }; project.tracks = { track }; });
        const auto xml = juce::parseXML(document.save().toString());
        expect(xml != nullptr);
        if (xml == nullptr) { return; }
        juce::UndoManager restoredUndo;
        motion::Document restored(restoredUndo);
        const auto loaded = restored.load(*xml);
        expect(loaded.wasOk(), loaded.getErrorMessage());
        if (loaded.failed()) { return; }
        const auto& restoredAsset = *restored.project().assets[1];
        expect(restoredAsset.data == midi->data);
        expectEquals(restoredAsset.midiImportBpm, 170.0);
        expectEquals(restoredAsset.midiSuggestedBpm, 170.0);
        expectEquals(restoredAsset.midiIgnoredEvents, 1);
        const auto& restoredClips = restored.project().tracks[0].clips;
        const auto& actual = restoredClips[0].midi->notes()[0];
        expect(actual.id == note.id && actual.start == note.start && actual.duration == note.duration);
        expectEquals(actual.pitch, 71);
        expectEquals(restoredClips[1].midi->notes()[0].pitch, 60);
        expect(restoredClips[0].midiAsset == midi->id);

        beginTest("Undo shares original pattern without touching sibling instances");
        const auto before = document.project().tracks[0].clips[0].midi;
        auto revisedNote = note; revisedNote.velocity = 43;
        const auto revised = before->withNote(revisedNote);
        expect(static_cast<bool>(revised));
        document.edit("Change note velocity", [&](motion::Project& project) { project.tracks[0].clips[0].midi = revised.source; });
        expect(document.project().tracks[0].clips[1].midi == midi->midi);
        expect(undo.undo()); expect(document.project().tracks[0].clips[0].midi == before);
        expect(undo.redo()); expect(document.project().tracks[0].clips[0].midi == revised.source);

        beginTest("Invalid references, duplicate IDs, numeric limits and duplicate patterns reject atomically");
        const auto assertRejected = [&](const std::function<void(juce::XmlElement&)>& damage) {
            auto invalid = juce::parseXML(xml->toString());
            damage(*invalid);
            const auto beforeText = restored.save().toString();
            expect(restored.load(*invalid).failed());
            expect(restored.save().toString() == beforeText);
        };
        const auto pattern = [](juce::XmlElement& root) { return root.getChildByName("track")->getChildByName("clip")->getChildByName("midi"); };
        assertRejected([&](auto& root) { pattern(root)->setAttribute("asset", juce::String(source->id)); });
        assertRejected([&](auto& root) { pattern(root)->setAttribute("asset", "999999"); });
        assertRejected([&](auto& root) { pattern(root)->setAttribute("asset", "garbage"); });
        assertRejected([&](auto& root) { pattern(root)->getChildByName("note")->setAttribute("duration", -1); });
        assertRejected([&](auto& root) { pattern(root)->getChildByName("note")->setAttribute("pitch", "garbage"); });
        assertRejected([&](auto& root) { pattern(root)->getChildByName("note")->setAttribute("start", "0.25suffix"); });
        assertRejected([&](auto& root) { pattern(root)->getChildByName("note")->setAttribute("id", "18446744073709551616"); });
        assertRejected([&](auto& root) { auto* p = pattern(root); p->addChildElement(new juce::XmlElement(*p->getChildByName("note"))); });
        assertRejected([&](auto& root) { auto* c = root.getChildByName("track")->getChildByName("clip"); c->addChildElement(new juce::XmlElement(*pattern(root))); });
        assertRejected([&](auto& root) { root.getChildByName("track")->getChildByName("clip")->setAttribute("asset", juce::String(midi->id)); });

        beginTest("New empty editable patterns need no imported MIDI asset");
        document.edit("Empty pattern", [&](motion::Project& project) {
            auto& target = project.tracks[0].clips[0]; target.midiAsset = 0; target.midi = motion::MidiNotes::create({}).source;
        });
        expect(restored.load(document.save()).wasOk());
        expect(restored.project().tracks[0].clips[0].midi->notes().empty());
    }
};
static MotionMidiDocumentTest motionMidiDocumentTest;
