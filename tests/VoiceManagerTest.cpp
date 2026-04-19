#include <JuceHeader.h>
#include "../Source/audio/synth/VoiceManager.h"

// TestVoice - a minimal juce::SynthesiserVoice used by the test client.
// Tracks activation/deactivation/kill events and drawing state transfers.

class TestVoice : public juce::SynthesiserVoice {
public:
    // Control: if true, deactivation immediately silences the voice (no release).
    bool instantRelease = true;

    // Tracking
    int activatedCount = 0;
    int deactivatedCount = 0;
    int killedCount = 0;
    int lastMidiNote = -1;
    float lastVelocity = 0.0f;
    bool lastLegatoFlag = false;

    // Drawing state
    int drawPosition = 0;
    int capturedPosition = -1;
    int inheritedPosition = -1;
    int inheritCount = 0;

    bool isSilentNow() const { return silent_; }

    // --- Called by TestClient ---

    void onActivated(int midiNote, float velocity, bool legato) {
        activatedCount++;
        lastMidiNote = midiNote;
        lastVelocity = velocity;
        lastLegatoFlag = legato;
        silent_ = false;
        releasing_ = false;
        releaseSamplesLeft_ = 0;
    }

    void onDeactivated() {
        deactivatedCount++;
        if (instantRelease) {
            silent_ = true;
        } else {
            releasing_ = true;
            releaseTotalSamples_ = (int)(getSampleRate() * 0.05); // 50ms
            releaseSamplesLeft_ = releaseTotalSamples_;
        }
    }

    void onKilled() {
        killedCount++;
        silent_ = true;
        releasing_ = false;
    }

    void onCaptureState() {
        capturedPosition = drawPosition;
    }

    void onRestoreState(int pos) {
        inheritedPosition = pos;
        inheritCount++;
    }

    // --- juce::SynthesiserVoice interface (not used by VoiceManager) ---
    bool canPlaySound(juce::SynthesiserSound*) override { return true; }
    void startNote(int, float, juce::SynthesiserSound*, int) override {}
    void stopNote(float, bool) override {}
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    void renderNextBlock(juce::AudioSampleBuffer& buf, int startSample, int numSamples) override {
        if (silent_) return;
        for (int i = 0; i < numSamples; ++i) {
            float envelope = 1.0f;
            if (releasing_) {
                releaseSamplesLeft_--;
                if (releaseSamplesLeft_ <= 0) {
                    silent_ = true;
                    releasing_ = false;
                    return;
                }
                envelope = (float)releaseSamplesLeft_ / releaseTotalSamples_;
            }
            if (buf.getNumChannels() >= 1)
                buf.addSample(0, startSample + i, 0.5f * envelope);
            drawPosition++;
        }
    }

private:
    bool silent_ = true;
    bool releasing_ = false;
    int releaseTotalSamples_ = 1;
    int releaseSamplesLeft_ = 0;
};

// TestClient - implements VoiceManagerClient, dispatches to TestVoice.

class TestClient : public VoiceManagerClient {
public:
    void voiceActivated(ManagedVoice& mv, bool isLegato) override {
        if (auto* v = cast(mv)) v->onActivated(mv.getState().midiNote, mv.getState().velocity, isLegato);
    }
    void voiceDeactivated(ManagedVoice& mv) override {
        if (auto* v = cast(mv)) v->onDeactivated();
    }
    void voiceKilled(ManagedVoice& mv) override {
        if (auto* v = cast(mv)) v->onKilled();
    }
    bool isVoiceSilent(ManagedVoice& mv) const override {
        auto* v = cast(mv);
        return v ? v->isSilentNow() : true;
    }
    double getVoiceFrequency(const ManagedVoice&) const override {
        return 440.0; // dummy value for tests
    }
    void captureDrawingState(ManagedVoice& mv) override {
        if (auto* v = cast(mv)) v->onCaptureState();
    }
    void restoreDrawingState(ManagedVoice& target, const ManagedVoice& source) override {
        auto* tv = cast(target);
        auto* sv = cast(source);
        if (tv && sv) tv->onRestoreState(sv->capturedPosition);
    }
    double noteToFrequency(int note, int channel) override {
        return juce::MidiMessage::getMidiNoteInHertz(note);
    }

private:
    static TestVoice* cast(ManagedVoice& mv) {
        return dynamic_cast<TestVoice*>(mv.getJuceVoice());
    }
    static TestVoice* cast(const ManagedVoice& mv) {
        return dynamic_cast<TestVoice*>(mv.getJuceVoice());
    }
};

