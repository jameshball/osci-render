#pragma once

#include <JuceHeader.h>

namespace osci {
    inline juce::String makeInstallWarningMessage(const juce::Array<DetectedDawProcess>& detectedDaws) {
        juce::String message = "Save your work before continuing. If this is running inside a DAW, close the host before completing the installer.";

        if (!detectedDaws.isEmpty()) {
            message << "\n\nDetected running DAW/plugin host processes: "
                    << DawProcessDetector::joinDisplayNames (detectedDaws)
                    << ".\n\nClose them before completing the installer.";
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
            options.preferredPanelSize = { 420, 420 };
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
