#pragma once

#include <JuceHeader.h>
#include "../CommonPluginProcessor.h"

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

    inline bool launchInstallerWithPendingMarker (const juce::File& installerFile,
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
            return true;
        }

        if (markerWritten) {
            PendingInstall (product).clear();
        }

        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                "Install Update",
                                                "Could not launch the downloaded installer.");
        return false;
    }

    inline void showInstallConfirmation (juce::Component* parent, std::function<void()> onConfirmed) {
        const auto detectedDaws = DawProcessDetector::scan();
        const auto message = makeInstallWarningMessage (detectedDaws);

        juce::AlertWindow::showOkCancelBox (
            juce::AlertWindow::WarningIcon,
            "Install Update",
            message,
            "Install",
            "Cancel",
            parent,
            juce::ModalCallbackFunction::create ([onConfirmed = std::move (onConfirmed)] (int result) mutable {
                if (result == 0) {
                    return;
                }

                onConfirmed();
            }));
    }
}