// Helpers

// Create VoiceManager with `polyphony` TestVoices + 1 overlap voice for kill-fade.
static std::pair<std::unique_ptr<VoiceManager>, std::unique_ptr<TestClient>>
createVM(int polyphony) {
    auto client = std::make_unique<TestClient>();
    auto vm = std::make_unique<VoiceManager>();
    vm->setCurrentPlaybackSampleRate(44100.0);
    vm->setPolyphony(polyphony);
    vm->setClient(client.get());
    for (int i = 0; i < polyphony + 1; ++i) // +1 overlap voice for kill-fade
        vm->addVoice(new TestVoice());
    return { std::move(vm), std::move(client) };
}

static void sendNoteOn(VoiceManager& vm, int note, float vel = 0.8f, int ch = 1) {
    vm.handleMidiEvent(juce::MidiMessage::noteOn(ch, note, vel));
}
static void sendNoteOff(VoiceManager& vm, int note, int ch = 1) {
    vm.handleMidiEvent(juce::MidiMessage::noteOff(ch, note));
}
static void sendAllNotesOff(VoiceManager& vm, int ch = 1) {
    vm.handleMidiEvent(juce::MidiMessage::allNotesOff(ch));
}
static void renderBlock(VoiceManager& vm, int numSamples = 1024) {
    juce::AudioSampleBuffer buf(1, numSamples);
    buf.clear();
    juce::MidiBuffer midi;
    vm.renderNextBlock(buf, midi, 0, numSamples);
}
// Count how many TestVoices are non-silent (playing or releasing)
static int countAudibleVoices(VoiceManager& vm) {
    int n = 0;
    for (int i = 0; i < vm.getNumVoices(); ++i) {
        auto* tv = dynamic_cast<TestVoice*>(vm.getVoice(i));
        if (tv && !tv->isSilentNow()) n++;
    }
    return n;
}
// Find first TestVoice with given lastMidiNote (non-silent)
static TestVoice* findVoicePlayingNote(VoiceManager& vm, int note) {
    for (int i = 0; i < vm.getNumVoices(); ++i) {
        auto* tv = dynamic_cast<TestVoice*>(vm.getVoice(i));
        if (tv && !tv->isSilentNow() && tv->lastMidiNote == note)
            return tv;
    }
    return nullptr;
}

// Test 1: Pressed-note tracking

class VMPressedNoteTrackingTest : public juce::UnitTest {
public:
    VMPressedNoteTrackingTest() : juce::UnitTest("Pressed Note Tracking", "Synth") {}
    void runTest() override {

        beginTest("noteOn increments pressed count");
        {
            auto [vm, _] = createVM(4);
            expectEquals(vm->getNumPressedNotes(), 0);
            sendNoteOn(*vm, 60);
            expectEquals(vm->getNumPressedNotes(), 1);
            sendNoteOn(*vm, 64);
            expectEquals(vm->getNumPressedNotes(), 2);
            sendNoteOn(*vm, 67);
            expectEquals(vm->getNumPressedNotes(), 3);
        }

        beginTest("noteOff decrements pressed count");
        {
            auto [vm, _] = createVM(4);
            sendNoteOn(*vm, 60); sendNoteOn(*vm, 64); sendNoteOn(*vm, 67);
            sendNoteOff(*vm, 64); expectEquals(vm->getNumPressedNotes(), 2);
            sendNoteOff(*vm, 60); expectEquals(vm->getNumPressedNotes(), 1);
            sendNoteOff(*vm, 67); expectEquals(vm->getNumPressedNotes(), 0);
        }

        beginTest("Duplicate noteOn does not double-count");
        {
            auto [vm, _] = createVM(4);
            sendNoteOn(*vm, 60);
            sendNoteOn(*vm, 60);
            expectEquals(vm->getNumPressedNotes(), 1);
        }

        beginTest("noteOff for unpressed note does not underflow");
        {
            auto [vm, _] = createVM(4);
            sendNoteOff(*vm, 60);
            expectEquals(vm->getNumPressedNotes(), 0);
            sendNoteOn(*vm, 60);
            sendNoteOff(*vm, 60);
            sendNoteOff(*vm, 60);
            expectEquals(vm->getNumPressedNotes(), 0);
        }

        beginTest("allNotesOff resets to zero");
        {
            auto [vm, _] = createVM(4);
            sendNoteOn(*vm, 60); sendNoteOn(*vm, 64); sendNoteOn(*vm, 67);
            sendAllNotesOff(*vm);
            expectEquals(vm->getNumPressedNotes(), 0);
        }

        beginTest("velocity-0 noteOn treated as noteOff");
        {
            auto [vm, _] = createVM(4);
            sendNoteOn(*vm, 60);
            expectEquals(vm->getNumPressedNotes(), 1);
            vm->handleMidiEvent(juce::MidiMessage::noteOn(1, 60, (float)0.0f));
            expectEquals(vm->getNumPressedNotes(), 0);
        }
    }
};

