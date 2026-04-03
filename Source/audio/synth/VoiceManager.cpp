// Voice management adapted from Vital's VoiceHandler (mtytel/vital, GPL-3.0).
// Original copyright: Copyright 2013-2019 Matt Tytel.

#include "VoiceManager.h"

VoiceManager::VoiceManager() {
    pressedNotes.reserve(128);
    allVoices.reserve(kMaxPolyphony + 1);
    activeVoices.reserve(kMaxPolyphony + 1);
    freeVoices.reserve(kMaxPolyphony + 1);
}

void VoiceManager::addVoice(juce::SynthesiserVoice* voice) {
    juce::ScopedLock sl(lock);
    auto mv = std::make_unique<ManagedVoice>();
    mv->setJuceVoice(voice);
    mv->setIndex(static_cast<int>(allVoices.size()));
    voice->setCurrentPlaybackSampleRate(sampleRate);
    freeVoices.push_back(mv.get());
    allVoices.push_back(std::move(mv));
    ownedVoices.emplace_back(voice);
}

void VoiceManager::removeVoice(int index) {
    juce::ScopedLock sl(lock);
    if (index < 0 || index >= static_cast<int>(allVoices.size()))
        return;

    auto* mv = allVoices[index].get();
    auto* jv = mv->getJuceVoice();

    activeVoices.erase(std::remove(activeVoices.begin(), activeVoices.end(), mv), activeVoices.end());
    freeVoices.erase(std::remove(freeVoices.begin(), freeVoices.end(), mv), freeVoices.end());

    allVoices.erase(allVoices.begin() + index);

    ownedVoices.erase(
        std::remove_if(ownedVoices.begin(), ownedVoices.end(),
            [jv](const std::unique_ptr<juce::SynthesiserVoice>& p) { return p.get() == jv; }),
        ownedVoices.end());

    for (int i = 0; i < static_cast<int>(allVoices.size()); ++i)
        allVoices[i]->setIndex(i);
}

void VoiceManager::addSound(juce::SynthesiserSound* sound) {
    sounds.add(sound);
}

void VoiceManager::clearSounds() {
    sounds.clear();
}

void VoiceManager::setPolyphony(int value) {
    polyphony = juce::jlimit(1, kMaxPolyphony, value);

    int numToKill = static_cast<int>(activeVoices.size()) - polyphony;
    for (int i = 0; i < numToKill; ++i) {
        auto* sacrifice = getVoiceToKill(polyphony);
        if (sacrifice != nullptr) {
            sacrifice->kill();
            if (client != nullptr)
                client->voiceKilled(*sacrifice);
        }
    }
}

void VoiceManager::setCurrentPlaybackSampleRate(double rate) {
    sampleRate = rate;
    juce::ScopedLock sl(lock);
    for (auto& mv : allVoices) {
        if (mv->getJuceVoice() != nullptr)
            mv->getJuceVoice()->setCurrentPlaybackSampleRate(rate);
    }
}

juce::SynthesiserVoice* VoiceManager::getVoice(int index) const {
    juce::ScopedLock sl(lock);
    if (index >= 0 && index < static_cast<int>(allVoices.size()))
        return allVoices[index]->getJuceVoice();
    return nullptr;
}

ManagedVoice* VoiceManager::getManagedVoice(int index) const {
    juce::ScopedLock sl(lock);
    if (index >= 0 && index < static_cast<int>(allVoices.size()))
        return allVoices[index].get();
    return nullptr;
}

void VoiceManager::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                    const juce::MidiBuffer& midiMessages,
                                    int startSample, int numSamples) {
    juce::ScopedLock sl(lock);

    auto midiIterator = midiMessages.findNextSamplePosition(startSample);
    bool firstEvent = true;

    const int endSample = startSample + numSamples;

    for (; midiIterator != midiMessages.cend(); ++midiIterator) {
        const auto metadata = *midiIterator;
        if (metadata.samplePosition >= endSample)
            break;

        const int samplePos = metadata.samplePosition;

        if (samplePos > startSample) {
            renderVoices(outputBuffer, startSample, samplePos - startSample);
            startSample = samplePos;
        }

        handleMidiEvent(metadata.getMessage());
        firstEvent = false;
    }

    if (startSample < endSample)
        renderVoices(outputBuffer, startSample, endSample - startSample);
}

