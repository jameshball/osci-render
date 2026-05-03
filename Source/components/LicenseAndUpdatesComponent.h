#pragma once

#include <JuceHeader.h>
#include "../CommonPluginEditor.h"
#include "../CommonPluginProcessor.h"
#include "DownloadProgressComponent.h"
#include "OverlayComponent.h"

class LicenseAndUpdatesComponent : public OverlayComponent
{
public:
    explicit LicenseAndUpdatesComponent (CommonAudioProcessor& processorToUse)
        : processor (processorToUse)
    {
        setOverlayTitle (requiresPremiumLicense() ? juce::String() : "Account");
        // Premium-required builds must keep the user blocked until they
        // activate; free builds never reach this branch (see
        // requiresPremiumLicense()), so dismissibility is effectively gated
        // on whether a premium licence is currently missing.
        setDismissible (! requiresPremiumLicense());

        configureLabel (statusLabel, juce::Font (juce::FontOptions (16.0f, juce::Font::bold)), juce::Justification::centredLeft);
        configureLabel (accountLabel, juce::Font (juce::FontOptions (13.0f)), juce::Justification::centredLeft);
        configureLabel (updatesLabel, juce::Font (juce::FontOptions (13.0f)), juce::Justification::centredLeft);
        configureLabel (messageLabel, juce::Font (juce::FontOptions (13.0f)), juce::Justification::centredLeft);

        accountLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.72f));
        updatesLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.72f));
        messageLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.72f));

        licenseKeyEditor.setTextToShowWhenEmpty ("License key", juce::Colours::white.withAlpha (0.45f));
        licenseKeyEditor.setMultiLine (false);
        licenseKeyEditor.setReturnKeyStartsNewLine (false);
        licenseKeyEditor.setFont (juce::Font (juce::FontOptions (20.0f)));
        licenseKeyEditor.setColour (juce::TextEditor::backgroundColourId, Colours::dark().brighter (0.08f));
        licenseKeyEditor.setColour (juce::TextEditor::textColourId, juce::Colours::white);
        licenseKeyEditor.setColour (juce::TextEditor::outlineColourId, Colours::accentColor().withAlpha (0.4f));
        licenseKeyEditor.onReturnKey = [this] { activateLicense(); };

        activateButton.onClick = [this] { activateLicense(); };
        deactivateButton.onClick = [this] { deactivateLicense(); };
        stableUpdatesButton.onClick = [this] { disableBetaUpdates(); };
        checkButton.onClick = [this] { checkForUpdates(); };
        updateButton.onClick = [this] { downloadAndInstallUpdate(); };
        freeVersionButton.onClick = [this] { installLatestFreeVersion(); };

        stylePrimaryButton (activateButton);
        stylePrimaryButton (updateButton);
        styleSecondaryButton (checkButton);
        styleSecondaryButton (stableUpdatesButton);
        styleSecondaryButton (deactivateButton);
        styleSecondaryButton (freeVersionButton);

        juce::Component* components[] = { &statusLabel, &accountLabel, &updatesLabel, &messageLabel,
                  &licenseKeyEditor, &activateButton, &deactivateButton, &stableUpdatesButton, &checkButton,
                  &updateButton, &freeVersionButton };
        for (auto* component : components)
            addAndMakeVisible (component);

        addChildComponent (downloadProgress);

        refreshState();
    }