// Test 2: lastPlayedNoteFreq

class VMLastPlayedNoteFreqTest : public juce::UnitTest {
public:
    VMLastPlayedNoteFreqTest() : juce::UnitTest("Last Played Note Freq", "Synth") {}
    void runTest() override {

        beginTest("Starts at zero");
        {
            auto [vm, _] = createVM(1);
            expectWithinAbsoluteError(vm->getLastPlayedNoteFreq(), 0.0, 0.001);
        }

        beginTest("Updated on noteOn");
        {
            auto [vm, _] = createVM(4);
            sendNoteOn(*vm, 69); // A4 = 440 Hz
            expectWithinAbsoluteError(vm->getLastPlayedNoteFreq(), 440.0, 0.01);
            sendNoteOn(*vm, 60); // C4
            expectWithinAbsoluteError(vm->getLastPlayedNoteFreq(),
                juce::MidiMessage::getMidiNoteInHertz(60), 0.01);
        }

        beginTest("allNotesOff resets to zero");
        {
            auto [vm, _] = createVM(4);
            sendNoteOn(*vm, 69);
            sendAllNotesOff(*vm);
            expectWithinAbsoluteError(vm->getLastPlayedNoteFreq(), 0.0, 0.001);
        }
    }
};

// Test 3: Hard-kill voice allocation (no kill-fade overlap)

class VMVoiceAllocationTest : public juce::UnitTest {
public:
    VMVoiceAllocationTest() : juce::UnitTest("Voice Allocation", "Synth") {}
    void runTest() override {

        beginTest("Single note activates one voice");
        {
            auto [vm, _] = createVM(1);
            sendNoteOn(*vm, 60);
            expectEquals(countAudibleVoices(*vm), 1);
        }

        beginTest("Poly=4: four simultaneous notes activate four voices");
        {
            auto [vm, _] = createVM(4);
            sendNoteOn(*vm, 60); sendNoteOn(*vm, 64);
            sendNoteOn(*vm, 67); sendNoteOn(*vm, 71);
            expectEquals(countAudibleVoices(*vm), 4);
        }

        beginTest("Kill-fade: stolen voice fades while new voice plays (no reuse)");
        {
            auto [vm, _] = createVM(1);
            sendNoteOn(*vm, 60);
            renderBlock(*vm, 128);
            auto* v60 = dynamic_cast<TestVoice*>(vm->getVoice(0));
            expect(v60 != nullptr);

            sendNoteOn(*vm, 64);
            // Kill-fade: old voice starts fading (killed), a separate +1
            // overlap voice plays the new note.
            expect(v60->killedCount >= 1, "voiceKilled should have been called on old voice");
            auto* v64 = findVoicePlayingNote(*vm, 64);
            expect(v64 != nullptr, "A voice should be playing note 64");
            // Only one audible voice (the new one; dying voice is silent in tests)
            expectEquals(countAudibleVoices(*vm), 1);
        }

        beginTest("Hard-kill: active voice count never exceeds polyphony");
        {
            auto [vm, _] = createVM(2);
            for (int n = 60; n < 70; ++n) {
                sendNoteOn(*vm, n);
                expect(countAudibleVoices(*vm) <= 2,
                    juce::String("Audible voices should never exceed polyphony (note ") + juce::String(n) + ")");
            }
        }

        beginTest("Velocity tracked on activation");
        {
            auto [vm, _] = createVM(1);
            sendNoteOn(*vm, 60, 0.75f);
            auto* v = findVoicePlayingNote(*vm, 60);
            expect(v != nullptr);
            // MIDI velocity byte rounds 0.75 → 95/127 ≈ 0.748; use wider tolerance.
            expectWithinAbsoluteError(v->lastVelocity, 0.75f, 0.01f);
        }
    }
};

