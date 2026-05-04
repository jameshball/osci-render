#pragma once

#include <JuceHeader.h>
#include "../CommonPluginProcessor.h"
#include "InstallFlowHelpers.h"
#include "../LookAndFeel.h"

class UpdatePromptComponent : public juce::Component {
public:
    explicit UpdatePromptComponent (CommonAudioProcessor& processorToUse)
        : processor (processorToUse), progressBar (progressValue) {
        setVisible (false);
        setAlwaysOnTop (true);

        titleLabel.setFont (juce::Font (14.0f, juce::Font::bold));
        titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        titleLabel.setJustificationType (juce::Justification::centredLeft);

        detailLabel.setFont (juce::Font (12.0f));
        detailLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.72f));
        detailLabel.setJustificationType (juce::Justification::centredLeft);

        primaryButton.setColour (juce::TextButton::buttonColourId, osci::Colours::accentColor());
        primaryButton.setColour (juce::TextButton::textColourOffId, osci::Colours::veryDark());
        secondaryButton.setColour (juce::TextButton::buttonColourId, osci::Colours::veryDark().brighter (0.12f));

        primaryButton.onClick = [this] {
            if (mode == Mode::Ready) {
                installUpdate();
            } else if (mode == Mode::PendingInstallFailed) {
                retryPendingInstall();
            } else if (mode == Mode::InstallSucceeded) {
                hidePrompt();
            } else if (mode == Mode::Available || mode == Mode::Failed) {
                downloadUpdate();
            }
        };

        secondaryButton.onClick = [this] { dismissFor48Hours(); };

        addAndMakeVisible (titleLabel);
        addAndMakeVisible (detailLabel);
        addAndMakeVisible (primaryButton);
        addAndMakeVisible (secondaryButton);
        addChildComponent (progressBar);
    }

    void scheduleInitialCheck() {
        auto safeThis = juce::Component::SafePointer<UpdatePromptComponent> (this);
        juce::Timer::callAfterDelay (1800, [safeThis] {
            if (safeThis != nullptr) {
                safeThis->checkForUpdate (false);
            }
        });
    }

    bool showPendingInstallStatusIfNeeded() {
        osci::PendingInstall pending (processor.getProductSlug());
        auto marker = pending.load();
        if (! marker.has_value()) {
            return false;
        }

        if (osci::PendingInstall::isResolvedByRunningVersion (*marker, JucePlugin_VersionString)) {
            pending.clear();
            showInstallSucceeded (*marker);
            return true;
        }

        pending.clear();
        showPendingInstallFailed (*marker);
        return true;
    }

    int getPreferredHeight() const noexcept { return 118; }

    void paint (juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (osci::Colours::veryDark().withAlpha (0.96f));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (osci::Colours::accentColor().withAlpha (0.55f));
        g.drawRoundedRectangle (bounds, 8.0f, 1.2f);
    }

    void resized() override {
        auto area = getLocalBounds().reduced (14, 12);
        titleLabel.setBounds (area.removeFromTop (20));
        detailLabel.setBounds (area.removeFromTop (34));
        area.removeFromTop (8);

        if (progressBar.isVisible()) {
            progressBar.setBounds (area.removeFromTop (8));
            area.removeFromTop (10);
        }

        auto buttonRow = area.removeFromBottom (28);
        if (secondaryButton.isVisible()) {
            secondaryButton.setBounds (buttonRow.removeFromRight (82));
            buttonRow.removeFromRight (8);
        } else {
            secondaryButton.setBounds ({});
        }
        primaryButton.setBounds (buttonRow.removeFromRight (98));
    }

