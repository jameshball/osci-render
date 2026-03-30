#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <atomic>
#include "RandomState.h"
#include "ModulationParameterTypes.h"
#include "ModulationSource.h"

// Encapsulates all DAW-automatable Random modulator parameters, assignments,
// and audio-thread state. Follows the VisualiserParameters/LfoParameters pattern.
class RandomParameters : public ModulationSource {
public:
    // DAW-automatable parameters (one per Random source)
    osci::FloatParameter* rate[NUM_RANDOM_SOURCES] = {};
    RandomStyleParameter* style[NUM_RANDOM_SOURCES] = {};
    LfoRateModeParameter* rateMode[NUM_RANDOM_SOURCES] = {};
    TempoDivisionParameter* tempoDivision[NUM_RANDOM_SOURCES] = {};

    // Non-parameter state
    int activeTab = 0;

    // Audio-thread state
    RandomAudioState audioStates[NUM_RANDOM_SOURCES];
    int activeNoteCount = 0;
    bool prevAnyVoiceActive = false;
    std::array<std::vector<float>, NUM_RANDOM_SOURCES> blockBuffer;

    // Thread-safe snapshots for UI
    std::atomic<float> currentValues[NUM_RANDOM_SOURCES] = {};
    std::atomic<bool> active[NUM_RANDOM_SOURCES] = {};
    std::atomic<bool> retriggered[NUM_RANDOM_SOURCES] = {};

    // Ring buffers for streaming random values to UI
    RandomUIRingBuffer uiBuffers[NUM_RANDOM_SOURCES];
    static constexpr int kUISubsampleInterval = 64;

    RandomParameters() {
        for (int i = 0; i < NUM_RANDOM_SOURCES; ++i) {
            juce::String idx = juce::String(i + 1);
            juce::String pfx = "random" + idx;

            rate[i] = new osci::FloatParameter("Random " + idx + " Rate", pfx + "Rate", VERSION_HINT, 1.0f, 0.01f, 1000.0f, 0.01f, "Hz");
            style[i] = new RandomStyleParameter("Random " + idx + " Style", pfx + "Style", VERSION_HINT, RandomStyle::Perlin);
            rateMode[i] = new LfoRateModeParameter("Random " + idx + " Rate Mode", pfx + "RateMode", VERSION_HINT, LfoRateMode::Seconds);
            tempoDivision[i] = new TempoDivisionParameter("Random " + idx + " Tempo Div", pfx + "TempoDivision", VERSION_HINT, 8);

            floatParameters.push_back(rate[i]);
            intParameters.push_back(style[i]);
            intParameters.push_back(rateMode[i]);
            intParameters.push_back(tempoDivision[i]);

            audioStates[i].seed(0x12345678u + (uint32_t)i * 0x9E3779B9u);
        }
    }

    // === ModulationSource interface ===
    juce::String getTypeId() const override { return "rng"; }
    juce::String getTypeLabel() const override { return "RNG"; }
    int getSourceCount() const override { return NUM_RANDOM_SOURCES; }
    const std::vector<float>* getBlockBuffers() const override { return blockBuffer.data(); }

    void prepareToPlay(double /*sampleRate*/, int samplesPerBlock) override {
        for (int r = 0; r < NUM_RANDOM_SOURCES; ++r)
            blockBuffer[r].resize(samplesPerBlock);
    }

    // === Parameter getters ===

    RandomStyle getStyle(int i) const {
        if (i < 0 || i >= NUM_RANDOM_SOURCES || !style[i]) return RandomStyle::Perlin;
        return (RandomStyle)style[i]->getValueUnnormalised();
    }

    LfoRateMode getRateMode(int i) const {
        if (i < 0 || i >= NUM_RANDOM_SOURCES || !rateMode[i]) return LfoRateMode::Seconds;
        return (LfoRateMode)rateMode[i]->getValueUnnormalised();
    }

    int getTempoDivision(int i) const {
        if (i < 0 || i >= NUM_RANDOM_SOURCES || !tempoDivision[i]) return 8;
        return tempoDivision[i]->getValueUnnormalised();
    }

    // === Parameter setters ===

    void setStyle(int i, RandomStyle s) {
        if (i < 0 || i >= NUM_RANDOM_SOURCES || !style[i]) return;
        style[i]->setUnnormalisedValueNotifyingHost((int)s);
    }

    void setRateMode(int i, LfoRateMode m) {
        if (i < 0 || i >= NUM_RANDOM_SOURCES || !rateMode[i]) return;
        rateMode[i]->setUnnormalisedValueNotifyingHost((int)m);
    }

    void setTempoDivision(int i, int divisionIndex) {
        if (i < 0 || i >= NUM_RANDOM_SOURCES || !tempoDivision[i]) return;
        tempoDivision[i]->setUnnormalisedValueNotifyingHost(divisionIndex);
    }

    // === UI snapshot access ===

    float getCurrentValue(int i) const override {
        if (i < 0 || i >= NUM_RANDOM_SOURCES) return 0.0f;
        return currentValues[i].load(std::memory_order_relaxed);
    }

    bool isActive(int i) const {
        if (i < 0 || i >= NUM_RANDOM_SOURCES) return false;
        return active[i].load(std::memory_order_relaxed);
    }

    bool consumeRetriggered(int i) {
        if (i < 0 || i >= NUM_RANDOM_SOURCES) return false;
        return retriggered[i].exchange(false, std::memory_order_relaxed);
    }

