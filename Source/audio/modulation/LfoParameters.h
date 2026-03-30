#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <atomic>
#include "LfoState.h"
#include "ModulationParameterTypes.h"
#include "ModulationSource.h"

// Encapsulates all DAW-automatable LFO parameters, waveform data, assignments,
// and audio-thread state. Follows the VisualiserParameters pattern:
// owns parameters and exposes vectors for the processor to adopt at construction.
class LfoParameters : public ModulationSource {
public:
    // DAW-automatable parameters (one per LFO)
    osci::FloatParameter* rate[NUM_LFOS] = {};
    LfoPresetParameter* preset[NUM_LFOS] = {};
    LfoRateModeParameter* rateMode[NUM_LFOS] = {};
    LfoModeParameter* mode[NUM_LFOS] = {};
    osci::FloatParameter* phaseOffset[NUM_LFOS] = {};
    osci::FloatParameter* smoothAmount[NUM_LFOS] = {};
    osci::FloatParameter* delayAmount[NUM_LFOS] = {};
    TempoDivisionParameter* tempoDivision[NUM_LFOS] = {};

    // Non-parameter state
    int activeTab = 0;

    // Waveform data (protected by waveformLock)
    LfoWaveform waveforms[NUM_LFOS];
    mutable juce::SpinLock waveformLock;

    // Audio-thread state
    LfoAudioState audioStates[NUM_LFOS];
    float smoothedOutput[NUM_LFOS] = {};
    float delayElapsed[NUM_LFOS] = {};
    int activeNoteCount = 0;
    bool prevAnyVoiceActive = false;
    std::array<std::vector<float>, NUM_LFOS> blockBuffer;

    // Thread-safe snapshots for UI
    std::atomic<float> currentValues[NUM_LFOS] = {};
    std::atomic<float> currentPhases[NUM_LFOS] = {};
    std::atomic<bool> active[NUM_LFOS] = {};
    std::atomic<bool> retriggered[NUM_LFOS] = {};

    LfoParameters() {
        for (int i = 0; i < NUM_LFOS; ++i) {
            juce::String idx = juce::String(i + 1);
            juce::String pfx = "lfo" + idx;

            rate[i] = new osci::FloatParameter("LFO " + idx + " Rate", pfx + "Rate", VERSION_HINT, 1.0f, 0.01f, 100.0f, 0.01f, "Hz");
            phaseOffset[i] = new osci::FloatParameter("LFO " + idx + " Phase Offset", pfx + "PhaseOffset", VERSION_HINT, 0.0f, 0.0f, 1.0f, 0.0001f);
            smoothAmount[i] = new osci::FloatParameter("LFO " + idx + " Smooth", pfx + "SmoothAmount", VERSION_HINT, 0.005f, 0.0f, 16.0f, 0.0001f, "s");
            delayAmount[i] = new osci::FloatParameter("LFO " + idx + " Delay", pfx + "DelayAmount", VERSION_HINT, 0.0f, 0.0f, 4.0f, 0.0001f, "s");
            mode[i] = new LfoModeParameter("LFO " + idx + " Mode", pfx + "Mode", VERSION_HINT, LfoMode::Free);
            preset[i] = new LfoPresetParameter("LFO " + idx + " Preset", pfx + "Preset", VERSION_HINT, LfoPreset::Triangle);
            rateMode[i] = new LfoRateModeParameter("LFO " + idx + " Rate Mode", pfx + "RateMode", VERSION_HINT, LfoRateMode::Seconds);
            tempoDivision[i] = new TempoDivisionParameter("LFO " + idx + " Tempo Div", pfx + "TempoDivision", VERSION_HINT, 8);

            floatParameters.push_back(rate[i]);
            floatParameters.push_back(phaseOffset[i]);
            floatParameters.push_back(smoothAmount[i]);
            floatParameters.push_back(delayAmount[i]);
            intParameters.push_back(mode[i]);
            intParameters.push_back(preset[i]);
            intParameters.push_back(rateMode[i]);
            intParameters.push_back(tempoDivision[i]);

            waveforms[i] = createLfoPreset(LfoPreset::Triangle);
        }
    }

    // === ModulationSource interface ===
    juce::String getTypeId() const override { return "lfo"; }
    juce::String getTypeLabel() const override { return "LFO"; }
    int getSourceCount() const override { return NUM_LFOS; }
    const std::vector<float>* getBlockBuffers() const override { return blockBuffer.data(); }

    void prepareToPlay(double /*sampleRate*/, int samplesPerBlock) override {
        for (int l = 0; l < NUM_LFOS; ++l)
            blockBuffer[l].resize(samplesPerBlock);
    }

