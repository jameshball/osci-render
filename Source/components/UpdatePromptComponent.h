#pragma once

#include <JuceHeader.h>
#include "../CommonPluginProcessor.h"
#include "../LookAndFeel.h"

class UpdatePromptComponent : public juce::Component
{
public:
    explicit UpdatePromptComponent (CommonAudioProcessor& processorToUse)
        : processor (processorToUse), progressBar (progressValue)
    {
        setVisible (false);
        setAlwaysOnTop (true);

        titleLabel.setFont (juce::Font (14.0f, juce::Font::bold));
        titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        titleLabel.setJustificationType (juce::Justification::centredLeft);

        detailLabel.setFont (juce::Font (12.0f));
        detailLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.72f));
        detailLabel.setJustificationType (juce::Justification::centredLeft);

        primaryButton.setColour (juce::TextButton::buttonColourId, Colours::accentColor());
        primaryButton.setColour (juce::TextButton::textColourOffId, Colours::veryDark());
        secondaryButton.setColour (juce::TextButton::buttonColourId, Colours::veryDark().brighter (0.12f));

        primaryButton.onClick = [this]
        {
            if (mode == Mode::Ready)
                installUpdate();
            else if (mode == Mode::Available || mode == Mode::Failed)
                downloadUpdate();
        };

        secondaryButton.onClick = [this] { dismissFor48Hours(); };

        addAndMakeVisible (titleLabel);
        addAndMakeVisible (detailLabel);
        addAndMakeVisible (primaryButton);
        addAndMakeVisible (secondaryButton);
        addChildComponent (progressBar);
    }

    void scheduleInitialCheck()
    {
        auto safeThis = juce::Component::SafePointer<UpdatePromptComponent> (this);
        juce::Timer::callAfterDelay (1800, [safeThis]
        {
            if (safeThis != nullptr)
                safeThis->checkForUpdate (false);
        });
    }

    int getPreferredHeight() const noexcept { return 118; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Colours::veryDark().withAlpha (0.96f));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (Colours::accentColor().withAlpha (0.55f));
        g.drawRoundedRectangle (bounds, 8.0f, 1.2f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14, 12);
        titleLabel.setBounds (area.removeFromTop (20));
        detailLabel.setBounds (area.removeFromTop (34));
        area.removeFromTop (8);

        if (progressBar.isVisible())
        {
            progressBar.setBounds (area.removeFromTop (8));
            area.removeFromTop (10);
        }

        auto buttonRow = area.removeFromBottom (28);
        secondaryButton.setBounds (buttonRow.removeFromRight (82));
        buttonRow.removeFromRight (8);
        primaryButton.setBounds (buttonRow.removeFromRight (98));
    }

