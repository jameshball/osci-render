#pragma once
#include <JuceHeader.h>

namespace Util {
    void changeSvgColour(juce::XmlElement* xml, juce::String colour) {
        forEachXmlChildElement(*xml, xmlnode) {
            xmlnode->setAttribute("fill", colour);
        }
    }
}