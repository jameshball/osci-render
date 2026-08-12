#pragma once

#include <JuceHeader.h>

namespace osci {
    inline LinuxInstallManifest linuxInstallManifest (juce::StringRef product) {
        if (juce::String (product) == "sosci") {
            return { "sosci", "sosci", "sosci", { "sosci.vst3" }, {}, "AudioVideo;Audio;" };
        }

        return { "osci-render", "osci-render", "osci-render",
                 { "osci-render.vst3" }, { "osci-render-instrument.vst3" }, "AudioVideo;Audio;" };
    }

    inline juce::MemoryBlock linuxProductIconPng (juce::StringRef product) {
        if (juce::String (product) == "sosci") {
            return { BinaryData::sosci_mac_saturated_png, static_cast<size_t> (BinaryData::sosci_mac_saturated_pngSize) };
        }

        return { BinaryData::osci_mac_png, static_cast<size_t> (BinaryData::osci_mac_pngSize) };
    }

    inline juce::String makeOverlayCloseButtonSvg() {
        return juce::String::createStringFromData(BinaryData::close_svg, BinaryData::close_svgSize);
    }
}
