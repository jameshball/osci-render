#pragma once

#include <JuceHeader.h>

namespace osci {
    inline LinuxInstallManifest linuxInstallManifest (juce::StringRef product) {
        const auto slug = juce::String(product);
        if (slug == "sosci") {
            return { "sosci", "sosci", "sosci", { "sosci.vst3" }, {}, "AudioVideo;Audio;" };
        }
        if (slug == "osci-laser") {
            return { "osci-laser", "osci-laser", "osci-laser", { "osci-laser.vst3" }, {}, "AudioVideo;Audio;" };
        }

        return { "osci-render", "osci-render", "osci-render",
                 { "osci-render.vst3" }, { "osci-render-instrument.vst3" }, "AudioVideo;Audio;" };
    }

    inline juce::MemoryBlock linuxProductIconPng() {
#if defined (SOSCI)
        return { BinaryData::sosci_mac_saturated_png, static_cast<size_t> (BinaryData::sosci_mac_saturated_pngSize) };
#else
        return { BinaryData::osci_mac_png, static_cast<size_t> (BinaryData::osci_mac_pngSize) };
#endif
    }

}
