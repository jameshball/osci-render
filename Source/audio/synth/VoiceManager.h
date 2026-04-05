// Voice management adapted from Vital's VoiceHandler (mtytel/vital, GPL-3.0).
// Original copyright: Copyright 2013-2019 Matt Tytel.

#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

enum class VoiceEvent {
    Invalid,
    On,
    Off,
    Kill,
};

enum class KeyState {
    Triggering,
    Held,
    Sustained,
    Released,
    Dead,
};

struct VoiceState {
    VoiceEvent event = VoiceEvent::Invalid;
    int midiNote = 0;
    float velocity = 0.0f;
    float lastNote = 0.0f;
    float lift = 0.5f;
    int notePressed = 0;
    int noteCount = 0;
    int channel = 0;
    bool sostenutoPushed = false;
    bool revoicing = false;
    double revoiceSourceFreq = 0.0;
};

class ManagedVoice {
public:
    ManagedVoice() = default;

    const VoiceState& getState() const { return voiceState; }
    KeyState getKeyState() const { return currentKeyState; }
    KeyState getLastKeyState() const { return previousKeyState; }

    bool isHeld() const { return currentKeyState == KeyState::Held; }
    bool isSustained() const { return currentKeyState == KeyState::Sustained; }
    bool isReleased() const { return currentKeyState == KeyState::Released; }
    bool isDead() const { return currentKeyState == KeyState::Dead; }
    bool isSostenuto() const { return voiceState.sostenutoPushed; }

    void setSostenuto(bool s) { voiceState.sostenutoPushed = s; }
    void setLiftVelocity(float lift) { voiceState.lift = lift; }

    void activate(int midiNote, float velocity, float lastNote,
                  int notePressed, int noteCount, int channel) {
        voiceState.event = VoiceEvent::On;
        voiceState.midiNote = midiNote;
        voiceState.velocity = velocity;
        voiceState.lastNote = lastNote;
        voiceState.lift = 0.5f;
        voiceState.notePressed = notePressed;
        voiceState.noteCount = noteCount;
        voiceState.channel = channel;
        voiceState.sostenutoPushed = false;
        voiceState.revoicing = false;
        voiceState.revoiceSourceFreq = 0.0;
        setKeyState(KeyState::Triggering);
    }

    void setRevoicing(bool r) { voiceState.revoicing = r; }
    void setRevoiceSourceFreq(double f) { voiceState.revoiceSourceFreq = f; }

    void deactivate() {
        voiceState.event = VoiceEvent::Off;
        setKeyState(KeyState::Released);
    }

    void kill() {
        voiceState.event = VoiceEvent::Kill;
    }

    void sustain() {
        previousKeyState = currentKeyState;
        currentKeyState = KeyState::Sustained;
    }

    void markDead() {
        setKeyState(KeyState::Dead);
    }

    // Transition Triggering → Held after first render pass.
    void completeVoiceEvent() {
        if (currentKeyState == KeyState::Triggering)
            setKeyState(KeyState::Held);
    }

    void setJuceVoice(juce::SynthesiserVoice* v) { juceVoice = v; }
    juce::SynthesiserVoice* getJuceVoice() const { return juceVoice; }

    void setIndex(int idx) { voiceIndex = idx; }
    int getIndex() const { return voiceIndex; }

private:
    void setKeyState(KeyState ks) {
        previousKeyState = currentKeyState;
        currentKeyState = ks;
    }

    VoiceState voiceState;
    KeyState currentKeyState = KeyState::Dead;
    KeyState previousKeyState = KeyState::Dead;
    juce::SynthesiserVoice* juceVoice = nullptr;
    int voiceIndex = -1;
};

// Interface for voice lifecycle callbacks. The processor implements this
// to bridge VoiceManager events to ShapeVoice.
class VoiceManagerClient {
public:
    virtual ~VoiceManagerClient() = default;

