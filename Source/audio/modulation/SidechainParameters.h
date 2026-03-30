#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <atomic>
#include "SidechainState.h"
#include "ModulationSource.h"

// Encapsulates all DAW-automatable Sidechain parameters, assignments,
// and audio-thread state. Follows the VisualiserParameters/LfoParameters pattern.
class SidechainParameters : public ModulationSource {
public:
    // DAW-automatable parameters (shared across all sidechain sources for now)
    osci::FloatParameter* attack = nullptr;
    osci::FloatParameter* release = nullptr;

    // Non-parameter state
    int activeTab = 0;

    // Audio-thread state
    SidechainAudioState audioStates[NUM_SIDECHAINS];
    std::array<std::vector<float>, NUM_SIDECHAINS> blockBuffer;

    // Thread-safe snapshots for UI
    std::atomic<float> currentValues[NUM_SIDECHAINS] = {};
    std::atomic<float> inputLevels[NUM_SIDECHAINS] = {};

    // Transfer curve (protected by curveLock)
    std::vector<GraphNode> transferCurve;
    std::vector<GraphNode> fullCurve; // cached: corner + transfer + corner
    mutable juce::SpinLock curveLock;

    SidechainParameters() {
        attack = new osci::FloatParameter("SC Attack", "sidechainAttack", VERSION_HINT, 0.3f, 0.001f, 2.0f, 0.001f, "s");
        release = new osci::FloatParameter("SC Release", "sidechainRelease", VERSION_HINT, 0.3f, 0.001f, 2.0f, 0.001f, "s");

        floatParameters.push_back(attack);
        floatParameters.push_back(release);

        transferCurve = sidechain::defaultTransferCurve();
        rebuildFullCurve();
    }

    // === ModulationSource interface ===
    juce::String getTypeId() const override { return "sc"; }
    juce::String getTypeLabel() const override { return "SC"; }
    int getSourceCount() const override { return NUM_SIDECHAINS; }
    const std::vector<float>* getBlockBuffers() const override { return blockBuffer.data(); }

    void prepareToPlay(double /*sampleRate*/, int samplesPerBlock) override {
        for (int sc = 0; sc < NUM_SIDECHAINS; ++sc)
            blockBuffer[sc].resize(samplesPerBlock);
        {
            juce::SpinLock::ScopedLockType lock(curveLock);
            cachedFullCurve = fullCurve;
        }
    }

    // === Parameter getters ===

    float getAttack(int /*scIndex*/) const {
        return attack ? attack->getValueUnnormalised() : 0.3f;
    }

    float getRelease(int /*scIndex*/) const {
        return release ? release->getValueUnnormalised() : 0.3f;
    }

    // === Parameter setters ===

    void setAttack(int /*scIndex*/, float seconds) {
        if (attack) attack->setUnnormalisedValueNotifyingHost(seconds);
    }

    void setRelease(int /*scIndex*/, float seconds) {
        if (release) release->setUnnormalisedValueNotifyingHost(seconds);
    }

    // === Transfer curve ===

    void setTransferCurve(int /*scIndex*/, const std::vector<GraphNode>& nodes) {
        juce::SpinLock::ScopedLockType lock(curveLock);
        transferCurve = nodes;
        rebuildFullCurve();
    }

    std::vector<GraphNode> getTransferCurve(int /*scIndex*/) const {
        juce::SpinLock::ScopedLockType lock(curveLock);
        return transferCurve;
    }

    // === UI snapshot access ===

    float getCurrentValue(int i) const override {
        if (i < 0 || i >= NUM_SIDECHAINS) return 0.0f;
        return currentValues[i].load(std::memory_order_relaxed);
    }

    bool isActive(int /*scIndex*/) const {
        return true; // Sidechain is always active
    }

    float getInputLevel(int i) const {
        if (i < 0 || i >= NUM_SIDECHAINS) return 0.0f;
        return inputLevels[i].load(std::memory_order_relaxed);
    }