void VoiceManager::handleMidiEvent(const juce::MidiMessage& m) {
    if (m.isNoteOn()) {
        noteOn(m.getNoteNumber(), m.getFloatVelocity(), m.getChannel() - 1);
    } else if (m.isNoteOff()) {
        noteOff(m.getNoteNumber(), m.getFloatVelocity(), m.getChannel() - 1);
    } else if (m.isSustainPedalOn()) {
        sustainOn(m.getChannel() - 1);
    } else if (m.isSustainPedalOff()) {
        sustainOff(m.getChannel() - 1);
    } else if (m.isSostenutoPedalOn()) {
        sostenutoOn(m.getChannel() - 1);
    } else if (m.isSostenutoPedalOff()) {
        sostenutoOff(m.getChannel() - 1);
    } else if (m.isAllNotesOff()) {
        allNotesOff();
    } else if (m.isAllSoundOff()) {
        allSoundsOff();
    } else if (m.isPitchWheel()) {
        int ch = m.getChannel() - 1;
        if (ch >= 0 && ch < kNumMidiChannels)
            pitchWheelValues[ch] = (m.getPitchWheelValue() - 8192.0f) / 8192.0f;
        for (auto* mv : activeVoices) {
            if (mv->getState().channel == ch && mv->getJuceVoice() != nullptr)
                mv->getJuceVoice()->pitchWheelMoved(m.getPitchWheelValue());
        }
    } else if (m.isController()) {
        for (auto* mv : activeVoices) {
            if (mv->getJuceVoice() != nullptr)
                mv->getJuceVoice()->controllerMoved(m.getControllerNumber(), m.getControllerValue());
        }
    }
}

void VoiceManager::noteOn(int note, float velocity, int channel) {
    if (velocity == 0.0f) {
        noteOff(note, 0.5f, channel);
        return;
    }

    jassert(channel >= 0 && channel < kNumMidiChannels);

    ManagedVoice* restoreSource = nullptr;
    ManagedVoice* voice = grabVoice(&restoreSource);
    if (voice == nullptr)
        return;

    float prevNote = static_cast<float>(note);
    if (lastPlayedNote >= 0.0f)
        prevNote = lastPlayedNote;
    lastPlayedNote = static_cast<float>(note);
    lastPlayedNoteFreq.store(juce::MidiMessage::getMidiNoteInHertz(note),
                             std::memory_order_relaxed);

    int noteValue = combineNoteChannel(note, channel);

    pressedNotes.erase(std::remove(pressedNotes.begin(), pressedNotes.end(), noteValue),
                       pressedNotes.end());
    pressedNotes.push_back(noteValue);

    bool isLegatoNote = legato
                        && voice->getKeyState() == KeyState::Held;

    totalNotes++;
    voice->activate(note, velocity, prevNote,
                    static_cast<int>(pressedNotes.size()), totalNotes, channel);

    if (client != nullptr) {
        client->voiceActivated(*voice, isLegatoNote);
        if (restoreSource != nullptr)
            client->restoreDrawingState(*voice, *restoreSource);
    }

    activeVoices.push_back(voice);
}

void VoiceManager::noteOff(int note, float lift, int channel) {
    int noteValue = combineNoteChannel(note, channel);
    pressedNotes.erase(std::remove(pressedNotes.begin(), pressedNotes.end(), noteValue),
                       pressedNotes.end());

    auto removeActiveVoice = [this](ManagedVoice* voice) {
        activeVoices.erase(std::remove(activeVoices.begin(), activeVoices.end(), voice),
                           activeVoices.end());
    };

    for (int i = static_cast<int>(activeVoices.size()) - 1; i >= 0; --i) {
        auto* voice = activeVoices[i];
        if (voice->getState().midiNote == note && voice->getState().channel == channel) {
            if (sustainState[channel]) {
                voice->sustain();
                voice->setLiftVelocity(lift);
            } else {
                // Re-voicing: if polyphony <= pressed notes, re-voice this
                // voice to play an unplayed held note.
                if (polyphony <= static_cast<int>(pressedNotes.size())
                    && voice->getState().event != VoiceEvent::Kill) {

                    ManagedVoice* newVoice = voice;
                    ManagedVoice* restoreSource = nullptr;

                    // Capture the dying voice's current frequency before
                    // killing it, so the replacement can glide from the
                    // mid-glide pitch.
                    double sourceFreq = 0.0;
                    if (client != nullptr)
                        sourceFreq = client->getVoiceFrequency(*voice);

                    if (!legato) {
                        restoreSource = voice;
                        if (client != nullptr)
                            client->captureDrawingState(*voice);

                        // Kill-fade path: start fade on old voice, grab
                        // the overlap voice for the re-voiced note.
                        voice->kill();
                        if (client != nullptr)
                            client->voiceKilled(*voice);

                        newVoice = grabFreeVoice();
                        if (newVoice == nullptr) {
                            removeActiveVoice(voice);
                            voice->markDead();
                            freeVoices.push_back(voice);
                            newVoice = grabFreeVoice();
                        }
                        if (newVoice == nullptr)
                            continue;
                    } else {
                        // Legato path: remove from active list, will re-add below.
                        removeActiveVoice(voice);
                    }

                    int oldNoteValue = grabNextUnplayedPressedNote();
                    int oldNote = getNote(oldNoteValue);
                    int oldChannel = getChannel(oldNoteValue);

                    bool isLegatoNote = legato
                                        && newVoice->getKeyState() == KeyState::Held;

                    totalNotes++;
                    newVoice->activate(oldNote, voice->getState().velocity,
                                       lastPlayedNote,
                                       static_cast<int>(pressedNotes.size()) + 1,
                                       totalNotes, oldChannel);
                    newVoice->setRevoicing(true);
                    newVoice->setRevoiceSourceFreq(sourceFreq);

                    lastPlayedNote = static_cast<float>(oldNote);
                    lastPlayedNoteFreq.store(
                        juce::MidiMessage::getMidiNoteInHertz(oldNote),
                        std::memory_order_relaxed);

                    if (client != nullptr) {
                        client->voiceActivated(*newVoice, isLegatoNote);
                        if (restoreSource != nullptr)
                            client->restoreDrawingState(*newVoice, *restoreSource);
                    }

                    activeVoices.push_back(newVoice);
                } else {
                    voice->deactivate();
                    voice->setLiftVelocity(lift);
                    if (client != nullptr)
                        client->voiceDeactivated(*voice);
                }
            }
        }
    }
}

