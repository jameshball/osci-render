#pragma once

#include <JuceHeader.h>

// Generic modulation assignment: any modulation source → any parameter.
// Used by LFO, Envelope, and future modulation sources (e.g. random, step sequencer).
struct ModAssignment {
    int sourceIndex = 0;       // Which source (0–N) within its type
    juce::String paramId;      // Target parameter ID
    float depth = 0.5f;        // Modulation depth [-1,1]
    bool bipolar = false;      // If true, modulates symmetrically around current value

    void saveToXml(juce::XmlElement* parent, const juce::String& indexAttrName) const {
        parent->setAttribute(indexAttrName, sourceIndex);
        parent->setAttribute("param", paramId);
        parent->setAttribute("depth", (double)depth);
        parent->setAttribute("bipolar", bipolar);
    }

    static ModAssignment loadFromXml(const juce::XmlElement* xml, const juce::String& indexAttrName) {
        ModAssignment a;
        a.sourceIndex = xml->getIntAttribute(indexAttrName, 0);
        a.paramId = xml->getStringAttribute("param");
        a.depth = (float)xml->getDoubleAttribute("depth", 0.5);
        a.bipolar = xml->getBoolAttribute("bipolar", false);
        return a;
    }
};