    // === Waveform access ===

    void waveformChanged(int index, const LfoWaveform& waveform) {
        if (index < 0 || index >= NUM_LFOS) return;
        juce::SpinLock::ScopedLockType lock(waveformLock);
        waveforms[index] = waveform;
    }

    LfoWaveform getWaveform(int index) const {
        if (index < 0 || index >= NUM_LFOS) return {};
        juce::SpinLock::ScopedLockType lock(waveformLock);
        return waveforms[index];
    }

    // === Parameter getters (read from DAW parameters atomically) ===

    LfoPreset getPreset(int i) const {
        if (i < 0 || i >= NUM_LFOS || !preset[i]) return LfoPreset::Custom;
        return (LfoPreset)preset[i]->getValueUnnormalised();
    }

    LfoMode getMode(int i) const {
        if (i < 0 || i >= NUM_LFOS || !mode[i]) return LfoMode::Free;
        return (LfoMode)mode[i]->getValueUnnormalised();
    }

    LfoRateMode getRateMode(int i) const {
        if (i < 0 || i >= NUM_LFOS || !rateMode[i]) return LfoRateMode::Seconds;
        return (LfoRateMode)rateMode[i]->getValueUnnormalised();
    }

    int getTempoDivision(int i) const {
        if (i < 0 || i >= NUM_LFOS || !tempoDivision[i]) return 8;
        return tempoDivision[i]->getValueUnnormalised();
    }

    float getPhaseOffset(int i) const {
        if (i < 0 || i >= NUM_LFOS || !phaseOffset[i]) return 0.0f;
        return phaseOffset[i]->getValueUnnormalised();
    }

    float getSmoothAmount(int i) const {
        if (i < 0 || i >= NUM_LFOS || !smoothAmount[i]) return 0.005f;
        return smoothAmount[i]->getValueUnnormalised();
    }

    float getDelayAmount(int i) const {
        if (i < 0 || i >= NUM_LFOS || !delayAmount[i]) return 0.0f;
        return delayAmount[i]->getValueUnnormalised();
    }

    // === Parameter setters (notify DAW host) ===

    void setPreset(int i, LfoPreset p) {
        if (i < 0 || i >= NUM_LFOS || !preset[i]) return;
        preset[i]->setUnnormalisedValueNotifyingHost((int)p);
    }

    void setMode(int i, LfoMode m) {
        if (i < 0 || i >= NUM_LFOS || !mode[i]) return;
        mode[i]->setUnnormalisedValueNotifyingHost((int)m);
    }

    void setRateMode(int i, LfoRateMode m) {
        if (i < 0 || i >= NUM_LFOS || !rateMode[i]) return;
        rateMode[i]->setUnnormalisedValueNotifyingHost((int)m);
    }

    void setTempoDivision(int i, int divisionIndex) {
        if (i < 0 || i >= NUM_LFOS || !tempoDivision[i]) return;
        tempoDivision[i]->setUnnormalisedValueNotifyingHost(divisionIndex);
    }

    void setPhaseOffset(int i, float phase) {
        if (i < 0 || i >= NUM_LFOS || !phaseOffset[i]) return;
        phaseOffset[i]->setUnnormalisedValueNotifyingHost(juce::jlimit(0.0f, 1.0f, phase));
    }

    void setSmoothAmount(int i, float seconds) {
        if (i < 0 || i >= NUM_LFOS || !smoothAmount[i]) return;
        smoothAmount[i]->setUnnormalisedValueNotifyingHost(juce::jlimit(0.0f, 16.0f, seconds));
    }

    void setDelayAmount(int i, float seconds) {
        if (i < 0 || i >= NUM_LFOS || !delayAmount[i]) return;
        delayAmount[i]->setUnnormalisedValueNotifyingHost(juce::jlimit(0.0f, 4.0f, seconds));
    }

    // === UI snapshot access ===

    float getCurrentValue(int i) const override {
        if (i < 0 || i >= NUM_LFOS) return 0.0f;
        return currentValues[i].load(std::memory_order_relaxed);
    }

    float getCurrentPhase(int i) const {
        if (i < 0 || i >= NUM_LFOS) return 0.0f;
        return currentPhases[i].load(std::memory_order_relaxed);
    }

    bool isActive(int i) const {
        if (i < 0 || i >= NUM_LFOS) return false;
        return active[i].load(std::memory_order_relaxed);
    }

    bool consumeRetriggered(int i) {
        if (i < 0 || i >= NUM_LFOS) return false;
        return retriggered[i].exchange(false, std::memory_order_relaxed);
    }

    // === Audio-thread reset ===