void VoiceManager::sustainOn(int channel) {
    if (channel >= 0 && channel < kNumMidiChannels)
        sustainState[channel] = true;
}

void VoiceManager::sustainOff(int channel) {
    if (channel < 0 || channel >= kNumMidiChannels)
        return;
    sustainState[channel] = false;

    for (auto* voice : activeVoices) {
        if (voice->isSustained() && !voice->isSostenuto() && voice->getState().channel == channel) {
            voice->deactivate();
            if (client != nullptr)
                client->voiceDeactivated(*voice);
        }
    }
}

void VoiceManager::sostenutoOn(int channel) {
    if (channel < 0 || channel >= kNumMidiChannels)
        return;
    sostenutoState[channel] = true;
    for (auto* voice : activeVoices) {
        if (voice->getState().channel == channel)
            voice->setSostenuto(true);
    }
}

void VoiceManager::sostenutoOff(int channel) {
    if (channel < 0 || channel >= kNumMidiChannels)
        return;
    sostenutoState[channel] = false;
    for (auto* voice : activeVoices) {
        if (voice->getState().channel == channel) {
            voice->setSostenuto(false);
            if (voice->isSustained() && !sustainState[channel]) {
                voice->deactivate();
                if (client != nullptr)
                    client->voiceDeactivated(*voice);
            }
        }
    }
}

void VoiceManager::allNotesOff() {
    pressedNotes.clear();
    lastPlayedNoteFreq.store(0.0, std::memory_order_relaxed);
    lastPlayedNote = -1.0f;

    for (auto* voice : activeVoices) {
        voice->deactivate();
        if (client != nullptr)
            client->voiceDeactivated(*voice);
    }

    std::fill(std::begin(sustainState), std::end(sustainState), false);
    std::fill(std::begin(sostenutoState), std::end(sostenutoState), false);
}

void VoiceManager::allSoundsOff() {
    pressedNotes.clear();
    lastPlayedNoteFreq.store(0.0, std::memory_order_relaxed);
    lastPlayedNote = -1.0f;

    for (auto* voice : activeVoices) {
        voice->kill();
        if (client != nullptr)
            client->voiceKilled(*voice);
        voice->markDead();
        freeVoices.push_back(voice);
    }
    activeVoices.clear();

    std::fill(std::begin(sustainState), std::end(sustainState), false);
    std::fill(std::begin(sostenutoState), std::end(sostenutoState), false);
}