private:
    CommonAudioProcessor& processor;

    juce::Label statusLabel;
    juce::Label accountLabel;
    juce::Label updatesLabel;
    juce::Label messageLabel;
    juce::TextEditor licenseKeyEditor;
    juce::TextButton activateButton { "Activate" };
    juce::TextButton deactivateButton { "Remove license" };
    juce::TextButton stableUpdatesButton { "Use stable updates" };
    juce::TextButton checkButton { "Check for update" };
    juce::TextButton updateButton { "Download and install" };
    juce::TextButton freeVersionButton { "or install free version" };

    std::optional<osci::licensing::VersionInfo> availableVersion;
    juce::File downloadedFile;
    bool busy = false;
    DownloadProgressComponent downloadProgress;

    juce::Point<int> getPreferredPanelSize() const override
    {
        return requiresPremiumLicense() ? juce::Point<int> { 520, 230 } : juce::Point<int> { 560, 360 };
    }

    void resizeContent (juce::Rectangle<int> area) override
    {
        if (requiresPremiumLicense())
        {
            statusLabel.setJustificationType (juce::Justification::centred);
            statusLabel.setBounds (area.removeFromTop (30));
            accountLabel.setBounds ({ });
            updatesLabel.setBounds ({ });
            area.removeFromTop (6);

            messageLabel.setJustificationType (juce::Justification::centred);
            messageLabel.setBounds (area.removeFromTop (24));
            area.removeFromTop (8);

            auto licenseRow = area.removeFromTop (34);
            activateButton.setBounds (licenseRow.removeFromRight (112));
            licenseRow.removeFromRight (8);
            licenseKeyEditor.setBounds (licenseRow);
            area.removeFromTop (8);

            if (downloadProgress.isVisible())
            {
                downloadProgress.setBounds (area.removeFromTop (48));
                area.removeFromTop (4);
                freeVersionButton.setBounds ({ });
            }
            else
            {
                downloadProgress.setBounds ({ });
                auto actionRow = area.removeFromTop (32);
                if (freeVersionButton.isVisible())
                    freeVersionButton.setBounds (actionRow.withSizeKeepingCentre (176, actionRow.getHeight()));
                else
                    freeVersionButton.setBounds ({ });
            }

            deactivateButton.setBounds ({ });
            checkButton.setBounds ({ });
            updateButton.setBounds ({ });
            stableUpdatesButton.setBounds ({ });
            return;
        }

        statusLabel.setJustificationType (juce::Justification::centredLeft);
        statusLabel.setBounds (area.removeFromTop (30));
        accountLabel.setBounds (area.removeFromTop (24));
        updatesLabel.setBounds (area.removeFromTop (24));
        messageLabel.setJustificationType (juce::Justification::centredLeft);
        messageLabel.setBounds (area.removeFromTop (26));
        area.removeFromTop (8);

        auto licenseRow = area.removeFromTop (38);
        if (activateButton.isVisible())
        {
            activateButton.setBounds (licenseRow.removeFromRight (112));
            licenseRow.removeFromRight (8);
        }
        else
        {
            activateButton.setBounds ({ });
        }
        licenseKeyEditor.setBounds (licenseRow);
        area.removeFromTop (10);

        if (downloadProgress.isVisible())
        {
            downloadProgress.setBounds (area.removeFromTop (48));
            checkButton.setBounds ({ });
            updateButton.setBounds ({ });
            freeVersionButton.setBounds ({ });
            stableUpdatesButton.setBounds ({ });
        }
        else
        {
            downloadProgress.setBounds ({ });
            auto actionRow = area.removeFromTop (32);
            if (freeVersionButton.isVisible() && ! checkButton.isVisible() && ! updateButton.isVisible() && ! stableUpdatesButton.isVisible())
            {
                freeVersionButton.setBounds (actionRow.withSizeKeepingCentre (176, actionRow.getHeight()));
            }
            else
            {
                layoutButton (actionRow, checkButton, 136);
                layoutButton (actionRow, updateButton, 156);
                layoutButton (actionRow, freeVersionButton, 176);
                layoutButton (actionRow, stableUpdatesButton, 144);
            }
        }

        auto bottomRow = area.removeFromBottom (32);
        layoutButton (bottomRow, deactivateButton, 132);
    }

    void refreshState()
    {
        const auto state = processor.getLicenseManager().getStateForUi();
        const auto premium = static_cast<bool> (state.getProperty ("premium"));
        const auto premiumRequired = requiresPremiumLicense();
        const auto betaEnabled = processor.getGlobalBoolValue ("betaUpdatesEnabled")
                              && processor.getGlobalStringValue ("releaseTrack", "stable") == "beta";
        auto status = premiumRequired ? juce::String ("Enter your premium license")
                                      : (premium ? juce::String ("Premium active") : juce::String ("Free version"));

        const auto email = state.getProperty ("email").toString();
        const auto expiresAt = state.getProperty ("expires_at").toString();
        auto accountText = premiumRequired ? juce::String ("This premium build needs activation")
                                           : (email.isNotEmpty() ? email : juce::String ("No license key activated"));
        if (expiresAt.isNotEmpty())
            accountText << " - expires " << expiresAt;

        statusLabel.setText (status, juce::dontSendNotification);
        statusLabel.setJustificationType (premiumRequired ? juce::Justification::centred : juce::Justification::centredLeft);
        accountLabel.setText (accountText, juce::dontSendNotification);
        accountLabel.setVisible (! premiumRequired);
        updatesLabel.setText (betaEnabled ? "Beta updates enabled" : "Stable updates", juce::dontSendNotification);
        updatesLabel.setVisible (! premiumRequired);
        stableUpdatesButton.setVisible (betaEnabled);
        setOverlayTitle (premiumRequired ? juce::String() : "Account");
        setDismissible (! premiumRequired);

        activateButton.setVisible (true);
        deactivateButton.setVisible (! premiumRequired && state.getProperty ("status").toString() != "free");
        stableUpdatesButton.setVisible (! premiumRequired && betaEnabled);
        checkButton.setVisible (! premiumRequired);
        updateButton.setVisible (! premiumRequired && availableVersion.has_value());
        freeVersionButton.setVisible (premiumRequired && hasFreeFallback());

        activateButton.setEnabled (! busy);
        deactivateButton.setEnabled (! busy && deactivateButton.isVisible());
        stableUpdatesButton.setEnabled (! busy && stableUpdatesButton.isVisible());
        checkButton.setEnabled (! busy && checkButton.isVisible());
        updateButton.setEnabled (! busy && updateButton.isVisible());
        freeVersionButton.setEnabled (! busy && freeVersionButton.isVisible());
        licenseKeyEditor.setEnabled (! busy);
        resized();
    }

    static void layoutButton (juce::Rectangle<int>& row, juce::TextButton& button, int width)
    {
        if (! button.isVisible())
        {
            button.setBounds ({ });
            return;
        }

        button.setBounds (row.removeFromLeft (juce::jmin (width, row.getWidth())));
        row.removeFromLeft (10);
    }

    bool requiresPremiumLicense() const
    {
#if OSCI_PREMIUM
        return ! processor.getLicenseManager().hasPremium();
#else
        return false;
#endif
    }

    bool hasFreeFallback() const
    {
        return processor.getProductSlug() == "osci-render";
    }

    static void stylePrimaryButton (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, Colours::accentColor());
        button.setColour (juce::TextButton::buttonOnColourId, Colours::accentColor().brighter (0.12f));
        button.setColour (juce::TextButton::textColourOffId, Colours::veryDark());
        button.setColour (juce::TextButton::textColourOnId, Colours::veryDark());
    }

    static void styleSecondaryButton (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, Colours::darker());
        button.setColour (juce::TextButton::buttonOnColourId, Colours::dark().brighter (0.08f));
        button.setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.88f));
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }

    void setBusy (bool shouldBeBusy, juce::StringRef text = {})
    {
        busy = shouldBeBusy;
        if (text.isNotEmpty())
            messageLabel.setText (text, juce::dontSendNotification);
        refreshState();
    }

    void runAsync (juce::String busyText,
                   std::function<juce::Result()> work,
                   std::function<void()> onSuccess = {})
    {
        if (busy)
            return;

        setBusy (true, busyText);
        auto result = std::make_shared<juce::Result> (juce::Result::ok());
        auto safeThis = juce::Component::SafePointer<LicenseAndUpdatesComponent> (this);

        juce::Thread::launch ([safeThis, result, work = std::move (work), onSuccess = std::move (onSuccess)] () mutable
        {
            *result = work();

            juce::MessageManager::callAsync ([safeThis, result, onSuccess = std::move (onSuccess)] () mutable
            {
                if (safeThis == nullptr)
                    return;

                // If the download progress bar was shown, always hide it when
                // the async operation completes (success path hides explicitly;
                // error path falls through here).
                safeThis->downloadProgress.setVisible (false);
                safeThis->resized();

                safeThis->setBusy (false);
                if (result->failed())
                    safeThis->messageLabel.setText (result->getErrorMessage(), juce::dontSendNotification);
                else if (onSuccess)
                    onSuccess();

                safeThis->refreshState();
            });
        });
    }

    void activateLicense()
    {
        const auto key = licenseKeyEditor.getText().trim();
        if (key.isEmpty())
        {
            messageLabel.setText ("Enter a license key.", juce::dontSendNotification);
            return;
        }

        // The processor (and thus its LicenseManager) outlives any editor
        // instance, so capturing by reference is safe even if the editor is
        // destroyed mid-activation.
        auto& manager = processor.getLicenseManager();
        runAsync ("Activating license...",
                  [&manager, key] { return manager.activate (key); },
                  [this]
                  {
                      licenseKeyEditor.clear();
                      messageLabel.setText ("License activated.", juce::dontSendNotification);
                  });
    }

    void deactivateLicense()
    {
        processor.getLicenseManager().deactivate();
        availableVersion.reset();
        downloadedFile = juce::File();
        messageLabel.setText ("License deactivated.", juce::dontSendNotification);
        refreshState();
    }

    void disableBetaUpdates()
    {
        processor.setGlobalValue ("betaUpdatesEnabled", false);
        processor.setGlobalValue ("releaseTrack", "stable");
        processor.saveGlobalSettings();
        availableVersion.reset();
        downloadedFile = juce::File();
        messageLabel.setText ("Stable updates enabled.", juce::dontSendNotification);

        if (auto* editor = findParentComponentOfClass<CommonPluginEditor>())
        {
            editor->refreshBetaUpdatesButton();
            editor->resized();
        }

        refreshState();
        resized();
    }

    void checkForUpdates()
    {
        auto foundVersion = std::make_shared<std::optional<osci::licensing::VersionInfo>>();
        const auto product = processor.getProductSlug();
        const auto currentVersion = juce::String (JucePlugin_VersionString);
        const auto track = selectedReleaseTrack();
        const auto variant = desiredUpdateVariant();

        runAsync ("Checking for updates...",
                  [foundVersion, product, currentVersion, track, variant]
                  {
                      osci::licensing::UpdateChecker checker;
                      *foundVersion = checker.checkForUpdate (product, currentVersion, track, variant);
                      return checker.getLastResult();
                  },
                  [this, foundVersion]
                  {
                      availableVersion = *foundVersion;
                      downloadedFile = juce::File();
                      if (availableVersion.has_value())
                      {
                          messageLabel.setText ("Version " + availableVersion->semver + " is available.", juce::dontSendNotification);
                      }
                      else
                      {
                          messageLabel.setText ("No update available.", juce::dontSendNotification);
                      }
                  });
    }

    void downloadAndInstallUpdate()
    {
        if (! availableVersion.has_value())
            return;

        downloadAndInstallVersion (*availableVersion, processor.getLicenseManager().getCachedToken());
    }

    void installLatestFreeVersion()
    {
        auto foundVersion = std::make_shared<std::optional<osci::licensing::VersionInfo>>();
        const auto product = processor.getProductSlug();

        runAsync ("Finding free version...",
                  [foundVersion, product]
                  {
                      osci::licensing::UpdateChecker checker;
                      *foundVersion = checker.checkForUpdate (product, "0.0.0.0", osci::licensing::ReleaseTrack::Stable, "free");
                      return checker.getLastResult();
                  },
                  [this, foundVersion]
                  {
                      if (! foundVersion->has_value())
                      {
                          messageLabel.setText ("No free download is available.", juce::dontSendNotification);
                          return;
                      }

                      downloadAndInstallVersion (**foundVersion, {});
                  });
    }

    void downloadAndInstallVersion (const osci::licensing::VersionInfo& versionToInstall,
                                    juce::StringRef licenseToken)
    {
        if (busy)
            return;

        auto downloaded = std::make_shared<juce::File>();
        const auto version = versionToInstall;
        const auto token = juce::String (licenseToken);
        const auto product = processor.getProductSlug();

        // Show progress bar immediately (before worker starts).
        downloadProgress.reset();
        downloadProgress.setStatus ("Downloading " + version.semver + "...");
        downloadProgress.setProgress (-1.0);
        downloadProgress.setVisible (true);
        resized();

        auto safeThis = juce::Component::SafePointer<LicenseAndUpdatesComponent> (this);

        runAsync ("Downloading update...",
                  [downloaded, version, token, product, safeThis]
                  {
                      osci::licensing::Downloader::Config config;
                      config.downloadDirectory = osci::licensing::HardwareInfo::getDefaultStorageDirectory (product).getChildFile ("downloads");
                      osci::licensing::Downloader downloader (config);
                      auto result = downloader.downloadAndVerify (
                          version, token,
                          [safeThis] (double fraction, juce::int64)
                          {
                              if (safeThis != nullptr)
                                  safeThis->downloadProgress.setProgress (fraction);
                          });
                      if (result.wasOk())
                          *downloaded = downloader.getDownloadedFile();
                      return result;
                  },
                  [this, downloaded]
                  {
                      downloadProgress.setVisible (false);
                      resized();
                      downloadedFile = *downloaded;
                      messageLabel.setText ("Update downloaded and verified.", juce::dontSendNotification);
                      installUpdate();
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
            "Save your work before continuing. The installer may need to close the host application.",
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

    osci::licensing::ReleaseTrack selectedReleaseTrack() const
    {
        if (processor.getGlobalBoolValue ("betaUpdatesEnabled")
            && processor.getGlobalStringValue ("releaseTrack", "stable") == "beta")
            return osci::licensing::ReleaseTrack::Beta;

        return osci::licensing::ReleaseTrack::Stable;
    }

    static juce::String compiledVariant()
    {
#if OSCI_PREMIUM
        return "premium";
#else
        return "free";
#endif
    }

    juce::String desiredUpdateVariant() const
    {
        if (processor.getLicenseManager().hasPremium())
            return "premium";

        return compiledVariant();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseAndUpdatesComponent)
};