    // === Block buffer generation (called from audio thread each processBlock) ===
    void fillBlockBuffers(int numSamples, double sampleRate, const float* rectifiedIn) {
        if (numSamples <= 0) return;

        float attackVal = attack ? attack->getValueUnnormalised() : 0.3f;
        float releaseVal = release ? release->getValueUnnormalised() : 0.3f;

        {
            juce::SpinLock::ScopedLockType lock(curveLock);
            cachedFullCurve = fullCurve; // no alloc if capacity >= size
        }

        for (int sc = 0; sc < NUM_SIDECHAINS; ++sc) {
            if ((int)blockBuffer[sc].size() < numSamples)
                blockBuffer[sc].resize(numSamples);

            audioStates[sc].advanceBlock(
                blockBuffer[sc].data(), rectifiedIn, numSamples,
                attackVal, releaseVal, (float)sampleRate,
                cachedFullCurve);

            currentValues[sc].store(blockBuffer[sc][numSamples - 1], std::memory_order_relaxed);
            inputLevels[sc].store(sidechain::linearToDb(audioStates[sc].smoothedLevel), std::memory_order_relaxed);
        }
    }

    // === State serialization ===

    void saveToXml(juce::XmlElement* root) const override {
        auto sidechainXml = root->createNewChildElement("sidechain");
        sidechainXml->setAttribute("activeTab", activeTab);

        {
            juce::SpinLock::ScopedLockType lock(curveLock);
            auto curveXml = sidechainXml->createNewChildElement("transferCurve");
            for (const auto& node : transferCurve) {
                auto nodeXml = curveXml->createNewChildElement("node");
                nodeXml->setAttribute("time", node.time);
                nodeXml->setAttribute("value", node.value);
                nodeXml->setAttribute("curve", (double)node.curve);
            }
        }

        auto assignmentsXml = sidechainXml->createNewChildElement("sidechainAssignments");
        {
            juce::SpinLock::ScopedLockType lock(assignments.lock);
            for (const auto& a : assignments.items) {
                auto xml = assignmentsXml->createNewChildElement("assignment");
                a.saveToXml(xml, "sc");
            }
        }
    }

    void loadFromXml(const juce::XmlElement* root) override {
        auto sidechainXml = root->getChildByName("sidechain");
        if (sidechainXml != nullptr) {
            activeTab = sidechainXml->getIntAttribute("activeTab", 0);

            auto curveXml = sidechainXml->getChildByName("transferCurve");
            if (curveXml != nullptr) {
                juce::SpinLock::ScopedLockType lock(curveLock);
                transferCurve.clear();
                for (auto* nodeXml : curveXml->getChildWithTagNameIterator("node")) {
                    GraphNode n;
                    n.time = nodeXml->getDoubleAttribute("time", 0.0);
                    n.value = nodeXml->getDoubleAttribute("value", 0.0);
                    n.curve = (float)nodeXml->getDoubleAttribute("curve", 0.0);
                    transferCurve.push_back(n);
                }
                if (transferCurve.size() < 2) {
                    transferCurve = sidechain::defaultTransferCurve();
                } else if (transferCurve.front().time >= 0.0 && transferCurve.back().time <= 1.0) {
                    // Migrate old linear [0,1] curve nodes to dB space
                    for (auto& node : transferCurve) {
                        float linear = (float)juce::jlimit(0.0, 1.0, node.time);
                        node.time = (double)sidechain::linearToDb(linear);
                    }
                }
                rebuildFullCurve();
            }

            auto scAssignmentsXml = sidechainXml->getChildByName("sidechainAssignments");
            if (scAssignmentsXml != nullptr) {
                juce::SpinLock::ScopedLockType lock(assignments.lock);
                assignments.items.clear();
                for (auto* xml : scAssignmentsXml->getChildWithTagNameIterator("assignment"))
                    assignments.items.push_back(SidechainAssignment::loadFromXml(xml, "sc"));
            } else {
                assignments.clear();
            }
        } else {
            activeTab = 0;
            {
                juce::SpinLock::ScopedLockType lock(curveLock);
                transferCurve = sidechain::defaultTransferCurve();
                rebuildFullCurve();
            }
            assignments.clear();
        }
    }

private:
    // Pre-allocated copy of fullCurve for the audio thread (avoids per-block allocation)
    std::vector<GraphNode> cachedFullCurve;

    // Rebuild fullCurve from transferCurve. Call while holding curveLock.
    void rebuildFullCurve() {
        fullCurve.clear();
        fullCurve.reserve(transferCurve.size() + 2);
        fullCurve.push_back({ (double)sidechain::DB_FLOOR, 0.0, 0.0f });
        for (auto& n : transferCurve)
            fullCurve.push_back(n);
        fullCurve.push_back({ (double)sidechain::DB_CEILING, 1.0, 0.0f });
    }
};
