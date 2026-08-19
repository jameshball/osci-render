#pragma once

#include <JuceHeader.h>

namespace osci {

inline ProductUpdateConfig makeProductUpdateConfig (std::function<void()> onUpdateSettingsChanged = {}) {
    ProductUpdateConfig config;
    config.currentVersion = JucePlugin_VersionString;
#if OSCI_PREMIUM
    config.compiledVariant = "premium";
#else
    config.compiledVariant = "free";
#endif
#if defined (SOSCI)
    config.productSlug = "sosci";
    config.productName = "sosci";
    config.productIcon = juce::ImageFileFormat::loadFrom (BinaryData::sosci_mac_saturated_png,
                                                          static_cast<size_t> (BinaryData::sosci_mac_saturated_pngSize));
    config.linuxIconPng = { BinaryData::sosci_mac_saturated_png,
                            static_cast<size_t> (BinaryData::sosci_mac_saturated_pngSize) };
    config.linuxInstallManifest = { "sosci", "sosci", "sosci", { "sosci.vst3" }, {}, "AudioVideo;Audio;" };
#else
    config.productSlug = "osci-render";
    config.productName = "osci-render";
    config.productIcon = juce::ImageFileFormat::loadFrom (BinaryData::osci_mac_png,
                                                          static_cast<size_t> (BinaryData::osci_mac_pngSize));
    config.linuxIconPng = { BinaryData::osci_mac_png, static_cast<size_t> (BinaryData::osci_mac_pngSize) };
    config.linuxInstallManifest = { "osci-render", "osci-render", "osci-render",
                                    { "osci-render.vst3" }, { "osci-render-instrument.vst3" }, "AudioVideo;Audio;" };
    config.hasFreeFallback = true;
#endif
    config.copyIconSvg = juce::String::createStringFromData (BinaryData::copy_svg, BinaryData::copy_svgSize);
    config.revealIconSvg = juce::String::createStringFromData (BinaryData::eye_svg, BinaryData::eye_svgSize);
    config.concealIconSvg = juce::String::createStringFromData (BinaryData::eyeoff_svg, BinaryData::eyeoff_svgSize);
    config.helpIconSvg = juce::String::createStringFromData (BinaryData::help_svg, BinaryData::help_svgSize);
    config.onUpdateSettingsChanged = std::move (onUpdateSettingsChanged);
    return config;
}

} // namespace osci
