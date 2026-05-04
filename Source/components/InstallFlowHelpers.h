#pragma once

#include <JuceHeader.h>

namespace osci {
    inline juce::String makeInstallWarningMessage (const juce::Array<DetectedDawProcess>& detectedDaws) {
        juce::String message = "Save your work before continuing. If this is running inside a DAW, close the host before completing the installer.";

        if (! detectedDaws.isEmpty()) {
            message << "\n\nDetected running DAW/plugin host processes: "
                    << DawProcessDetector::joinDisplayNames (detectedDaws)
                    << ".\n\nClose them before completing the installer.";
        }

        return message;
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
                                                  juce::StringRef currentVersion) {
        const auto result = launchInstallerWithPendingMarkerResult (installerFile, version, product, currentVersion);
        if (result.wasOk()) {
            return true;
        }

        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                "Install Update",
                                                result.getErrorMessage());
        return false;
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

                const auto message = makeInstallWarningMessage (detectedDaws);

                juce::AlertWindow::showOkCancelBox (
                    juce::AlertWindow::WarningIcon,
                    "Install Update",
                    message,
                    "Install",
                    "Cancel",
                    hasParent ? safeParent.getComponent() : nullptr,
                    juce::ModalCallbackFunction::create ([onConfirmed = std::move (onConfirmed),
                                                          onCancelled = std::move (onCancelled)] (int result) mutable {
                        if (result == 0) {
                            if (onCancelled != nullptr) {
                                onCancelled();
                            }

                            return;
                        }

                        onConfirmed();
                    }));
            });
    }
}
