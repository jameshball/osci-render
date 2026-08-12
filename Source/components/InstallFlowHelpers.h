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

    inline juce::MemoryBlock linuxProductIconPng() {
#if defined (SOSCI)
        return { BinaryData::sosci_mac_saturated_png, static_cast<size_t> (BinaryData::sosci_mac_saturated_pngSize) };
#else
        return { BinaryData::osci_mac_png, static_cast<size_t> (BinaryData::osci_mac_pngSize) };
#endif
    }

    inline juce::String makeOverlayCloseButtonSvg() {
        return juce::String::createStringFromData(BinaryData::close_svg, BinaryData::close_svgSize);
    }

    inline void showInstallError(juce::Component* parent, juce::StringRef title, juce::StringRef message) {
        InstallPrompt::showError (parent, makeOverlayCloseButtonSvg(), title, message);
    }

    inline bool launchInstallerWithPendingMarker (const juce::File& installerFile,
                                                  const std::optional<VersionInfo>& version,
                                                  juce::StringRef product,
                                                  juce::StringRef currentVersion,
                                                  juce::Component* errorParent) {
        const auto result = UpdateInstaller::launchWithPendingMarker (
            { installerFile, version, juce::String (product), juce::String (currentVersion) });
        if (result.wasOk()) {
            return true;
        }

        showInstallError(errorParent, "Install Update", result.getErrorMessage());
        return false;
    }

    inline std::unique_ptr<LinuxUpdateInstallThread> installLinuxUpdateAsync (
        const juce::File& archive,
        const VersionInfo& version,
        juce::String product,
        juce::String currentVersion,
        LinuxInstaller::Progress progress,
        std::function<void (juce::Result, LinuxInstaller::Report)> completion) {
        LinuxUpdateInstallThread::Request request;
        request.archive = archive;
        request.version = version;
        request.currentVersion = std::move (currentVersion);
        request.manifest = linuxInstallManifest (product);
        request.iconPng = linuxProductIconPng();
        request.progress = std::move (progress);
        request.completion = std::move (completion);
        return UpdateInstaller::installLinuxAsync (std::move (request));
    }

    inline void showInstallConfirmation (juce::Component* parent,
                                         std::function<void()> onConfirmed,
                                         std::function<void()> onCancelled = {}) {
        InstallPrompt::showConfirmation ({ parent, makeOverlayCloseButtonSvg(), std::move (onConfirmed), std::move (onCancelled) });
    }
}