ManagedVoice* VoiceManager::grabVoice(ManagedVoice** restoreSource) {
    ManagedVoice* voice = nullptr;

    if (restoreSource != nullptr)
        *restoreSource = nullptr;

    if (static_cast<int>(activeVoices.size()) < polyphony) {
        voice = grabFreeVoice();
        if (voice != nullptr)
            return voice;
    }

    if (legato) {
        // Legato (steal): reuse the same voice object without killing it.
        voice = grabVoiceOfType(KeyState::Released);
        if (voice == nullptr) voice = grabVoiceOfType(KeyState::Sustained);
        if (voice == nullptr) voice = grabVoiceOfType(KeyState::Held);
        if (voice == nullptr) voice = grabVoiceOfType(KeyState::Triggering);
        return voice;
    }

    // Non-legato (kill): kill the victim with a fade, then grab the
    // overlap voice from the free pool for the new note.
    ManagedVoice* victim = findVictim();
    if (victim == nullptr)
        return nullptr;

    if (restoreSource != nullptr)
        *restoreSource = victim;
    if (client != nullptr)
        client->captureDrawingState(*victim);
    victim->kill();
    if (client != nullptr)
        client->voiceKilled(*victim);

    voice = grabFreeVoice();
    if (voice != nullptr)
        return voice;

    // Fallback: no overlap voice available. Hard-stop the victim.
    activeVoices.erase(std::remove(activeVoices.begin(), activeVoices.end(), victim),
                        activeVoices.end());
    victim->markDead();
    freeVoices.push_back(victim);
    return grabFreeVoice();
}

ManagedVoice* VoiceManager::grabFreeVoice() {
    if (!freeVoices.empty()) {
        auto* voice = freeVoices.front();
        freeVoices.erase(freeVoices.begin());
        return voice;
    }
    return nullptr;
}

ManagedVoice* VoiceManager::grabVoiceOfType(KeyState keyState) {
    for (auto it = activeVoices.begin(); it != activeVoices.end(); ++it) {
        if ((*it)->getKeyState() == keyState
            && (*it)->getState().event != VoiceEvent::Kill) {
            auto* voice = *it;
            activeVoices.erase(it);
            return voice;
        }
    }
    return nullptr;
}

ManagedVoice* VoiceManager::findVictim() {
    // Find the best active voice to steal by priority:
    // released → sustained → held → triggering.
    for (KeyState ks : { KeyState::Released, KeyState::Sustained,
                         KeyState::Held, KeyState::Triggering }) {
        for (auto* v : activeVoices) {
            if (v->getState().event != VoiceEvent::Kill && v->getKeyState() == ks)
                return v;
        }
    }
    return nullptr;
}

ManagedVoice* VoiceManager::getVoiceToKill(int maxVoices) {
    int excessVoices = static_cast<int>(activeVoices.size()) - maxVoices;
    ManagedVoice* released = nullptr;
    ManagedVoice* sustained = nullptr;
    ManagedVoice* held = nullptr;

    for (auto* voice : activeVoices) {
        if (voice->getState().event == VoiceEvent::Kill)
            excessVoices--;
        else if (released == nullptr && voice->getKeyState() == KeyState::Released)
            released = voice;
        else if (sustained == nullptr && voice->getKeyState() == KeyState::Sustained)
            sustained = voice;
        else if (held == nullptr)
            held = voice;
    }

    if (excessVoices <= 0)
        return nullptr;

    if (released) return released;
    if (sustained) return sustained;
    if (held) return held;
    return nullptr;
}

int VoiceManager::grabNextUnplayedPressedNote() {
    // Newest priority: search from back (most recent).
    for (auto it = pressedNotes.rbegin(); it != pressedNotes.rend(); ++it) {
        if (!isNotePlaying(getNote(*it), getChannel(*it)))
            return *it;
    }
    return pressedNotes.back();
}

bool VoiceManager::isNotePlaying(int note, int channel) const {
    for (const auto* voice : activeVoices) {
        if (voice->getState().event != VoiceEvent::Kill
            && voice->getState().midiNote == note
            && voice->getState().channel == channel)
            return true;
    }
    return false;
}

void VoiceManager::renderVoices(juce::AudioBuffer<float>& outputBuffer,
                                 int startSample, int numSamples) {
    for (auto* mv : activeVoices) {
        auto* jv = mv->getJuceVoice();
        if (jv != nullptr)
            jv->renderNextBlock(outputBuffer, startSample, numSamples);
    }

    for (auto* mv : activeVoices)
        mv->completeVoiceEvent();

    checkVoiceKiller();
}

void VoiceManager::checkVoiceKiller() {
    if (client == nullptr)
        return;

    for (int i = static_cast<int>(activeVoices.size()) - 1; i >= 0; --i) {
        auto* mv = activeVoices[i];
        bool released = mv->getState().event == VoiceEvent::Off
                     || mv->getState().event == VoiceEvent::Kill;

        if (released && client->isVoiceSilent(*mv)) {
            activeVoices.erase(activeVoices.begin() + i);
            mv->markDead();
            freeVoices.push_back(mv);
        }
    }
}
