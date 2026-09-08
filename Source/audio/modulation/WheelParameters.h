#pragma once
#include "ModulationSource.h"
#include <array>

class WheelParameters : public ModulationSource {
public:
    std::array<osci::FloatParameter*, 16> pitch{};
    osci::FloatParameter* modulation;
    std::vector<float> buffer;
    std::vector<float> pitchMultipliers;
    std::array<int, 16> lastPitch{};
    std::atomic<float> pitchDisplay{0.0f};

    WheelParameters() {
        for (int ch = 0; ch < 16; ++ch) {
            pitch[ch] = new osci::FloatParameter("Pitch Wheel Channel " + juce::String(ch + 1), "pitchWheel" + juce::String(ch + 1), VERSION_HINT, 0.0f, -1.0f, 1.0f, 0.0f, "");
            floatParameters.push_back(pitch[ch]);
        }
        lastPitch.fill(8192);
        modulation = new osci::FloatParameter("Modulation Wheel", "modulationWheel", VERSION_HINT, 0.0f, 0.0f, 1.0f, 0.0f, "");
        floatParameters.push_back(modulation);
    }
    juce::String getTypeId() const override { return "mw"; }
    juce::String getTypeLabel() const override { return "MW"; }
    int getSourceCount() const override { return 1; }
    const std::vector<float>* getBlockBuffers() const override { return &buffer; }
    float getCurrentValue(int) const override { return modulation->getValueUnnormalised(); }
    void prepareToPlay(double, int samples) override {
        buffer.resize(samples);
        pitchMultipliers.resize(samples, 1.0f);
    }
    void fillBlock(int samples, const juce::MidiBuffer& midi) {
        jassert(samples <= int(buffer.size()));
        int written = 0;
        bool receivedController = false;
        float value = modulation->getValueUnnormalised();
        for (const auto event : midi) {
            if (event.samplePosition >= samples) {
                break;
            }
            if (event.numBytes != 3) {
                continue;
            }
            const int status = event.data[0] & 0xf0;
            if (status == 0xe0) {
                const int channel = event.data[0] & 0x0f;
                const int bend = event.data[1] | (event.data[2] << 7);
                pitch[channel]->setValueUnnormalised((bend - 8192.0f) / 8192.0f);
                lastPitch[channel] = bend;
            }
            if (status != 0xb0 || event.data[1] != 1) {
                continue;
            }
            const int end = juce::jlimit(written, samples, event.samplePosition);
            std::fill(buffer.begin() + written, buffer.begin() + end, value);
            written = end;
            value = event.data[2] / 127.0f;
            receivedController = true;
        }
        std::fill(buffer.begin() + written, buffer.begin() + samples, value);
        if (receivedController) { modulation->setValueUnnormalised(value); }
    }
    void saveToXml(juce::XmlElement* root) const override {
        auto* xml = root->createNewChildElement("wheelAssignments");
        for (const auto& assignment : getAssignments()) { assignment.saveToXml(xml->createNewChildElement("assignment"), "mw"); }
    }
    void loadFromXml(const juce::XmlElement* root) override {
        assignments.clear();
        const auto* xml = root->getChildByName("wheelAssignments");
        if (xml != nullptr) {
            for (const auto* child : xml->getChildIterator()) { assignments.add(ModAssignment::loadFromXml(child, "mw")); }
        }
    }
};