    void resetAudioState() {
        activeNoteCount = 0;
        prevAnyVoiceActive = false;
        for (int i = 0; i < NUM_LFOS; ++i)
            audioStates[i].reset();
    }

    // === Block buffer generation (called from audio thread each processBlock) ===
    // Processes MIDI note events for LFO triggering and computes per-sample LFO values.
    template<int MaxVoices>
    void fillBlockBuffers(int numSamples, double sampleRate, const juce::MidiBuffer& midi,
                          double bpm, const std::atomic<bool> (&voiceActive)[MaxVoices]) {
        if (numSamples <= 0) return;

        // Process MIDI note events to drive LFO triggering for non-Free modes
        bool hadNoteOnThisBlock = false;
        for (const auto metadata : midi) {
            auto msg = metadata.getMessage();
            if (msg.isNoteOn()) {
                hadNoteOnThisBlock = true;
                activeNoteCount++;
                for (int l = 0; l < NUM_LFOS; ++l) {
                    if (!mode[l]) continue;
                    LfoMode m = (LfoMode)mode[l]->getValueUnnormalised();
                    if (m != LfoMode::Free && m != LfoMode::Sync) {
                        if (!audioStates[l].finished)
                            retriggered[l].store(true, std::memory_order_relaxed);
                        float phaseOff = phaseOffset[l] ? phaseOffset[l]->getValueUnnormalised() : 0.0f;
                        audioStates[l].noteOn(m, phaseOff);
                        delayElapsed[l] = 0.0f;
                    }
                }
            } else if (msg.isNoteOff()) {
                activeNoteCount = std::max(0, activeNoteCount - 1);
                if (activeNoteCount == 0) {
                    for (int l = 0; l < NUM_LFOS; ++l) {
                        if (!mode[l]) continue;
                        LfoMode m = (LfoMode)mode[l]->getValueUnnormalised();
                        if (m != LfoMode::Free && m != LfoMode::Sync)
                            audioStates[l].noteOff(m);
                    }
                }
            } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
                activeNoteCount = 0;
                for (int l = 0; l < NUM_LFOS; ++l) {
                    if (!mode[l]) continue;
                    LfoMode m = (LfoMode)mode[l]->getValueUnnormalised();
                    if (m != LfoMode::Free && m != LfoMode::Sync)
                        audioStates[l].noteOff(m);
                }
            }
        }