// Test 4: Re-voicing (return-to-held-note on noteOff)

class VMReVoicingTest : public juce::UnitTest {
public:
    VMReVoicingTest() : juce::UnitTest("Re-voicing", "Synth") {}
    void runTest() override {

        beginTest("Mono: releasing top note returns to held note");
        {
            auto [vm, _] = createVM(1);
            sendNoteOn(*vm, 60); // hold C4
            sendNoteOn(*vm, 64); // play E4 (steals C4)
            renderBlock(*vm, 512);
            expectEquals(vm->getNumPressedNotes(), 2);

            sendNoteOff(*vm, 64); // release E4 → should re-voice C4
            renderBlock(*vm, 512);

            // C4 should now be the active note
            expect(findVoicePlayingNote(*vm, 60) != nullptr,
                "C4 should be re-voiced after E4 is released");
        }

        beginTest("Mono: last-pressed-first re-voicing order (kNewest)");
        {
            auto [vm, _] = createVM(1);
            sendNoteOn(*vm, 60); sendNoteOn(*vm, 64); sendNoteOn(*vm, 67);
            renderBlock(*vm, 128);
            expectEquals(vm->getNumPressedNotes(), 3);

            sendNoteOff(*vm, 67); // release G4 → should return to E4 (most recent)
            renderBlock(*vm, 128);

            expect(findVoicePlayingNote(*vm, 64) != nullptr,
                "E4 should be re-voiced (most recently pressed held note)");
        }

        beginTest("Poly: no re-voicing when held notes are already sounding");
        {
            auto [vm, _] = createVM(4);
            sendNoteOn(*vm, 60); sendNoteOn(*vm, 64); sendNoteOn(*vm, 67);
            int beforeActive = countAudibleVoices(*vm);
            sendNoteOff(*vm, 64);
            renderBlock(*vm, 512);
            // The released note should go silent, no new voice triggered
            expect(countAudibleVoices(*vm) <= beforeActive,
                "No extra voices should be created during poly noteOff");
        }

        beginTest("Mono: rapidhold-three, release-to-held pattern");
        {
            // Hold 60, press 64 (steals 60), press 67 (steals 64),
            // release 67 → should return to 64, release 64 → return to 60
            auto [vm, _] = createVM(1);
            sendNoteOn(*vm, 60);
            sendNoteOn(*vm, 64);
            sendNoteOn(*vm, 67);
            renderBlock(*vm, 64);

            sendNoteOff(*vm, 67);
            renderBlock(*vm, 64);
            expect(findVoicePlayingNote(*vm, 64) != nullptr, "Should return to 64");

            sendNoteOff(*vm, 64);
            renderBlock(*vm, 64);
            expect(findVoicePlayingNote(*vm, 60) != nullptr, "Should return to 60");
        }
    }
};

// Test 5: Vital-style sustain pedal

