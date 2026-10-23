#pragma once

#include <JuceHeader.h>
#include "../ProductIdentity.h"
#include "InstallFlowHelpers.h"

namespace osci {

inline ProductUpdateConfig makeProductUpdateConfig (std::function<void()> onUpdateSettingsChanged = {}) {
    ProductUpdateConfig config;
    config.currentVersion = JucePlugin_VersionString;
#if OSCI_PREMIUM
    config.compiledVariant = "premium";
#else
    config.compiledVariant = "free";
#endif
    config.productSlug = currentProductSlug();
    config.productName = config.productSlug;
    config.linuxIconPng = linuxProductIconPng();
    config.productIcon = juce::ImageFileFormat::loadFrom(config.linuxIconPng.getData(), config.linuxIconPng.getSize());
    config.linuxInstallManifest = linuxInstallManifest(config.productSlug);
    config.hasFreeFallback = config.productSlug == juce::String("osci-render");
    config.copyIconSvg = juce::String::createStringFromData (BinaryData::copy_svg, BinaryData::copy_svgSize);
    config.revealIconSvg = juce::String::createStringFromData (BinaryData::eye_svg, BinaryData::eye_svgSize);
    config.concealIconSvg = juce::String::createStringFromData (BinaryData::eyeoff_svg, BinaryData::eyeoff_svgSize);
    config.helpIconSvg = juce::String::createStringFromData (BinaryData::help_svg, BinaryData::help_svgSize);
    config.onUpdateSettingsChanged = std::move (onUpdateSettingsChanged);
    return config;
}

} // namespace osci