        // Compute LFO samples for this block
        {
            juce::SpinLock::ScopedLockType wfLock(waveformLock);
            auto& divisions = getTempoDivisions();

            bool anyVoiceActive = false;
            for (int v = 0; v < MaxVoices; ++v) {
                if (voiceActive[v].load(std::memory_order_relaxed)) {
                    anyVoiceActive = true;
                    break;
                }
            }
            bool effectiveVoiceActive = anyVoiceActive || hadNoteOnThisBlock;

            for (int l = 0; l < NUM_LFOS; ++l) {
                if (!rate[l] || !rateMode[l]) continue;
                if ((int)blockBuffer[l].size() < numSamples)
                    blockBuffer[l].resize(numSamples);
                float r;
                LfoRateMode rateMd = (LfoRateMode)rateMode[l]->getValueUnnormalised();
                if (rateMd == LfoRateMode::Seconds) {
                    r = rate[l]->getValueUnnormalised();
                } else {
                    int tdIdx = tempoDivision[l] ? tempoDivision[l]->getValueUnnormalised() : 8;
                    int idx = juce::jlimit(0, (int)divisions.size() - 1, tdIdx);
                    r = (float)divisions[idx].toHz(bpm, rateMd);
                }
                float sr = (float)sampleRate;
                LfoMode md = mode[l] ? (LfoMode)mode[l]->getValueUnnormalised() : LfoMode::Free;
                float phaseOff = phaseOffset[l] ? phaseOffset[l]->getValueUnnormalised() : 0.0f;

                bool isActive;
                if (md == LfoMode::Free) {
                    isActive = true;
                } else if (md == LfoMode::Sync) {
                    isActive = effectiveVoiceActive;
                } else {
                    isActive = !audioStates[l].finished;
                }

                int delaySkipSamples = 0;
                float delaySecs = delayAmount[l] ? delayAmount[l]->getValueUnnormalised() : 0.0f;
                if (delaySecs > 1e-6f) {
                    float elapsed = delayElapsed[l];
                    if (elapsed < delaySecs) {
                        float remainingSec = delaySecs - elapsed;
                        delaySkipSamples = juce::jmin(numSamples, (int)std::ceil(remainingSec * sr));
                        float heldValue = waveforms[l].evaluate(audioStates[l].phase);
                        for (int s = 0; s < delaySkipSamples; ++s)
                            blockBuffer[l][s] = heldValue;
                    }
                    float blockTimeSec = (float)numSamples / sr;
                    delayElapsed[l] = elapsed + blockTimeSec;
                }

                int advanceSamples = numSamples - delaySkipSamples;
                if (advanceSamples > 0) {
                    audioStates[l].advanceBlock(blockBuffer[l].data() + delaySkipSamples, advanceSamples,
                                                r, sr, waveforms[l], md, phaseOff);
                }
                if (md == LfoMode::Sync && !effectiveVoiceActive) {
                    for (int s = 0; s < numSamples; ++s)
                        blockBuffer[l][s] = 0.0f;
                }

                float smoothSecs = smoothAmount[l] ? smoothAmount[l]->getValueUnnormalised() : 0.005f;
                if (smoothSecs > 1e-6f) {
                    float alpha = 1.0f - std::exp(-1.0f / (smoothSecs * sr));
                    float prev = smoothedOutput[l];
                    for (int s = 0; s < numSamples; ++s) {
                        prev += alpha * (blockBuffer[l][s] - prev);
                        blockBuffer[l][s] = prev;
                    }
                    smoothedOutput[l] = prev;
                } else {
                    smoothedOutput[l] = blockBuffer[l][numSamples - 1];
                }

                if (!effectiveVoiceActive && prevAnyVoiceActive)
                    audioStates[l].voicesFinished(md);

                currentValues[l].store(blockBuffer[l][numSamples - 1], std::memory_order_relaxed);
                currentPhases[l].store(audioStates[l].phase, std::memory_order_relaxed);
                active[l].store(isActive, std::memory_order_relaxed);
            }

            prevAnyVoiceActive = effectiveVoiceActive;
        }
    }

    // === State serialization (advanced mode only) ===

    void saveToXml(juce::XmlElement* root) const override {
        auto lfosXml = root->createNewChildElement("lfos");
        lfosXml->setAttribute("activeTab", activeTab);
        {
            juce::SpinLock::ScopedLockType wfLock(waveformLock);
            for (int i = 0; i < NUM_LFOS; ++i) {
                auto lfoXml = lfosXml->createNewChildElement("lfo");
                lfoXml->setAttribute("index", i);
                waveforms[i].saveToXml(lfoXml);
            }
        }

        auto assignmentsXml = root->createNewChildElement("lfoAssignments");
        {
            juce::SpinLock::ScopedLockType lock(assignments.lock);
            for (const auto& a : assignments.items) {
                auto xml = assignmentsXml->createNewChildElement("assignment");
                a.saveToXml(xml, "lfo");
            }
        }
    }

    void loadFromXml(const juce::XmlElement* root) override {
        auto lfosXml = root->getChildByName("lfos");
        if (lfosXml != nullptr) {
            activeTab = lfosXml->getIntAttribute("activeTab", 0);
            juce::SpinLock::ScopedLockType wfLock(waveformLock);
            for (auto* lfoXml : lfosXml->getChildWithTagNameIterator("lfo")) {
                int idx = lfoXml->getIntAttribute("index", -1);
                if (idx >= 0 && idx < NUM_LFOS)
                    waveforms[idx].loadFromXml(lfoXml);
            }
        }

        resetAudioState();

        auto assignmentsXml = root->getChildByName("lfoAssignments");
        if (assignmentsXml != nullptr) {
            juce::SpinLock::ScopedLockType lock(assignments.lock);
            assignments.items.clear();
            for (auto* xml : assignmentsXml->getChildWithTagNameIterator("assignment"))
                assignments.items.push_back(LfoAssignment::loadFromXml(xml, "lfo"));
        } else {
            assignments.clear();
        }
    }

    // === LFO auto-assignment ===

    // Automatically assign LFOs to modulate the parameters of an effect,
    // based on each parameter's lfoTypeDefault and lfoRateDefault.
    void autoAssignForEffect(osci::Effect& effect) {
        std::vector<LfoAssignment> currentAssignments = getAssignments();

        int assignmentCount[NUM_LFOS] = {};
        for (const auto& a : currentAssignments)
            if (a.sourceIndex >= 0 && a.sourceIndex < NUM_LFOS)
                assignmentCount[a.sourceIndex]++;

        auto ratesSimilar = [](float a, float b) -> bool {
            if (a <= 0.0f || b <= 0.0f) return false;
            float ratio = (a > b) ? a / b : b / a;
            return ratio < 1.25f;
        };

        auto findBestLfo = [&](LfoPreset desiredPreset, float desiredRate) -> int {
            int bestLfo = -1;
            int bestScore = 0;
            for (int i = 0; i < NUM_LFOS; ++i) {
                int score = 0;
                bool idle = assignmentCount[i] == 0;
                LfoPreset currentPreset = getPreset(i);
                if (!idle && currentPreset == desiredPreset) {
                    float currentRate = (rate[i] != nullptr) ? rate[i]->getValueUnnormalised() : 1.0f;
                    if (ratesSimilar(currentRate, desiredRate))
                        score = 4;
                } else if (idle && currentPreset == desiredPreset && currentPreset != LfoPreset::Custom) {
                    score = 3;
                } else if (idle && currentPreset != LfoPreset::Custom) {
                    score = 2;
                } else if (idle) {
                    score = 1;
                }
                if (score > bestScore) {
                    bestScore = score;
                    bestLfo = i;
                    if (score == 4) break;
                }
            }
            return bestLfo;
        };

        auto configureLfo = [&](int lfoIdx, LfoPreset desiredPreset, float desiredRate, int score) {
            if (score <= 2) {
                juce::SpinLock::ScopedLockType lock(waveformLock);
                setPreset(lfoIdx, desiredPreset);
                waveforms[lfoIdx] = createLfoPreset(desiredPreset);
            }
            if (score <= 3 && rate[lfoIdx] != nullptr) {
                rate[lfoIdx]->setUnnormalisedValueNotifyingHost(desiredRate);
            }
        };

        for (auto* param : effect.parameters) {
            if (param->lfoTypeDefault == osci::LfoType::Static)
                continue;

            LfoPreset desiredPreset = lfoTypeToLfoPreset(param->lfoTypeDefault);
            float desiredRate = param->lfoRateDefault_;

            int chosenLfo = findBestLfo(desiredPreset, desiredRate);
            if (chosenLfo < 0)
                continue;

            int score = 0;
            {
                bool idle = assignmentCount[chosenLfo] == 0;
                LfoPreset currentPreset = getPreset(chosenLfo);
                if (!idle) score = 4;
                else if (currentPreset == desiredPreset && currentPreset != LfoPreset::Custom) score = 3;
                else if (currentPreset != LfoPreset::Custom) score = 2;
                else score = 1;
            }
            configureLfo(chosenLfo, desiredPreset, desiredRate, score);

            param->setUnnormalisedValueNotifyingHost(param->min.load());

            LfoAssignment assignment;
            assignment.sourceIndex = chosenLfo;
            assignment.paramId = param->paramID;
            assignment.depth = 1.0f;
            assignment.bipolar = false;
            addAssignment(assignment);

            assignmentCount[chosenLfo]++;
        }
    }

    // === LFO preview assignment ===
    // Temporarily auto-assigns LFOs while hovering an effect in the grid.

    // Preview state (message-thread only — no lock needed)
    juce::String previewEffectId;
    std::vector<std::pair<juce::String, float>> previewSavedParamValues;

    // Start previewing: save current param values, auto-assign LFOs to the effect.
    void startPreview(const juce::String& effectId,
                      const std::vector<std::shared_ptr<osci::Effect>>& effects,
                      std::function<void(const osci::Effect&)> removeAllAssignments) {
        jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
        stopPreview(effects, removeAllAssignments);
        for (auto& eff : effects) {
            if (eff->getId() == effectId) {
                previewSavedParamValues.clear();
                for (auto* param : eff->parameters) {
                    if (param->lfoTypeDefault != osci::LfoType::Static)
                        previewSavedParamValues.emplace_back(param->paramID, param->getValueUnnormalised());
                }
                autoAssignForEffect(*eff);
                previewEffectId = effectId;
                break;
            }
        }
    }

    // Stop previewing: remove assignments and restore saved parameter values.
    void stopPreview(const std::vector<std::shared_ptr<osci::Effect>>& effects,
                     std::function<void(const osci::Effect&)> removeAllAssignments) {
        jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
        if (previewEffectId.isEmpty()) return;
        for (auto& eff : effects) {
            if (eff->getId() == previewEffectId) {
                removeAllAssignments(*eff);
                for (const auto& [paramId, savedValue] : previewSavedParamValues) {
                    for (auto* param : eff->parameters) {
                        if (param->paramID == paramId) {
                            param->setUnnormalisedValueNotifyingHost(savedValue);
                            break;
                        }
                    }
                }
                break;
            }
        }
        previewSavedParamValues.clear();
        previewEffectId = juce::String();
    }

    // Promote preview: keep assignments and param values, just clear preview tracking.
    void promotePreview() {
        jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
        previewSavedParamValues.clear();
        previewEffectId = juce::String();
    }
};