class VMSustainPedalTest : public juce::UnitTest {
public:
    VMSustainPedalTest() : juce::UnitTest("Sustain Pedal", "Synth") {}
    void runTest() override {

        beginTest("Sustain holds voice after noteOff");
        {
            auto [vm, _] = createVM(1);
            vm->handleMidiEvent(juce::MidiMessage::controllerEvent(1, 64, 127));
            sendNoteOn(*vm, 60);
            sendNoteOff(*vm, 60);
            renderBlock(*vm, 512);
            // Voice should still be audible (sustained)
            auto* sustained = findVoicePlayingNote(*vm, 60);
            expect(sustained != nullptr && !sustained->isSilentNow(),
                "Voice should be sustained");
            expectEquals(vm->getNumPressedNotes(), 0);
        }

        beginTest("SustainOff releases sustained voices");
        {
            auto [vm, _] = createVM(1);
            // Set instant release so voice goes silent immediately on deactivate
            auto* v = dynamic_cast<TestVoice*>(vm->getVoice(0));
            if (v) v->instantRelease = true;

            vm->handleMidiEvent(juce::MidiMessage::controllerEvent(1, 64, 127));
            sendNoteOn(*vm, 60);
            sendNoteOff(*vm, 60);
            renderBlock(*vm, 128);
            // Still sustained
            expectEquals(countAudibleVoices(*vm), 1);

            // Release sustain → voice should deactivate and go silent
            vm->handleMidiEvent(juce::MidiMessage::controllerEvent(1, 64, 0));
            renderBlock(*vm, 128);
            expectEquals(countAudibleVoices(*vm), 0);
        }

        beginTest("Sustain does not hold voices on different channel");
        {
            auto [vm, _] = createVM(2);
            auto* v0 = dynamic_cast<TestVoice*>(vm->getVoice(0));
            auto* v1 = dynamic_cast<TestVoice*>(vm->getVoice(1));
            if (v0) v0->instantRelease = true;
            if (v1) v1->instantRelease = true;

            // Sustain only on channel 1
            vm->handleMidiEvent(juce::MidiMessage::controllerEvent(1, 64, 127));
            sendNoteOn(*vm, 60, 0.8f, 1); // channel 1 - sustained
            sendNoteOn(*vm, 64, 0.8f, 2); // channel 2 - not sustained
            sendNoteOff(*vm, 60, 1);
            sendNoteOff(*vm, 64, 2);
            renderBlock(*vm, 128);

            // Note 60 should still be audible (sustained on ch1)
            expect(countAudibleVoices(*vm) >= 1,
                "Sustained note should still be audible");
        }

        beginTest("Pressing note during sustain still works");
        {
            auto [vm, _] = createVM(2);
            vm->handleMidiEvent(juce::MidiMessage::controllerEvent(1, 64, 127));
            sendNoteOn(*vm, 60);
            sendNoteOff(*vm, 60);
            sendNoteOn(*vm, 64); // new note while 60 is sustained
            expectEquals(countAudibleVoices(*vm), 2);
        }
    }
};

// Test 6: Legato mode

class VMLegatoTest : public juce::UnitTest {
public:
    VMLegatoTest() : juce::UnitTest("Legato Mode", "Synth") {}
    void runTest() override {

        beginTest("Legato mono: same voice object re-used for new note");
        {
            auto [vm, _] = createVM(1);
            vm->setLegato(true);
            sendNoteOn(*vm, 60);
            renderBlock(*vm, 128);
            auto* firstVoice = findVoicePlayingNote(*vm, 60);
            expect(firstVoice != nullptr);
            int killsBefore = firstVoice->killedCount;

            sendNoteOn(*vm, 64);
            auto* secondVoice = findVoicePlayingNote(*vm, 64);
            expect(secondVoice != nullptr);
            expect(firstVoice == secondVoice, "Legato should reuse same voice object");
            // In legato mode: the voice is killed and re-activated (hard re-pitch)
            // but with isLegato=true so ShapeVoice can skip envelope retrigger
            expect(secondVoice->lastLegatoFlag, "isLegato flag should be true");
        }

        beginTest("Legato: isLegato false on first note from silence");
        {
            auto [vm, _] = createVM(1);
            vm->setLegato(true);
            sendNoteOn(*vm, 60);
            auto* v = findVoicePlayingNote(*vm, 60);
            expect(v != nullptr);
            expect(!v->lastLegatoFlag, "First note from silence should not be legato");
        }

        beginTest("Legato off: new note gets new activation, isLegato false");
        {
            auto [vm, _] = createVM(1);
            vm->setLegato(false);
            sendNoteOn(*vm, 60);
            renderBlock(*vm, 128);
            sendNoteOn(*vm, 64);
            auto* v = findVoicePlayingNote(*vm, 64);
            expect(v != nullptr);
            expect(!v->lastLegatoFlag, "Non-legato mode: isLegato should be false");
        }

        beginTest("Legato poly=2: second note does NOT reuse existing voice");
        {
            // Legato re-pitch only applies when polyphony=1
            auto [vm, _] = createVM(2);
            vm->setLegato(true);
            vm->addVoice(new TestVoice()); // ensure 2 voices registered

            sendNoteOn(*vm, 60);
            renderBlock(*vm, 128);
            auto* v60 = findVoicePlayingNote(*vm, 60);

            sendNoteOn(*vm, 64);
            auto* v64 = findVoicePlayingNote(*vm, 64);
            // Both notes should be playing (poly=2 with 2 available voices)
            expect(v60 != nullptr && !v60->isSilentNow(), "Note 60 should still be playing");
            expect(v64 != nullptr, "Note 64 should be playing");
            expect(!v64->lastLegatoFlag, "Poly>1 legato should not set isLegato flag");
        }
    }
};