private:
    enum class Mode
    {
        Hidden,
        Available,
        Downloading,
        Ready,
        Failed,
    };

    CommonAudioProcessor& processor;
    juce::Label titleLabel;
    juce::Label detailLabel;
    juce::TextButton primaryButton { "Update" };
    juce::TextButton secondaryButton { "Later" };
    double progressValue = 0.0;
    juce::ProgressBar progressBar;
    std::optional<osci::licensing::VersionInfo> availableVersion;
    juce::File downloadedFile;
    Mode mode = Mode::Hidden;
    bool busy = false;

    void checkForUpdate (bool showErrors)
    {
        if (busy)
            return;

        busy = true;
        const auto product = processor.getProductSlug();
        const auto currentVersion = juce::String (JucePlugin_VersionString);
        const auto track = selectedReleaseTrack();
        const auto variant = desiredUpdateVariant();
        auto foundVersion = std::make_shared<std::optional<osci::licensing::VersionInfo>>();
        auto result = std::make_shared<juce::Result> (juce::Result::ok());
        auto safeThis = juce::Component::SafePointer<UpdatePromptComponent> (this);

        juce::Thread::launch ([safeThis, foundVersion, result, product, currentVersion, track, variant, showErrors]
        {
            osci::licensing::UpdateChecker checker;
            *foundVersion = checker.checkForUpdate (product, currentVersion, track, variant);
            *result = checker.getLastResult();

            juce::MessageManager::callAsync ([safeThis, foundVersion, result, showErrors]
            {
                if (safeThis == nullptr)
                    return;

                safeThis->busy = false;
                if (result->failed())
                {
                    if (showErrors)
                        safeThis->showFailure (result->getErrorMessage());
                    return;
                }

                if (! foundVersion->has_value())
                    return;

                if (safeThis->isDismissed ((*foundVersion)->semver))
                    return;

                safeThis->showAvailable (**foundVersion);
            });
        });
    }

    void showAvailable (const osci::licensing::VersionInfo& version)
    {
        availableVersion = version;
        downloadedFile = juce::File();
        mode = Mode::Available;
        progressBar.setVisible (false);
        titleLabel.setText ("Update available", juce::dontSendNotification);
        detailLabel.setText ("Version " + version.semver + " is ready for " + version.releaseTrack + ".", juce::dontSendNotification);
        primaryButton.setButtonText ("Update");
        secondaryButton.setButtonText ("Later");
        primaryButton.setEnabled (true);
        secondaryButton.setEnabled (true);
        setVisible (true);
        refreshParentLayout();
        repaint();
    }

    void showFailure (juce::StringRef message)
    {
        mode = Mode::Failed;
        progressBar.setVisible (false);
        titleLabel.setText ("Update failed", juce::dontSendNotification);
        detailLabel.setText (message, juce::dontSendNotification);
        primaryButton.setButtonText ("Retry");
        secondaryButton.setButtonText ("Later");
        primaryButton.setEnabled (true);
        secondaryButton.setEnabled (true);
        setVisible (true);
        refreshParentLayout();
        repaint();
    }

    void downloadUpdate()
    {
        if (! availableVersion.has_value() || busy)
            return;

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
        const auto token = processor.getLicenseManager().getCachedToken();
        const auto product = processor.getProductSlug();
        auto safeThis = juce::Component::SafePointer<UpdatePromptComponent> (this);

        juce::Thread::launch ([safeThis, downloaded, result, version, token, product]
        {
            osci::licensing::Downloader::Config config;
            config.downloadDirectory = osci::licensing::HardwareInfo::getDefaultStorageDirectory (product).getChildFile ("downloads");
            osci::licensing::Downloader downloader (config);
            *result = downloader.downloadAndVerify (version, token, [safeThis] (double fraction, juce::int64)
            {
                juce::MessageManager::callAsync ([safeThis, fraction]
                {
                    if (safeThis != nullptr && fraction >= 0.0)
                    {
                        safeThis->progressValue = juce::jlimit (0.0, 1.0, fraction);
                        safeThis->repaint();
                    }
                });
            });

            if (result->wasOk())
                *downloaded = downloader.getDownloadedFile();

            juce::MessageManager::callAsync ([safeThis, downloaded, result]
            {
                if (safeThis == nullptr)
                    return;

                safeThis->busy = false;
                if (result->failed())
                {
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
                safeThis->primaryButton.setEnabled (true);
                safeThis->secondaryButton.setEnabled (true);
                safeThis->resized();
                safeThis->repaint();
            });
        });
    }

    void installUpdate()
    {
        if (! downloadedFile.existsAsFile())
            return;

        const auto file = downloadedFile;
        juce::AlertWindow::showOkCancelBox (
            juce::AlertWindow::WarningIcon,
            "Install Update",
            "Save your work before continuing. If this is running inside a DAW, close the host before completing the installer.",
            "Install",
            "Cancel",
            this,
            juce::ModalCallbackFunction::create ([file] (int result)
            {
                if (result == 0)
                    return;

                if (! osci::licensing::InstallerLauncher::launchAndExitHost (file))
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                            "Install Update",
                                                            "Could not launch the downloaded installer.");
                }
            }));
    }

    void dismissFor48Hours()
    {
        if (availableVersion.has_value())
        {
            processor.setGlobalValue ("dismissedUpdateSemver", availableVersion->semver);
            processor.setGlobalValue ("dismissedUpdateAt", currentTimeSeconds());
            processor.saveGlobalSettings();
        }

        mode = Mode::Hidden;
        setVisible (false);
    }

    bool isDismissed (const juce::String& semver) const
    {
        if (processor.getGlobalStringValue ("dismissedUpdateSemver") != semver)
            return false;

        const auto dismissedAt = processor.getGlobalDoubleValue ("dismissedUpdateAt", 0.0);
        return dismissedAt > 0.0 && (currentTimeSeconds() - dismissedAt) < (48.0 * 60.0 * 60.0);
    }

    osci::licensing::ReleaseTrack selectedReleaseTrack() const
    {
        if (processor.getGlobalBoolValue ("betaUpdatesEnabled")
            && processor.getGlobalStringValue ("releaseTrack", "stable") == "beta")
            return osci::licensing::ReleaseTrack::Beta;

        return osci::licensing::ReleaseTrack::Stable;
    }

    juce::String desiredUpdateVariant() const
    {
        if (processor.getLicenseManager().hasPremium())
            return "premium";

        return compiledVariant();
    }

    static juce::String compiledVariant()
    {
#if OSCI_PREMIUM
        return "premium";
#else
        return "free";
#endif
    }

    static double currentTimeSeconds()
    {
        return static_cast<double> (juce::Time::currentTimeMillis()) / 1000.0;
    }

    void refreshParentLayout()
    {
        if (auto* parent = getParentComponent())
            parent->resized();
        else
            resized();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpdatePromptComponent)
};