private:
    enum class Mode {
        Hidden,
        Available,
        Downloading,
        Ready,
        Failed,
        InstallSucceeded,
        PendingInstallFailed,
    };

    CommonAudioProcessor& processor;
    juce::Label titleLabel;
    juce::Label detailLabel;
    juce::TextButton primaryButton { "Update" };
    juce::TextButton secondaryButton { "Later" };
    double progressValue = 0.0;
    juce::ProgressBar progressBar;
    std::optional<osci::VersionInfo> availableVersion;
    std::optional<osci::PendingInstallMarker> pendingInstallRetryMarker;
    juce::File downloadedFile;
    Mode mode = Mode::Hidden;
    bool busy = false;

    void checkForUpdate (bool showErrors) {
        if (busy || mode == Mode::InstallSucceeded || mode == Mode::PendingInstallFailed) {
            return;
        }

        busy = true;
        const auto product = processor.getProductSlug();
        const auto currentVersion = juce::String (JucePlugin_VersionString);
        const auto track = selectedReleaseTrack();
        const auto variant = desiredUpdateVariant();
        auto foundVersion = std::make_shared<std::optional<osci::VersionInfo>>();
        auto result = std::make_shared<juce::Result> (juce::Result::ok());
        auto safeThis = juce::Component::SafePointer<UpdatePromptComponent> (this);

        juce::Thread::launch ([safeThis, foundVersion, result, product, currentVersion, track, variant, showErrors] {
            osci::UpdateChecker checker;
            *foundVersion = checker.checkForUpdate (product, currentVersion, track, variant);
            *result = checker.getLastResult();
            if (result->failed()) {
                *result = failWithContext ("Could not check for updates", *result);
            }

            juce::MessageManager::callAsync ([safeThis, foundVersion, result, showErrors] {
                if (safeThis == nullptr) {
                    return;
                }

                safeThis->busy = false;
                if (result->failed()) {
                    if (showErrors) {
                        safeThis->showFailure (result->getErrorMessage());
                    }
                    return;
                }

                if (! foundVersion->has_value()) {
                    return;
                }

                if (safeThis->isDismissed ((*foundVersion)->semver)) {
                    return;
                }

                safeThis->showAvailable (**foundVersion);
            });
        });
    }

    void showAvailable (const osci::VersionInfo& version) {
        availableVersion = version;
        downloadedFile = juce::File();
        mode = Mode::Available;
        progressBar.setVisible (false);
        titleLabel.setText ("Update available", juce::dontSendNotification);
        detailLabel.setText ("Version " + version.semver + " is ready for " + version.releaseTrack + ".", juce::dontSendNotification);
        primaryButton.setButtonText ("Update");
        secondaryButton.setButtonText ("Later");
        secondaryButton.setVisible (true);
        primaryButton.setEnabled (true);
        secondaryButton.setEnabled (true);
        setVisible (true);
        refreshParentLayout();
        repaint();
    }

    void showFailure (juce::StringRef message) {
        mode = Mode::Failed;
        progressBar.setVisible (false);
        titleLabel.setText ("Update failed", juce::dontSendNotification);
        detailLabel.setText (message, juce::dontSendNotification);
        primaryButton.setButtonText ("Retry");
        secondaryButton.setButtonText ("Later");
        secondaryButton.setVisible (true);
        primaryButton.setEnabled (true);
        secondaryButton.setEnabled (true);
        setVisible (true);
        refreshParentLayout();
        repaint();
    }

    void downloadUpdate() {
        if (! availableVersion.has_value() || busy) {
            return;
        }

        busy = true;
        mode = Mode::Downloading;
        progressValue = 0.0;
        progressBar.setVisible (true);
        titleLabel.setText ("Downloading update", juce::dontSendNotification);
        detailLabel.setText ("Preparing verified installer...", juce::dontSendNotification);
        primaryButton.setEnabled (false);
        secondaryButton.setEnabled (false);
        resized();

        auto downloaded = std::make_shared<juce::File>();
        auto result = std::make_shared<juce::Result> (juce::Result::ok());
        const auto version = *availableVersion;
        const auto token = processor.licenseManager.getCachedToken();
        const auto product = processor.getProductSlug();
        auto safeThis = juce::Component::SafePointer<UpdatePromptComponent> (this);

        juce::Thread::launch ([safeThis, downloaded, result, version, token, product] {
            osci::Downloader::Config config;
            config.downloadDirectory = osci::HardwareInfo::getDefaultStorageDirectory (product).getChildFile ("downloads");
            osci::Downloader downloader (config);
            *result = downloader.downloadAndVerify (version, token, [safeThis] (double fraction, juce::int64) {
                juce::MessageManager::callAsync ([safeThis, fraction] {
                    if (safeThis != nullptr && fraction >= 0.0) {
                        safeThis->progressValue = juce::jlimit (0.0, 1.0, fraction);
                        safeThis->repaint();
                    }
                });
            });

            if (result->wasOk()) {
                *downloaded = downloader.getDownloadedFile();
            } else {
                *result = failWithContext ("Could not download update", *result);
            }

            juce::MessageManager::callAsync ([safeThis, downloaded, result] {
                if (safeThis == nullptr) {
                    return;
                }

                safeThis->busy = false;
                if (result->failed()) {
                    safeThis->showFailure (result->getErrorMessage());
                    return;
                }

                safeThis->downloadedFile = *downloaded;
                safeThis->mode = Mode::Ready;
                safeThis->progressValue = 1.0;
                safeThis->progressBar.setVisible (false);
                safeThis->titleLabel.setText ("Ready to install", juce::dontSendNotification);
                safeThis->detailLabel.setText ("The update was downloaded and verified.", juce::dontSendNotification);
                safeThis->primaryButton.setButtonText ("Install");
                safeThis->secondaryButton.setButtonText ("Later");
                safeThis->secondaryButton.setVisible (true);
                safeThis->primaryButton.setEnabled (true);
                safeThis->secondaryButton.setEnabled (true);
                safeThis->resized();
                safeThis->repaint();
            });
        });
    }

    void installUpdate() {
        if (! downloadedFile.existsAsFile()) {
            return;
        }

        const auto file = downloadedFile;
        const auto version = availableVersion;
        const auto product = processor.getProductSlug();
        const auto currentVersion = juce::String (JucePlugin_VersionString);
        auto safeThis = juce::Component::SafePointer<UpdatePromptComponent> (this);

        osci::showInstallConfirmation (
            this,
            [safeThis, file, version, product, currentVersion] {
                if (! osci::launchInstallerWithPendingMarker (file, version, product, currentVersion)) {
                    return;
                }

                if (safeThis != nullptr) {
                    safeThis->hidePrompt();
                }
            });
    }

    void dismissFor48Hours() {
        if (availableVersion.has_value()) {
            osci::UpdateSettings(processor.getProductSlug()).dismiss (availableVersion->semver, currentTimeSeconds());
        }

        hidePrompt();
    }

    bool isDismissed (const juce::String& semver) const {
        return osci::UpdateSettings(processor.getProductSlug()).isDismissed (semver, currentTimeSeconds());
    }

    osci::ReleaseTrack selectedReleaseTrack() const {
        return osci::UpdateSettings(processor.getProductSlug()).releaseTrack();
    }

    juce::String desiredUpdateVariant() const {
        if (processor.licenseManager.hasPremium()) {
            return "premium";
        }

        return compiledVariant();
    }

    static juce::String compiledVariant() {
#if OSCI_PREMIUM
        return "premium";
#else
        return "free";
#endif
    }

    static double currentTimeSeconds() {
        return static_cast<double> (juce::Time::currentTimeMillis()) / 1000.0;
    }

    static juce::Result failWithContext (juce::StringRef context, const juce::Result& result) {
        return juce::Result::fail (juce::String (context) + ": " + result.getErrorMessage());
    }

    void showInstallSucceeded (const osci::PendingInstallMarker& marker) {
        availableVersion.reset();
        pendingInstallRetryMarker.reset();
        downloadedFile = juce::File();
        mode = Mode::InstallSucceeded;
        progressBar.setVisible (false);
        titleLabel.setText ("Update installed", juce::dontSendNotification);
        detailLabel.setText ("Version " + marker.targetVersion + " is now running.", juce::dontSendNotification);
        primaryButton.setButtonText ("OK");
        secondaryButton.setVisible (false);
        primaryButton.setEnabled (true);
        setVisible (true);
        refreshParentLayout();
        repaint();
    }

    void showPendingInstallFailed (const osci::PendingInstallMarker& marker) {
        pendingInstallRetryMarker = marker;
        availableVersion = marker.toVersionInfo();
        downloadedFile = marker.artifactPath.isNotEmpty() ? juce::File (marker.artifactPath) : juce::File();
        mode = Mode::PendingInstallFailed;
        progressBar.setVisible (false);
        titleLabel.setText ("Install did not complete", juce::dontSendNotification);
        detailLabel.setText ("Version " + marker.targetVersion + " was not installed.", juce::dontSendNotification);
        primaryButton.setButtonText ("Retry");
        secondaryButton.setButtonText ("Later");
        secondaryButton.setVisible (true);
        primaryButton.setEnabled (true);
        secondaryButton.setEnabled (true);
        setVisible (true);
        refreshParentLayout();
        repaint();
    }

    void retryPendingInstall() {
        if (! pendingInstallRetryMarker.has_value()) {
            return;
        }

        availableVersion = pendingInstallRetryMarker->toVersionInfo();

        if (osci::PendingInstall::validateArtifact (*pendingInstallRetryMarker).wasOk()) {
            downloadedFile = juce::File (pendingInstallRetryMarker->artifactPath);
            installUpdate();
            return;
        }

        downloadedFile = juce::File();
        downloadUpdate();
    }

    void hidePrompt() {
        mode = Mode::Hidden;
        setVisible (false);
        refreshParentLayout();
    }

    void refreshParentLayout() {
        auto* parent = getParentComponent();
        if (parent != nullptr) {
            parent->resized();
        } else {
            resized();
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpdatePromptComponent)
};