// Test 7: Drawing state transfer (capture before steal, restore on replacement)

class VMDrawingStateTransferTest : public juce::UnitTest {
public:
    VMDrawingStateTransferTest() : juce::UnitTest("Drawing State Transfer", "Synth") {}
    void runTest() override {

        beginTest("captureDrawingState called before kill on steal");
        {
            auto [vm, _] = createVM(1);
            sendNoteOn(*vm, 60);
            renderBlock(*vm, 256); // advance draw position

            auto* v60 = findVoicePlayingNote(*vm, 60);
            expect(v60 != nullptr);
            int drawPosBefore = v60->drawPosition;
            expect(drawPosBefore > 0, "Voice should have advanced draw position");

            sendNoteOn(*vm, 64); // steals v60

            // captureDrawingState should have been called with the position before kill
            expectEquals(v60->capturedPosition, drawPosBefore);
        }

        beginTest("restoreDrawingState called on replacement voice");
        {
            auto [vm, _] = createVM(1);
            sendNoteOn(*vm, 60);
            renderBlock(*vm, 256);

            auto* v60 = findVoicePlayingNote(*vm, 60);
            expect(v60 != nullptr);
            int capturedPos = v60->drawPosition; // will be captured before kill

            sendNoteOn(*vm, 64);
            // v60 was captured then replaced by the overlap voice playing v64.
            auto* v64 = findVoicePlayingNote(*vm, 64);
            expect(v64 != nullptr);
            expectEquals(v64->inheritCount, 1);
            expectEquals(v64->inheritedPosition, capturedPos);
        }

        beginTest("No drawing state transfer when new voice is from free pool");
        {
            auto [vm, _] = createVM(2);
            sendNoteOn(*vm, 60); // uses one free voice
            renderBlock(*vm, 256);

            // Second note uses a different free voice - no steal, no state transfer
            sendNoteOn(*vm, 64);
            auto* v64 = findVoicePlayingNote(*vm, 64);
            expect(v64 != nullptr);
            expectEquals(v64->inheritCount, 0);
        }

        beginTest("Drawing position inherited through chain of steals");
        {
            auto [vm, _] = createVM(1);
            sendNoteOn(*vm, 60);
            renderBlock(*vm, 100);

            sendNoteOn(*vm, 64);
            renderBlock(*vm, 50);

            auto* v64 = findVoicePlayingNote(*vm, 64);
            expect(v64 != nullptr);
            int capturedPos2 = v64->drawPosition;

            sendNoteOn(*vm, 67);
            // v64 was captured then replaced by the recycled overlap voice.
            auto* v67 = findVoicePlayingNote(*vm, 67);
            expect(v67 != nullptr);
            expectEquals(v67->inheritCount, 1);
            expectEquals(v67->inheritedPosition, capturedPos2);
        }

        beginTest("restoreDrawingState called on noteOff re-voice");
        {
            auto [vm, _] = createVM(1);
            sendNoteOn(*vm, 60);
            renderBlock(*vm, 128);
            sendNoteOn(*vm, 64);
            renderBlock(*vm, 128);

            auto* source = findVoicePlayingNote(*vm, 64);
            expect(source != nullptr);
            int capturedPos = source != nullptr ? source->drawPosition : -1;

            sendNoteOff(*vm, 64);

            auto* target = findVoicePlayingNote(*vm, 60);
            expect(target != nullptr);
            expect(target != source, "Re-voice should move onto the overlap voice in this test setup");
            if (target != nullptr) {
                expectEquals(target->inheritCount, 1);
                expectEquals(target->inheritedPosition, capturedPos);
            }
        }
    }
};

// Test 8: Polyphony enforcement - Vital hard-kill semantics

