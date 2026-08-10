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

    inline juce::String makeInstallWarningMessage(const juce::Array<DetectedDawProcess>& detectedDaws) {
#if JUCE_LINUX
        juce::String message = "Keep this app or plugin host open until installation finishes. Restart it afterward to use the update.";
#else
        juce::String message = "Save your work before continuing. If this is running inside a DAW, close the host before completing the installer.";
#endif

        if (!detectedDaws.isEmpty()) {
#if JUCE_LINUX
            message << "\n\nDetected running DAW/plugin host processes: "
                    << DawProcessDetector::joinDisplayNames (detectedDaws)
                    << ".\n\nKeep them open until installation finishes, then restart them.";
#else
            message << "\n\nDetected running DAW/plugin host processes: "
                    << DawProcessDetector::joinDisplayNames (detectedDaws)
                    << ".\n\nClose them before completing the installer.";
#endif
        }

        return message;
    }

    inline juce::String makeOverlayCloseButtonSvg() {
        return juce::String::createStringFromData(BinaryData::close_svg, BinaryData::close_svgSize);
    }

    inline void showInstallError(juce::Component* parent, juce::StringRef title, juce::StringRef message) {
        if (parent != nullptr) {
            ErrorOverlay::Options options;
            options.closeButtonSvg = makeOverlayCloseButtonSvg();
            options.title = title;
            options.message = message;
            options.icon = ErrorOverlay::Icon::Error;
            options.preferredPanelSize = { 440, 320 };
            options.buttons.push_back({ "OK", {}, true });
            ErrorOverlay::show(*parent, std::move(options));
            return;
        }

        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               title,
                                               juce::String(message));
    }

    inline juce::Result launchInstallerWithPendingMarkerResult (const juce::File& installerFile,
                                                                const std::optional<VersionInfo>& version,
                                                                juce::StringRef product,
                                                                juce::StringRef currentVersion) {
        bool markerWritten = false;
        if (version.has_value()) {
            PendingInstall pending (product);
            const auto marker = PendingInstall::makeMarker (product, currentVersion, *version, installerFile);
            const auto markerResult = pending.write (marker);
            if (markerResult.failed()) {
                juce::Logger::writeToLog ("Pending install marker write failed: " + markerResult.getErrorMessage());
            } else {
                markerWritten = true;
            }
        }

        if (InstallerLauncher::launchAndExitHost (installerFile)) {
            return juce::Result::ok();
        }

        if (markerWritten) {
            PendingInstall (product).clear();
        }

        return juce::Result::fail ("Could not launch downloaded installer at " + installerFile.getFullPathName() + ".");
    }

    inline bool launchInstallerWithPendingMarker (const juce::File& installerFile,
                                                  const std::optional<VersionInfo>& version,
                                                  juce::StringRef product,
                                                  juce::StringRef currentVersion,
                                                  juce::Component* errorParent) {
        const auto result = launchInstallerWithPendingMarkerResult (installerFile, version, product, currentVersion);
        if (result.wasOk()) {
            return true;
        }

        showInstallError(errorParent, "Install Update", result.getErrorMessage());
        return false;
    }

    inline bool launchInstallerWithPendingMarker (const juce::File& installerFile,
                                                  const std::optional<VersionInfo>& version,
                                                  juce::StringRef product,
                                                  juce::StringRef currentVersion) {
        return launchInstallerWithPendingMarker(installerFile, version, product, currentVersion, nullptr);
    }

    using LinuxInstallCompletion = std::function<void (juce::Result, LinuxInstaller::Report)>;

    class LinuxUpdateInstallThread final : public juce::Thread {
    public:
        LinuxUpdateInstallThread (juce::File archiveToUse,
                                  VersionInfo versionToUse,
                                  juce::String productToUse,
                                  juce::String currentVersionToUse,
                                  LinuxInstaller::Progress progressToUse,
                                  LinuxInstallCompletion completionToUse)
            : juce::Thread ("Linux update installer"), archive (std::move (archiveToUse)), version (std::move (versionToUse)),
              product (std::move (productToUse)), currentVersion (std::move (currentVersionToUse)),
              progress (std::move (progressToUse)), completion (std::move (completionToUse)) {
        }

        ~LinuxUpdateInstallThread() override {
            stopThread (-1);
        }

        void run() override {
            juce::InterProcessLock updateLock ("osci-linux-update-" + product);
            if (!updateLock.enter (0)) {
                finish (juce::Result::fail ("Another update of " + product + " is already in progress"), {});
                return;
            }

            PendingInstall pending (product);
            const auto marker = PendingInstall::makeMarker (product, currentVersion, version, archive);
            auto result = pending.write (marker);
            LinuxInstaller::Report report;

            if (result.wasOk()) {
                result = PendingInstall::validateArtifact (marker);
            }

            LinuxInstallLocations locations;
            std::optional<LinuxInstallLocations> savedLocations;
            if (result.wasOk()) {
                result = LinuxInstallSettings (product).loadSaved (savedLocations);
                if (result.wasOk()) {
                    locations = savedLocations.value_or (LinuxInstallSettings::defaults());
                }
            }

            if (result.wasOk()) {
                const auto policy = savedLocations.has_value() ? LinuxInstaller::MissingDirectoryPolicy::Reject
                                                                : LinuxInstaller::MissingDirectoryPolicy::Create;
                result = LinuxInstaller::validateLocations (locations, policy);
                if (result.failed()) {
                    result = juce::Result::fail (result.getErrorMessage() + " Run osci-installer to choose installation locations again.");
                }
            }

            if (result.wasOk()) {
                LinuxInstaller::Request request;
                request.manifest = linuxInstallManifest (product);
                request.archive = archive;
                request.locations = locations;
                request.iconPng = linuxProductIconPng();
                request.progress = std::move (progress);
                request.missingDirectoryPolicy = savedLocations.has_value() ? LinuxInstaller::MissingDirectoryPolicy::Reject
                                                                             : LinuxInstaller::MissingDirectoryPolicy::Create;
                result = LinuxInstaller().install (request, report);
            }

            pending.clear();
            finish (result, std::move (report));
        }

    private:
        void finish (juce::Result result, LinuxInstaller::Report report) {
            juce::MessageManager::callAsync ([completion = std::move (completion), result, report = std::move (report)]() mutable {
                if (completion != nullptr) {
                    completion (result, std::move (report));
                }
            });
        }

        juce::File archive;
        VersionInfo version;
        juce::String product;
        juce::String currentVersion;
        LinuxInstaller::Progress progress;
        LinuxInstallCompletion completion;
    };

    inline std::unique_ptr<LinuxUpdateInstallThread> installLinuxUpdateAsync (
        const juce::File& archive,
        const VersionInfo& version,
        juce::String product,
        juce::String currentVersion,
        LinuxInstaller::Progress progress,
        LinuxInstallCompletion completion) {
        auto worker = std::make_unique<LinuxUpdateInstallThread> (archive, version, std::move (product),
                                                                  std::move (currentVersion), std::move (progress),
                                                                  std::move (completion));
        if (!worker->startThread()) {
            return nullptr;
        }
        return worker;
    }

    inline void showInstallConfirmation (juce::Component* parent,
                                         std::function<void()> onConfirmed,
                                         std::function<void()> onCancelled = {}) {
        const auto hasParent = parent != nullptr;
        auto safeParent = juce::Component::SafePointer<juce::Component> (parent);

        DawProcessDetector::scanAsync (
            [hasParent,
             safeParent,
             onConfirmed = std::move (onConfirmed),
             onCancelled = std::move (onCancelled)] (juce::Array<DetectedDawProcess> detectedDaws) mutable {
                if (hasParent && safeParent == nullptr) {
                    return;
                }

                const auto message = makeInstallWarningMessage(detectedDaws);
                auto confirmedCallback = std::make_shared<std::function<void()>>(std::move(onConfirmed));
                auto cancelledCallback = std::make_shared<std::function<void()>>(std::move(onCancelled));

                if (hasParent) {
                    ErrorOverlay::Options options;
                    options.closeButtonSvg = makeOverlayCloseButtonSvg();
                    options.title = "Install Update";
                    options.message = message;
                    options.icon = ErrorOverlay::Icon::Warning;
                    options.messageJustification = juce::Justification::centredTop;
                    options.preferredPanelSize = detectedDaws.isEmpty() ? juce::Point<int> { 460, 270 }
                                                                        : juce::Point<int> { 500, 340 };
                    options.buttons.push_back({
                        "Install",
                        [confirmedCallback] {
                            if (*confirmedCallback != nullptr) {
                                (*confirmedCallback)();
                            }
                        },
                        true,
                    });
                    options.buttons.push_back({
                        "Cancel",
                        [cancelledCallback] {
                            if (*cancelledCallback != nullptr) {
                                (*cancelledCallback)();
                            }
                        },
                        false,
                    });
                    options.onDismissed = [cancelledCallback] {
                        if (*cancelledCallback != nullptr) {
                            (*cancelledCallback)();
                        }
                    };

                    ErrorOverlay::show(*safeParent.getComponent(), std::move(options));
                    return;
                }

                juce::AlertWindow::showOkCancelBox (
                    juce::AlertWindow::WarningIcon,
                    "Install Update",
                    message,
                    "Install",
                    "Cancel",
                    hasParent ? safeParent.getComponent() : nullptr,
                    juce::ModalCallbackFunction::create ([confirmedCallback,
                                                          cancelledCallback] (int result) mutable {
                        if (result == 0) {
                            if (*cancelledCallback != nullptr) {
                                (*cancelledCallback)();
                            }

                            return;
                        }

                        if (*confirmedCallback != nullptr) {
                            (*confirmedCallback)();
                        }
                    }));
            });
    }
}
