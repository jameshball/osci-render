#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <atomic>
#include "DahdsrEnvelope.h"
#include "EnvState.h"
#include "ModulationSource.h"

// Encapsulates all DAW-automatable envelope parameters, assignments,
// and audio-thread state. Follows the same pattern as LfoParameters,
// RandomParameters, and SidechainParameters.
//
// Envelope 0 uses legacy parameter IDs (e.g. "delayTime") for backwards
// compatibility with existing DAW sessions. Envelopes 1-4 use indexed IDs
// (e.g. "env2Delay").
class EnvelopeParameters : public ModulationSource {
public:
    // Per-envelope DAHDSR parameter pointers
    struct ParamSet {
        osci::FloatParameter* delayTime = nullptr;
        osci::FloatParameter* attackTime = nullptr;
        osci::FloatParameter* holdTime = nullptr;
        osci::FloatParameter* decayTime = nullptr;
        osci::FloatParameter* sustainLevel = nullptr;
        osci::FloatParameter* releaseTime = nullptr;
        osci::FloatParameter* attackShape = nullptr;
        osci::FloatParameter* decayShape = nullptr;
        osci::FloatParameter* releaseShape = nullptr;

        void addListenerToAll(juce::AudioProcessorParameter::Listener* listener) {
            for (auto* p : getAllParams())
                if (p) p->addListener(listener);
        }

        void removeListenerFromAll(juce::AudioProcessorParameter::Listener* listener) {
            for (auto* p : getAllParams())
                if (p) p->removeListener(listener);
        }

    private:
        std::array<osci::FloatParameter*, 9> getAllParams() const {
            return { delayTime, attackTime, holdTime, decayTime, sustainLevel,
                     releaseTime, attackShape, decayShape, releaseShape };
        }
    };
    ParamSet params[NUM_ENVELOPES];

    // Non-parameter state
    int activeTab = 0;

    // Audio-thread state
    DahdsrState globalStates[NUM_ENVELOPES];
    std::atomic<float> currentValues[NUM_ENVELOPES] = {};
    std::atomic<bool> active[NUM_ENVELOPES] = {};
    std::atomic<bool> noteOnPending{false};
    std::atomic<bool> noteOffPending{false};
    std::array<std::vector<float>, NUM_ENVELOPES> blockBuffer;
    std::array<float, NUM_ENVELOPES> prevBlockValues = {};

    // Whether to create envelopes 1-4 (advanced mode)
    bool advancedMode = false;