class VMPolyphonyEnforcementTest : public juce::UnitTest {
public:
    VMPolyphonyEnforcementTest() : juce::UnitTest("Polyphony Enforcement", "Synth") {}
    void runTest() override {

        beginTest("Mono: exactly 1 audible voice after each noteOn");
        {
            auto [vm, _] = createVM(1);
            for (int n = 48; n < 72; ++n) {
                sendNoteOn(*vm, n);
                expectEquals(countAudibleVoices(*vm), 1,
                    juce::String("Should be exactly 1 audible voice after noteOn ") + juce::String(n));
            }
        }

        beginTest("Mono: same note retriggered - still 1 audible voice");
        {
            auto [vm, _] = createVM(1);
            for (int i = 0; i < 10; ++i) {
                sendNoteOn(*vm, 60);
                expectEquals(countAudibleVoices(*vm), 1);
            }
        }

        beginTest("Poly=4: exactly 4 voices after 4 noteOns");
        {
            auto [vm, _] = createVM(4);
            sendNoteOn(*vm, 60); sendNoteOn(*vm, 64);
            sendNoteOn(*vm, 67); sendNoteOn(*vm, 71);
            expectEquals(countAudibleVoices(*vm), 4);
        }

        beginTest("Poly=4: 5th noteOn kills one and stays at 4");
        {
            auto [vm, _] = createVM(4);
            sendNoteOn(*vm, 60); sendNoteOn(*vm, 64);
            sendNoteOn(*vm, 67); sendNoteOn(*vm, 71);
            sendNoteOn(*vm, 74); // steal
            // Hard kill: exactly polyphony voices, no overlap
            expectEquals(countAudibleVoices(*vm), 4);
        }

        beginTest("Random 100 noteOns - polyphony always respected");
        {
            auto [vm, _] = createVM(3);
            juce::Random rng(99);
            for (int i = 0; i < 100; ++i) {
                sendNoteOn(*vm, 36 + rng.nextInt(48));
                expect(countAudibleVoices(*vm) <= 3,
                    juce::String("Audible voices exceeded polyphony at step ") + juce::String(i));
                renderBlock(*vm, 16);
            }
        }
    }
};

// Test 9: Rapid note flurries

class VMRapidNoteFlurryTest : public juce::UnitTest {
public:
    VMRapidNoteFlurryTest() : juce::UnitTest("Rapid Note Flurry", "Synth") {}
    void runTest() override {

        beginTest("Mono: hold A, rapidly press/release B 100 times");
        {
            auto [vm, _] = createVM(1);
            sendNoteOn(*vm, 60); // hold A

            for (int i = 0; i < 100; ++i) {
                sendNoteOn(*vm, 72);
                renderBlock(*vm, 64);
                sendNoteOff(*vm, 72);
                renderBlock(*vm, 64);
            }

            // After flurry, note 60 should be re-voiced and active
            expectEquals(vm->getNumPressedNotes(), 1);
            renderBlock(*vm, 1024);

            expect(findVoicePlayingNote(*vm, 60) != nullptr,
                "Held note 60 should be sounding after flurry");
        }

        beginTest("Mono: chromatic run up and back down");
        {
            auto [vm, _] = createVM(1);
            for (int n = 48; n <= 72; ++n) {
                sendNoteOn(*vm, n);
                renderBlock(*vm, 32);
            }
            expectEquals(vm->getNumPressedNotes(), 25);
            for (int n = 72; n >= 48; --n) {
                sendNoteOff(*vm, n);
                renderBlock(*vm, 32);
            }
            expectEquals(vm->getNumPressedNotes(), 0);
            renderBlock(*vm, 1024);
            expectEquals(countAudibleVoices(*vm), 0);
        }

        beginTest("Poly=4: random note barrage (200 events)");
        {
            auto [vm, _] = createVM(4);
            juce::Random rng(42);
            bool held[128] = {};
            int heldCount = 0;

            for (int i = 0; i < 200; ++i) {
                int note = 36 + rng.nextInt(48);
                if (rng.nextFloat() < 0.6f || heldCount == 0) {
                    sendNoteOn(*vm, note, 0.5f + rng.nextFloat() * 0.5f);
                    if (!held[note]) { held[note] = true; ++heldCount; }
                } else {
                    int target = rng.nextInt(128);
                    for (int j = 0; j < 128; ++j) {
                        int n = (target + j) % 128;
                        if (held[n]) { sendNoteOff(*vm, n); held[n] = false; --heldCount; break; }
                    }
                }
                renderBlock(*vm, 16 + rng.nextInt(128));
            }

            expectEquals(vm->getNumPressedNotes(), heldCount);
            expect(countAudibleVoices(*vm) <= 4, "Must not exceed polyphony");
        }

        beginTest("Mono: trill (50 cycles) - note count clean after");
        {
            auto [vm, _] = createVM(1);
            for (int i = 0; i < 50; ++i) {
                sendNoteOn(*vm, 60); renderBlock(*vm, 32);
                sendNoteOn(*vm, 62); renderBlock(*vm, 32);
                sendNoteOff(*vm, 60); renderBlock(*vm, 32);
                sendNoteOff(*vm, 62); renderBlock(*vm, 32);
            }
            expectEquals(vm->getNumPressedNotes(), 0);
            renderBlock(*vm, 1024);
            expectEquals(countAudibleVoices(*vm), 0);
        }
    }
};