    virtual void voiceActivated(ManagedVoice& mv, bool isLegato) = 0;
    virtual void voiceDeactivated(ManagedVoice& mv) = 0;
    virtual void voiceKilled(ManagedVoice& mv) = 0;
    virtual bool isVoiceSilent(ManagedVoice& mv) const = 0;
    virtual double getVoiceFrequency(const ManagedVoice& mv) const = 0;
    virtual void captureDrawingState(ManagedVoice& mv) = 0;
    virtual void restoreDrawingState(ManagedVoice& target, const ManagedVoice& source) = 0;
};

class VoiceManager {
public:
    static constexpr int kNumMidiChannels = 16;
    static constexpr int kMaxPolyphony = 32;

    VoiceManager();

    void addVoice(juce::SynthesiserVoice* voice);
    void removeVoice(int index);

    void addSound(juce::SynthesiserSound* sound);
    void clearSounds();

    void setPolyphony(int polyphony);
    int getPolyphony() const { return polyphony; }
    void setLegato(bool value) { legato = value; }
    bool isLegato() const { return legato; }
    void setClient(VoiceManagerClient* c) { client = c; }
    void setCurrentPlaybackSampleRate(double rate);
    double getSampleRate() const { return sampleRate; }

    int getNumPressedNotes() const { return static_cast<int>(pressedNotes.size()); }
    double getLastPlayedNoteFreq() const { return lastPlayedNoteFreq.load(std::memory_order_relaxed); }
    int getNumActiveVoices() const { return static_cast<int>(activeVoices.size()); }

    int getNumVoices() const {
        juce::SpinLock::ScopedLockType sl(lock);
        return static_cast<int>(allVoices.size());
    }
    juce::SynthesiserVoice* getVoice(int index) const;
    ManagedVoice* getManagedVoice(int index) const;

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                         const juce::MidiBuffer& midiMessages,
                         int startSample, int numSamples);

    void handleMidiEvent(const juce::MidiMessage& m);

private:
    void noteOn(int note, float velocity, int channel);
    void noteOff(int note, float lift, int channel);
    void sustainOn(int channel);
    void sustainOff(int channel);
    void sostenutoOn(int channel);
    void sostenutoOff(int channel);
    void allNotesOff();
    void allSoundsOff();

    ManagedVoice* grabVoice(ManagedVoice** restoreSource = nullptr);
    ManagedVoice* grabFreeVoice();
    ManagedVoice* grabVoiceOfType(KeyState keyState);
    ManagedVoice* findVictim();
    ManagedVoice* getVoiceToKill(int maxVoices);
    int grabNextUnplayedPressedNote();
    bool isNotePlaying(int note, int channel) const;

    void renderVoices(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples);
    void checkVoiceKiller();

    static constexpr int kChannelShift = 8;
    static constexpr int kNoteMask = (1 << kChannelShift) - 1;
    static int combineNoteChannel(int note, int channel) { return (channel << kChannelShift) + note; }
    static int getNote(int value) { return value & kNoteMask; }
    static int getChannel(int value) { return value >> kChannelShift; }

    VoiceManagerClient* client = nullptr;
    double sampleRate = 44100.0;
    std::atomic<int> polyphony{1};
    std::atomic<bool> legato{false};

    std::atomic<double> lastPlayedNoteFreq { 0.0 };
    float lastPlayedNote = -1.0f;
    int totalNotes = 0;

    std::vector<int> pressedNotes;

    std::vector<std::unique_ptr<ManagedVoice>> allVoices;
    std::vector<std::unique_ptr<juce::SynthesiserVoice>> ownedVoices;
    std::vector<ManagedVoice*> activeVoices;
    std::vector<ManagedVoice*> freeVoices;

    bool sustainState[kNumMidiChannels] = {};
    bool sostenutoState[kNumMidiChannels] = {};
    float pitchWheelValues[kNumMidiChannels] = {};

    juce::ReferenceCountedArray<juce::SynthesiserSound> sounds;

    juce::SpinLock lock;
};