    // Envelope 0 always uses legacy IDs. Envelopes 1-4 are only created in advanced mode.
    explicit EnvelopeParameters(bool beginnerMode) {
        advancedMode = !beginnerMode;

        // Envelope 0: legacy parameter IDs for backwards compatibility
        params[0].delayTime    = new osci::FloatParameter("Delay Time",    "delayTime",    VERSION_HINT, 0.0f,   osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
        params[0].attackTime   = new osci::FloatParameter("Attack Time",   "attackTime",   VERSION_HINT, 0.005f, osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
        params[0].holdTime     = new osci::FloatParameter("Hold Time",     "holdTime",     VERSION_HINT, 1.0f,   osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
        params[0].decayTime    = new osci::FloatParameter("Decay Time",    "decayTime",    VERSION_HINT, 0.0f,   osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
        params[0].sustainLevel = new osci::FloatParameter("Sustain Level", "sustainLevel", VERSION_HINT, 1.0f, 0.0f, 1.0f, 0.00001f);
        params[0].releaseTime  = new osci::FloatParameter("Release Time",  "releaseTime",  VERSION_HINT, 0.1f,   osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
        params[0].attackShape  = new osci::FloatParameter("Attack Shape",  "attackShape",  VERSION_HINT, 5.0f,  -50.0f, 50.0f, 0.00001f);
        params[0].decayShape   = new osci::FloatParameter("Decay Shape",   "decayShape",   VERSION_HINT, -20.0f, -50.0f, 50.0f, 0.00001f);
        params[0].releaseShape = new osci::FloatParameter("Release Shape", "releaseShape", VERSION_HINT, -5.0f,  -50.0f, 50.0f, 0.00001f);

        // Envelope 0 params are always registered
        floatParameters.push_back(params[0].delayTime);
        floatParameters.push_back(params[0].attackTime);
        floatParameters.push_back(params[0].holdTime);
        floatParameters.push_back(params[0].decayTime);
        floatParameters.push_back(params[0].sustainLevel);
        floatParameters.push_back(params[0].releaseTime);
        floatParameters.push_back(params[0].attackShape);
        floatParameters.push_back(params[0].decayShape);
        floatParameters.push_back(params[0].releaseShape);

        // Envelopes 1-4: indexed parameter IDs (advanced mode only)
        if (advancedMode) {
            for (int i = 1; i < NUM_ENVELOPES; ++i) {
                juce::String idx = juce::String(i + 1);
                params[i].delayTime    = new osci::FloatParameter("Env " + idx + " Delay",     "env" + idx + "Delay",    VERSION_HINT, 0.0f,   osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
                params[i].attackTime   = new osci::FloatParameter("Env " + idx + " Attack",    "env" + idx + "Attack",   VERSION_HINT, 0.005f, osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
                params[i].holdTime     = new osci::FloatParameter("Env " + idx + " Hold",      "env" + idx + "Hold",     VERSION_HINT, 1.0f,   osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
                params[i].decayTime    = new osci::FloatParameter("Env " + idx + " Decay",     "env" + idx + "Decay",    VERSION_HINT, 0.0f,   osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
                params[i].sustainLevel = new osci::FloatParameter("Env " + idx + " Sustain",   "env" + idx + "Sustain",  VERSION_HINT, 1.0f, 0.0f, 1.0f, 0.00001f);
                params[i].releaseTime  = new osci::FloatParameter("Env " + idx + " Release",   "env" + idx + "Release",  VERSION_HINT, 0.1f,   osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
                params[i].attackShape  = new osci::FloatParameter("Env " + idx + " Atk Shape", "env" + idx + "AtkShape", VERSION_HINT, 5.0f,  -50.0f, 50.0f, 0.00001f);
                params[i].decayShape   = new osci::FloatParameter("Env " + idx + " Dec Shape", "env" + idx + "DecShape", VERSION_HINT, -20.0f, -50.0f, 50.0f, 0.00001f);
                params[i].releaseShape = new osci::FloatParameter("Env " + idx + " Rel Shape", "env" + idx + "RelShape", VERSION_HINT, -5.0f,  -50.0f, 50.0f, 0.00001f);

                floatParameters.push_back(params[i].delayTime);
                floatParameters.push_back(params[i].attackTime);
                floatParameters.push_back(params[i].holdTime);
                floatParameters.push_back(params[i].decayTime);
                floatParameters.push_back(params[i].sustainLevel);
                floatParameters.push_back(params[i].releaseTime);
                floatParameters.push_back(params[i].attackShape);
                floatParameters.push_back(params[i].decayShape);
                floatParameters.push_back(params[i].releaseShape);
            }
        }
    }

    // === DAHDSR param access ===

    DahdsrParams getDahdsrParams(int envIndex) const {
        jassert(envIndex >= 0 && envIndex < NUM_ENVELOPES);
        const auto& ep = params[juce::jlimit(0, NUM_ENVELOPES - 1, envIndex)];
        if (ep.delayTime == nullptr) return DahdsrParams{};
        return DahdsrParams{
            .delaySeconds  = ep.delayTime->getValueUnnormalised(),
            .attackSeconds = ep.attackTime->getValueUnnormalised(),
            .holdSeconds   = ep.holdTime->getValueUnnormalised(),
            .decaySeconds  = ep.decayTime->getValueUnnormalised(),
            .sustainLevel  = ep.sustainLevel->getValueUnnormalised(),
            .releaseSeconds = ep.releaseTime->getValueUnnormalised(),
            .attackCurve   = ep.attackShape->getValueUnnormalised(),
            .decayCurve    = ep.decayShape->getValueUnnormalised(),
            .releaseCurve  = ep.releaseShape->getValueUnnormalised(),
        };
    }

    // === ModulationSource interface ===
    juce::String getTypeId() const override { return "env"; }
    juce::String getTypeLabel() const override { return "ENV"; }
    int getSourceCount() const override { return NUM_ENVELOPES; }
    const std::vector<float>* getBlockBuffers() const override { return blockBuffer.data(); }

    void prepareToPlay(double /*sampleRate*/, int samplesPerBlock) override {
        for (int e = 0; e < NUM_ENVELOPES; ++e)
            blockBuffer[e].resize(samplesPerBlock);
    }

    // === UI snapshot access ===

    float getCurrentValue(int envIndex) const override {
        if (envIndex < 0 || envIndex >= NUM_ENVELOPES) return 0.0f;
        return currentValues[envIndex].load(std::memory_order_relaxed);
    }

    bool isActive(int envIndex) const override {
        if (envIndex < 0 || envIndex >= NUM_ENVELOPES) return false;
        return active[envIndex].load(std::memory_order_relaxed);
    }

    // === Block buffer generation (called from audio thread each processBlock) ===
    // Aggregates per-voice envelope values and fills interpolated per-sample buffers.
    template<int MaxVoices>
    void fillBlockBuffers(int numSamples,
                          const std::atomic<bool> (&envActive)[NUM_ENVELOPES][MaxVoices],
                          const std::atomic<float> (&envValue)[NUM_ENVELOPES][MaxVoices]) {
        if (numSamples <= 0) return;

        for (int e = 0; e < NUM_ENVELOPES; ++e) {
            float maxVal = 0.0f;
            for (int v = 0; v < MaxVoices; ++v) {
                if (envActive[e][v].load(std::memory_order_relaxed)) {
                    float val = envValue[e][v].load(std::memory_order_relaxed);
                    if (val > maxVal) maxVal = val;
                }
            }
            currentValues[e].store(maxVal, std::memory_order_relaxed);
            active[e].store(maxVal > 0.0f, std::memory_order_relaxed);

            float prev = prevBlockValues[e];
            float curr = maxVal;
            if ((int)blockBuffer[e].size() < numSamples)
                blockBuffer[e].resize(numSamples);
            float invN = 1.0f / (float)numSamples;
            for (int s = 0; s < numSamples; ++s) {
                float t = (float)(s + 1) * invN;
                blockBuffer[e][s] = prev + (curr - prev) * t;
            }
            prevBlockValues[e] = curr;
        }
    }

    // === State serialization ===

    void saveToXml(juce::XmlElement* root) const override {
        auto envXml = root->createNewChildElement("envelopes");
        envXml->setAttribute("activeTab", activeTab);

        auto envAssignmentsXml = envXml->createNewChildElement("envAssignments");
        {
            juce::SpinLock::ScopedLockType lock(assignments.lock);
            for (const auto& a : assignments.items) {
                auto xml = envAssignmentsXml->createNewChildElement("assignment");
                a.saveToXml(xml, "env");
            }
        }
    }

    void loadFromXml(const juce::XmlElement* root) override {
        auto envelopesXml = root->getChildByName("envelopes");
        if (envelopesXml != nullptr) {
            activeTab = envelopesXml->getIntAttribute("activeTab", 0);
            auto envAssignmentsXml = envelopesXml->getChildByName("envAssignments");
            if (envAssignmentsXml != nullptr) {
                juce::SpinLock::ScopedLockType lock(assignments.lock);
                assignments.items.clear();
                for (auto* xml : envAssignmentsXml->getChildWithTagNameIterator("assignment"))
                    assignments.items.push_back(EnvAssignment::loadFromXml(xml, "env"));
            } else {
                assignments.clear();
            }
        } else {
            activeTab = 0;
            assignments.clear();
        }
    }
};