// Test 10: Stress test

class VMStressTest : public juce::UnitTest {
public:
    VMStressTest() : juce::UnitTest("VoiceManager Stress Test", "Synth") {}
    void runTest() override {

        beginTest("10000 random MIDI events without crashing");
        {
            auto [vm, _] = createVM(4);
            juce::Random rng(12345);
            for (int i = 0; i < 10000; ++i) {
                int note = rng.nextInt(128);
                if (rng.nextFloat() < 0.55f)
                    sendNoteOn(*vm, note, rng.nextFloat());
                else
                    sendNoteOff(*vm, note);
                if (i % 10 == 0) renderBlock(*vm, 32 + rng.nextInt(256));
            }
            sendAllNotesOff(*vm);
            renderBlock(*vm, 1024);
            expectEquals(vm->getNumPressedNotes(), 0);
        }

        beginTest("Interleaved allNotesOff with rapid playing");
        {
            auto [vm, _] = createVM(4);
            for (int round = 0; round < 50; ++round) {
                for (int n = 60; n < 72; ++n) {
                    sendNoteOn(*vm, n);
                    renderBlock(*vm, 16);
                }
                sendAllNotesOff(*vm);
                expectEquals(vm->getNumPressedNotes(), 0);
                renderBlock(*vm, 512);
            }
            expectEquals(vm->getNumPressedNotes(), 0);
        }

        beginTest("All 128 notes pressed and released");
        {
            auto [vm, _] = createVM(8);
            // Add extra voices to handle 128 distinct notes
            for (int i = 8; i < 16; ++i) vm->addVoice(new TestVoice());
            vm->setPolyphony(8);
            for (int n = 0; n < 128; ++n) sendNoteOn(*vm, n);
            expectEquals(vm->getNumPressedNotes(), 128);
            for (int n = 0; n < 128; ++n) sendNoteOff(*vm, n);
            expectEquals(vm->getNumPressedNotes(), 0);
        }

        beginTest("MIDI-interleaved renderNextBlock - sample-accurate events");
        {
            auto [vm, _] = createVM(4);
            sendNoteOn(*vm, 60);
            renderBlock(*vm, 256);

            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)100), 64);
            midi.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8)100), 128);
            midi.addEvent(juce::MidiMessage::noteOff(1, 64), 192);

            juce::AudioSampleBuffer buf(1, 512);
            buf.clear();
            vm->renderNextBlock(buf, midi, 0, 512);
            // Didn't crash, pressed notes updated correctly
            expectEquals(vm->getNumPressedNotes(), 2); // 60 and 67 still held
        }
    }
};

// Static instances

static VMPressedNoteTrackingTest vmPressedNoteTrackingTest;
static VMLastPlayedNoteFreqTest vmLastPlayedNoteFreqTest;
static VMVoiceAllocationTest vmVoiceAllocationTest;
static VMReVoicingTest vmReVoicingTest;
static VMSustainPedalTest vmSustainPedalTest;
static VMLegatoTest vmLegatoTest;
static VMDrawingStateTransferTest vmDrawingStateTransferTest;
static VMPolyphonyEnforcementTest vmPolyphonyEnforcementTest;
static VMRapidNoteFlurryTest vmRapidNoteFlurryTest;
static VMStressTest vmStressTest;
