#pragma once

#include <juce_core/juce_core.h>
#include <osci_gui/visualiser/osci_VisualiserGeometry.h>

namespace RecordingStateMigration {
    inline int getLegacyResolution(const juce::XmlElement* settingsXml) {
        if (settingsXml == nullptr) {
            return 0;
        }

        auto* resolutionXml = settingsXml->getChildByName("resolution");
        if (resolutionXml == nullptr) {
            return 0;
        }

        for (const auto* parameterXml : resolutionXml->getChildIterator()) {
            if (parameterXml->getStringAttribute("id") == "resolution" && parameterXml->hasAttribute("value")) {
                return parameterXml->getIntAttribute("value");
            }
        }

        return 0;
    }

    inline VisualiserRenderSize getLegacyCanvasSize(const juce::XmlElement* settingsXml, VisualiserRenderSize fallback = {1024, 1024}) {
        const int legacyResolution = getLegacyResolution(settingsXml);
        if (legacyResolution > 0) {
            return VisualiserGeometry::sanitiseRenderSize(legacyResolution, legacyResolution);
        }

        return VisualiserGeometry::sanitiseRenderSize(fallback.width, fallback.height);
    }
}
