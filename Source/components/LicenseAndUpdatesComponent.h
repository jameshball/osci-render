#pragma once

#include <JuceHeader.h>
#include "../CommonPluginProcessor.h"
#include "OverlayComponent.h"

class LicenseAndUpdatesComponent : public OverlayComponent
{
public:
    explicit LicenseAndUpdatesComponent (CommonAudioProcessor& processorToUse)
        : processor (processorToUse)
    {
        configureLabel (titleLabel, juce::Font (22.0f, juce::Font::bold), juce::Justification::centredLeft);
        configureLabel (statusLabel, juce::Font (14.0f), juce::Justification::centredLeft);
        configureLabel (messageLabel, juce::Font (13.0f), juce::Justification::centredLeft);
        configureLabel (trackLabel, juce::Font (13.0f), juce::Justification::centredLeft);

        titleLabel.setText ("License and Updates", juce::dontSendNotification);
        trackLabel.setText ("Release track", juce::dontSendNotification);
        messageLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.72f));

        licenseKeyEditor.setTextToShowWhenEmpty ("License key", juce::Colours::white.withAlpha (0.45f));
        licenseKeyEditor.setMultiLine (false);
        licenseKeyEditor.setReturnKeyStartsNewLine (false);
        licenseKeyEditor.setColour (juce::TextEditor::backgroundColourId, Colours::dark().brighter (0.08f));
        licenseKeyEditor.setColour (juce::TextEditor::textColourId, juce::Colours::white);
        licenseKeyEditor.setColour (juce::TextEditor::outlineColourId, Colours::accentColor().withAlpha (0.4f));
        licenseKeyEditor.onReturnKey = [this] { activateLicense(); };

        releaseTrackBox.addItem ("Stable", 1);
        releaseTrackBox.addItem ("Beta", 2);
        releaseTrackBox.addItem ("Alpha", 3);
        releaseTrackBox.setSelectedId (releaseTrackIdForString (processor.getGlobalStringValue ("releaseTrack", "stable")), juce::dontSendNotification);
        releaseTrackBox.onChange = [this]
        {
            processor.setGlobalValue ("releaseTrack", releaseTrackString());
            processor.saveGlobalSettings();
        };

        activateButton.onClick = [this] { activateLicense(); };
        deactivateButton.onClick = [this] { deactivateLicense(); };
        checkButton.onClick = [this] { checkForUpdates(); };
        downloadButton.onClick = [this] { downloadUpdate(); };
        installButton.onClick = [this] { installUpdate(); };
        closeButton.onClick = [this] { dismiss(); };

        juce::Component* components[] = { &titleLabel, &statusLabel, &messageLabel,
                          &trackLabel, &licenseKeyEditor, &releaseTrackBox,
                          &activateButton, &deactivateButton, &checkButton,
                          &downloadButton, &installButton, &closeButton };
        for (auto* component : components)
            addAndMakeVisible (component);

        refreshState();
    }

