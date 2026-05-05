#pragma once

#include <JuceHeader.h>
#include <melatonin_blur/melatonin_blur.h>
#include "../CommonPluginProcessor.h"
#include "InstallFlowHelpers.h"
#include "../LookAndFeel.h"

class UpdatePromptComponent : public juce::Component {
public:
    explicit UpdatePromptComponent (CommonAudioProcessor& processorToUse)
        : processor (processorToUse) {
        setVisible (false);
        setAlwaysOnTop (true);

        titleLabel.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        titleLabel.setJustificationType (juce::Justification::centredLeft);

        detailLabel.setFont (juce::Font (juce::FontOptions (14.5f, juce::Font::plain)));
        detailLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.78f));
        detailLabel.setJustificationType (juce::Justification::centredLeft);
        detailLabel.setMinimumHorizontalScale (0.75f);

        releaseNotesLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        releaseNotesLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.88f));
        releaseNotesLabel.setText ("Release notes", juce::dontSendNotification);

        releaseNotesEditor.setReadOnly (true);
        releaseNotesEditor.setMultiLine (true);
        releaseNotesEditor.setReturnKeyStartsNewLine (false);
        releaseNotesEditor.setScrollbarsShown (true);
        releaseNotesEditor.setCaretVisible (false);
        releaseNotesEditor.setPopupMenuEnabled (false);
        releaseNotesEditor.setFont (juce::Font (juce::FontOptions (13.5f, juce::Font::plain)));
        releaseNotesEditor.setIndents (10, 8);
        releaseNotesEditor.setColour (juce::TextEditor::backgroundColourId, osci::Colours::veryDark().brighter (0.045f));
        releaseNotesEditor.setColour (juce::TextEditor::textColourId, juce::Colours::white.withAlpha (0.82f));
        releaseNotesEditor.setColour (juce::TextEditor::outlineColourId, osci::Colours::grey().withAlpha (0.26f));
        releaseNotesEditor.setColour (juce::TextEditor::focusedOutlineColourId, osci::Colours::grey().withAlpha (0.26f));

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
        addAndMakeVisible (releaseNotesLabel);
        addAndMakeVisible (releaseNotesEditor);
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

    int getPreferredWidth() const noexcept { return 560; }

    int getPreferredHeight() const noexcept {
        if (mode == Mode::Available && releaseNotesEditor.isVisible()) {
            return 250;
        }

        if (mode == Mode::Downloading) {
            return 178;
        }

        return 150;
    }

    void paint (juce::Graphics& g) override {
        const auto bounds = getCardBounds().toFloat();

        juce::Path shadowPath;
        shadowPath.addRoundedRectangle (bounds, cornerRadius);
        shadow.render (g, shadowPath);

        g.setColour (osci::Colours::veryDark().withAlpha (0.985f));
        g.fillRoundedRectangle (bounds, cornerRadius);
        g.setColour (osci::Colours::accentColor().withAlpha (0.72f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.2f);
    }

    void resized() override {
        auto area = getCardBounds().reduced (28, 22);
        titleLabel.setBounds (area.removeFromTop (24));
        area.removeFromTop (8);
        detailLabel.setBounds (area.removeFromTop (24));
        area.removeFromTop (12);

        if (progressBar.isVisible()) {
            progressBar.setBounds (area.removeFromTop (34));
            area.removeFromTop (16);
        } else if (releaseNotesEditor.isVisible()) {
            releaseNotesLabel.setBounds (area.removeFromTop (18));
            area.removeFromTop (6);
            releaseNotesEditor.setBounds (area.removeFromTop (70));
            area.removeFromTop (14);
        } else {
            releaseNotesLabel.setBounds ({});
            releaseNotesEditor.setBounds ({});
        }

        auto buttonRow = area.removeFromBottom (40);
        if (secondaryButton.isVisible()) {
            secondaryButton.setBounds (buttonRow.removeFromRight (112));
            buttonRow.removeFromRight (12);
        } else {
            secondaryButton.setBounds ({});
        }
        primaryButton.setBounds (buttonRow.removeFromRight (128));
    }

private:
    class UpdateProgressBar final : public juce::Component {
    public:
        void setProgress (double newProgress) {
            progress = juce::jlimit (0.0, 1.0, newProgress);
            repaint();
        }

        void paint (juce::Graphics& g) override {
            const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
            const auto fillWidth = bounds.getWidth() * static_cast<float>(progress);

            g.setColour (osci::Colours::veryDark().brighter (0.12f));
            g.fillRoundedRectangle (bounds, 8.0f);

            if (fillWidth > 1.0f) {
                g.setColour (osci::Colours::accentColor().withAlpha (0.92f));
                g.fillRoundedRectangle (bounds.withWidth (fillWidth), 8.0f);
            }

            g.setColour (juce::Colours::white.withAlpha (0.92f));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (juce::String (juce::roundToInt (progress * 100.0)) + "%",
                        getLocalBounds(),
                        juce::Justification::centred);
        }

    private:
        double progress = 0.0;
    };

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
    juce::Label releaseNotesLabel;
    juce::TextEditor releaseNotesEditor;
    juce::TextButton primaryButton { "Update" };
    juce::TextButton secondaryButton { "Later" };
    UpdateProgressBar progressBar;
    std::optional<osci::VersionInfo> availableVersion;
    std::optional<osci::PendingInstallMarker> pendingInstallRetryMarker;
    juce::File downloadedFile;
    Mode mode = Mode::Hidden;
    bool busy = false;
    melatonin::DropShadow shadow {
        { juce::Colours::black.withAlpha (0.62f), 20, { 0, 8 }, 0 },
        { juce::Colours::black.withAlpha (0.42f), 8, { 0, 2 }, 0 }
    };

    static constexpr int shadowPadding = 16;
    static constexpr float cornerRadius = 10.0f;

    juce::Rectangle<int> getCardBounds() const {
        return getLocalBounds().reduced (shadowPadding);
    }

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
        setReleaseNotes (version.notesMarkdown);
        titleLabel.setText ("Update available", juce::dontSendNotification);
        detailLabel.setText ("Version " + version.semver + " is ready to install.", juce::dontSendNotification);
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
        clearReleaseNotes();
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
        progressBar.setProgress (0.0);
        progressBar.setVisible (true);
        clearReleaseNotes();
        titleLabel.setText ("Downloading update", juce::dontSendNotification);
        detailLabel.setText ("Preparing a verified installer...", juce::dontSendNotification);
        primaryButton.setEnabled (false);
        secondaryButton.setEnabled (false);
        refreshParentLayout();

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
                        safeThis->progressBar.setProgress (fraction);
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
                safeThis->progressBar.setProgress (1.0);
                safeThis->progressBar.setVisible (false);
                safeThis->clearReleaseNotes();
                safeThis->titleLabel.setText ("Update ready", juce::dontSendNotification);
                safeThis->detailLabel.setText ("The verified installer has downloaded.", juce::dontSendNotification);
                safeThis->primaryButton.setButtonText ("Install");
                safeThis->secondaryButton.setButtonText ("Later");
                safeThis->secondaryButton.setVisible (true);
                safeThis->primaryButton.setEnabled (true);
                safeThis->secondaryButton.setEnabled (true);
                safeThis->refreshParentLayout();
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
                if (!osci::launchInstallerWithPendingMarker(file, version, product, currentVersion, safeThis.getComponent())) {
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
        clearReleaseNotes();
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
        clearReleaseNotes();
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

    void setReleaseNotes (juce::String notesMarkdown) {
        const auto notes = formatReleaseNotes (std::move (notesMarkdown));
        const auto hasNotes = notes.isNotEmpty();
        releaseNotesLabel.setVisible (hasNotes);
        releaseNotesEditor.setVisible (hasNotes);
        releaseNotesEditor.setText (notes, false);
        releaseNotesEditor.moveCaretToTop (false);
    }

    void clearReleaseNotes() {
        releaseNotesLabel.setVisible (false);
        releaseNotesEditor.setVisible (false);
        releaseNotesEditor.clear();
    }

    static juce::String formatReleaseNotes (juce::String notesMarkdown) {
        notesMarkdown = notesMarkdown.trim();
        if (notesMarkdown.isEmpty()) {
            return {};
        }

        juce::StringArray lines;
        lines.addLines (notesMarkdown);

        juce::StringArray formatted;
        for (auto line : lines) {
            line = line.trim();

            while (line.startsWithChar ('#')) {
                line = line.substring (1).trimStart();
            }

            line = line.replace ("**", "")
                       .replace ("`", "");

            if (line.startsWith ("* ")) {
                line = "- " + line.substring (2).trimStart();
            }

            if (line.isNotEmpty()) {
                formatted.add (line);
            }
        }

        return formatted.joinIntoString ("\n");
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