    int drainUIBuffer(int i, RandomUIRingBuffer::Entry* out, int maxEntries) {
        if (i < 0 || i >= NUM_RANDOM_SOURCES) return 0;
        return uiBuffers[i].drain(out, maxEntries);
    }

    // === Block buffer generation (called from audio thread each processBlock) ===
    // Processes MIDI note events for Random triggering and computes per-sample values.
    template<int MaxVoices>
    void fillBlockBuffers(int numSamples, double sampleRate, const juce::MidiBuffer& midi,
                          double bpm, const std::atomic<bool> (&voiceActive)[MaxVoices]) {
        if (numSamples <= 0) return;

        // Process MIDI note events (Random sources are always note-dependent)
        bool hadNoteOnThisBlock = false;
        for (const auto metadata : midi) {
            auto msg = metadata.getMessage();
            if (msg.isNoteOn()) {
                hadNoteOnThisBlock = true;
                activeNoteCount++;
                for (int r = 0; r < NUM_RANDOM_SOURCES; ++r) {
                    if (!audioStates[r].finished)
                        retriggered[r].store(true, std::memory_order_relaxed);
                    audioStates[r].noteOn();
                }
            } else if (msg.isNoteOff()) {
                activeNoteCount = std::max(0, activeNoteCount - 1);
                if (activeNoteCount == 0) {
                    for (int r = 0; r < NUM_RANDOM_SOURCES; ++r)
                        audioStates[r].noteOff();
                }
            } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
                activeNoteCount = 0;
                for (int r = 0; r < NUM_RANDOM_SOURCES; ++r)
                    audioStates[r].noteOff();
            }
        }

        // Compute Random samples for this block
        {
            auto& divisions = getTempoDivisions();

            bool anyVoiceActive = false;
            for (int v = 0; v < MaxVoices; ++v) {
                if (voiceActive[v].load(std::memory_order_relaxed)) {
                    anyVoiceActive = true;
                    break;
                }
            }
            bool effectiveVoiceActive = anyVoiceActive || hadNoteOnThisBlock;

            for (int r = 0; r < NUM_RANDOM_SOURCES; ++r) {
                if ((int)blockBuffer[r].size() < numSamples)
                    blockBuffer[r].resize(numSamples);

                float rt;
                LfoRateMode rateMd = rateMode[r] ? (LfoRateMode)rateMode[r]->getValueUnnormalised() : LfoRateMode::Seconds;
                if (rateMd == LfoRateMode::Seconds) {
                    rt = rate[r] ? rate[r]->getValueUnnormalised() : 1.0f;
                } else {
                    int tdIdx = tempoDivision[r] ? tempoDivision[r]->getValueUnnormalised() : 8;
                    int idx = juce::jlimit(0, (int)divisions.size() - 1, tdIdx);
                    rt = (float)divisions[idx].toHz(bpm, rateMd);
                }

                audioStates[r].style = style[r] ? (RandomStyle)style[r]->getValueUnnormalised() : RandomStyle::Perlin;
                bool isActive = !audioStates[r].finished;

                audioStates[r].advanceBlock(blockBuffer[r].data(), numSamples,
                                            rt, (float)sampleRate);

                if (!effectiveVoiceActive && prevAnyVoiceActive)
                    audioStates[r].voicesFinished();

                // Subsample the block into the UI ring buffer
                for (int s = kUISubsampleInterval - 1; s < numSamples; s += kUISubsampleInterval)
                    uiBuffers[r].push(blockBuffer[r][s], isActive);
                if (numSamples > 0) {
                    int lastPushed = ((numSamples - 1) / kUISubsampleInterval) * kUISubsampleInterval + kUISubsampleInterval - 1;
                    if (lastPushed != numSamples - 1)
                        uiBuffers[r].push(blockBuffer[r][numSamples - 1], isActive);
                }

                currentValues[r].store(blockBuffer[r][numSamples - 1], std::memory_order_relaxed);
                active[r].store(isActive, std::memory_order_relaxed);
            }

            prevAnyVoiceActive = effectiveVoiceActive;
        }
    }

    // === State serialization ===

    void saveToXml(juce::XmlElement* root) const override {
        auto randomsXml = root->createNewChildElement("randoms");
        randomsXml->setAttribute("activeTab", activeTab);

        auto assignmentsXml = randomsXml->createNewChildElement("randomAssignments");
        {
            juce::SpinLock::ScopedLockType lock(assignments.lock);
            for (const auto& a : assignments.items) {
                auto xml = assignmentsXml->createNewChildElement("assignment");
                a.saveToXml(xml, "rng");
            }
        }
    }

    void loadFromXml(const juce::XmlElement* root) override {
        auto randomsXml = root->getChildByName("randoms");
        if (randomsXml != nullptr) {
            activeTab = randomsXml->getIntAttribute("activeTab", 0);

            auto assignmentsXml = randomsXml->getChildByName("randomAssignments");
            if (assignmentsXml != nullptr) {
                juce::SpinLock::ScopedLockType lock(assignments.lock);
                assignments.items.clear();
                for (auto* xml : assignmentsXml->getChildWithTagNameIterator("assignment"))
                    assignments.items.push_back(RandomAssignment::loadFromXml(xml, "rng"));
            } else {
                assignments.clear();
            }
        } else {
            activeTab = 0;
            assignments.clear();
        }
    }
};