private:
    CommonAudioProcessor& processor;

    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::Label messageLabel;
    juce::Label trackLabel;
    juce::TextEditor licenseKeyEditor;
    juce::ComboBox releaseTrackBox;
    juce::TextButton activateButton { "Activate" };
    juce::TextButton deactivateButton { "Deactivate" };
    juce::TextButton checkButton { "Check" };
    juce::TextButton downloadButton { "Download" };
    juce::TextButton installButton { "Install" };
    juce::TextButton closeButton { "Close" };

    std::optional<osci::licensing::VersionInfo> availableVersion;
    juce::File downloadedFile;
    bool busy = false;

    void resizeContent (juce::Rectangle<int> area) override
    {
        titleLabel.setBounds (area.removeFromTop (32));
        area.removeFromTop (8);
        statusLabel.setBounds (area.removeFromTop (28));
        messageLabel.setBounds (area.removeFromTop (28));
        area.removeFromTop (14);

        auto licenseRow = area.removeFromTop (34);
        activateButton.setBounds (licenseRow.removeFromRight (104));
        licenseRow.removeFromRight (8);
        licenseKeyEditor.setBounds (licenseRow);
        area.removeFromTop (10);

        auto actionRow = area.removeFromTop (32);
        deactivateButton.setBounds (actionRow.removeFromLeft (112));
        actionRow.removeFromLeft (10);
        trackLabel.setBounds (actionRow.removeFromLeft (92));
        releaseTrackBox.setBounds (actionRow.removeFromLeft (120));
        actionRow.removeFromLeft (10);
        checkButton.setBounds (actionRow.removeFromLeft (96));
        actionRow.removeFromLeft (10);
        downloadButton.setBounds (actionRow.removeFromLeft (104));
        actionRow.removeFromLeft (10);
        installButton.setBounds (actionRow.removeFromLeft (96));

        closeButton.setBounds (area.removeFromBottom (32).removeFromRight (92));
    }

    void refreshState()
    {
        const auto state = processor.getLicenseManager().getStateForUi();
        const auto premium = static_cast<bool> (state.getProperty ("premium"));
        auto status = premium ? juce::String ("Premium active") : juce::String ("Free mode");

        const auto email = state.getProperty ("email").toString();
        const auto expiresAt = state.getProperty ("expires_at").toString();
        if (email.isNotEmpty())
            status << " - " << email;
        if (expiresAt.isNotEmpty())
            status << " - expires " << expiresAt;

        statusLabel.setText (status, juce::dontSendNotification);

        activateButton.setEnabled (! busy);
        deactivateButton.setEnabled (! busy && state.getProperty ("status").toString() != "free");
        checkButton.setEnabled (! busy);
        downloadButton.setEnabled (! busy && availableVersion.has_value());
        installButton.setEnabled (! busy && downloadedFile.existsAsFile());
        releaseTrackBox.setEnabled (! busy);
        licenseKeyEditor.setEnabled (! busy);
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

        auto* manager = &processor.getLicenseManager();
        runAsync ("Activating license...",
                  [manager, key] { return manager->activate (key); },
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

    void checkForUpdates()
    {
        auto foundVersion = std::make_shared<std::optional<osci::licensing::VersionInfo>>();
        const auto product = processor.getProductSlug();
        const auto currentVersion = juce::String (JucePlugin_VersionString);
        const auto track = selectedReleaseTrack();
        const auto variant = processor.getLicenseManager().hasPremium() ? juce::String ("premium") : juce::String ("free");

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

    void downloadUpdate()
    {
        if (! availableVersion.has_value())
            return;

        auto downloaded = std::make_shared<juce::File>();
        const auto version = *availableVersion;
        const auto payload = processor.getLicenseManager().getPayload();
        const auto licenseKey = payload.has_value() ? payload->licenseKey : juce::String();
        const auto product = processor.getProductSlug();

        runAsync ("Downloading update...",
                  [downloaded, version, licenseKey, product]
                  {
                      osci::licensing::Downloader::Config config;
                      config.downloadDirectory = osci::licensing::HardwareInfo::getDefaultStorageDirectory (product).getChildFile ("downloads");
                      osci::licensing::Downloader downloader (config);
                      auto result = downloader.downloadAndVerify (version, licenseKey);
                      if (result.wasOk())
                          *downloaded = downloader.getDownloadedFile();
                      return result;
                  },
                  [this, downloaded]
                  {
                      downloadedFile = *downloaded;
                      messageLabel.setText ("Update downloaded and verified.", juce::dontSendNotification);
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
        switch (releaseTrackBox.getSelectedId())
        {
            case 2: return osci::licensing::ReleaseTrack::Beta;
            case 3: return osci::licensing::ReleaseTrack::Alpha;
            default: return osci::licensing::ReleaseTrack::Stable;
        }
    }

    juce::String releaseTrackString() const
    {
        switch (selectedReleaseTrack())
        {
            case osci::licensing::ReleaseTrack::Alpha: return "alpha";
            case osci::licensing::ReleaseTrack::Beta: return "beta";
            case osci::licensing::ReleaseTrack::Stable: return "stable";
        }

        return "stable";
    }

    static int releaseTrackIdForString (juce::StringRef value)
    {
        const auto text = juce::String (value).toLowerCase();
        if (text == "beta")
            return 2;
        if (text == "alpha")
            return 3;
        return 1;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseAndUpdatesComponent)
};
