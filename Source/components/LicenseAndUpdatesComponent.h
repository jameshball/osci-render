#pragma once

#include <JuceHeader.h>
#include "../CommonPluginEditor.h"
#include "../CommonPluginProcessor.h"
#include "DownloadProgressComponent.h"
#include "InstallFlowHelpers.h"

class LicenseAndUpdatesComponent : public osci::OverlayComponent {
public:
    explicit LicenseAndUpdatesComponent (CommonAudioProcessor& processorToUse)
        : osci::OverlayComponent (juce::String::createStringFromData (BinaryData::close_svg, BinaryData::close_svgSize)),
          processor (processorToUse),
          licenseCard (loadProductIcon(),
                       juce::String::createStringFromData (BinaryData::copy_svg, BinaryData::copy_svgSize),
                       juce::String::createStringFromData (BinaryData::eye_svg, BinaryData::eye_svgSize),
                       juce::String::createStringFromData (BinaryData::eyeoff_svg, BinaryData::eyeoff_svgSize)),
          helpButton ("accountHelp",
                      juce::String::createStringFromData (BinaryData::help_svg, BinaryData::help_svgSize),
                      juce::Colours::white) {
        setOverlayTitle (requiresPremiumLicense() ? juce::String() : "Account");
        setDismissible(!requiresPremiumLicense());

        licenseCard.onActivate = [this] {
            activateLicense();
        };
        licenseCard.onCopyLicenseKey = [this] {
            copyLicenseKey();
        };
        licenseCard.onToggleReveal = [this] {
            licenseKeyRevealed = !licenseKeyRevealed;
            refreshState();
        };
        licenseCard.onInstallFreeVersion = [this] {
            installLatestFreeVersion();
        };
        addPanelContentAndMakeVisible (licenseCard);

        updateCard.onCheckForUpdates = [this] {
            checkForUpdates();
        };
        updateCard.onDownloadAndInstall = [this] {
            downloadAndInstallUpdate();
        };
        updateCard.onUseStableUpdates = [this] {
            disableBetaUpdates();
        };
        addPanelContentAndMakeVisible (updateCard);

        removeLicenseButton.setButtonText ("Remove license");
        removeLicenseButton.onClick = [this] {
            confirmDeactivateLicense();
        };
        styleDangerButton (removeLicenseButton);
        addPanelContentAndMakeVisible (removeLicenseButton);

        helpButton.setTooltip ("Help");
        helpButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        helpButton.onClick = [this] {
            showLicenseHelpOverlay();
        };
        addPanelControlAndMakeVisible (helpButton);

        refreshState();
    }

private:
    enum class NoticeKind {
        None,
        Info,
        Success,
        Warning,
        Error,
    };

    enum class NoticeTarget {
        License,
        Update,
    };

    struct Notice {
        juce::String text;
        NoticeKind kind = NoticeKind::None;
    };

    static void configureLabel (juce::Label& label,
                                const juce::Font& font,
                                juce::Justification justification) {
        label.setColour (juce::Label::textColourId, juce::Colours::white);
        label.setFont (font);
        label.setJustificationType (justification);
        label.setEditable (false, false, false);
        label.setInterceptsMouseClicks (false, false);
    }

    static juce::Colour noticeColour (NoticeKind kind) {
        switch (kind) {
            case NoticeKind::Success: return osci::Colours::accentColor();
            case NoticeKind::Warning: return juce::Colours::orange.withAlpha (0.92f);
            case NoticeKind::Error: return juce::Colours::red.brighter (0.42f);
            case NoticeKind::Info: return juce::Colours::white.withAlpha (0.68f);
            case NoticeKind::None: break;
        }

        return juce::Colours::white.withAlpha (0.68f);
    }

    static void drawCardBackground (juce::Graphics& g, juce::Rectangle<int> bounds) {
        const auto card = bounds.toFloat();
        juce::Path shadowPath;
        shadowPath.addRoundedRectangle (card, 8.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.22f), 14, { 0, 4 }).drawForPath (g, shadowPath);

        g.setColour (osci::Colours::veryDark().brighter (0.035f));
        g.fillRoundedRectangle (card, 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.11f));
        g.drawRoundedRectangle (card.reduced (0.5f), 8.0f, 1.0f);
    }

    static void stylePrimaryButton (juce::TextButton& button) {
        button.setColour (juce::TextButton::buttonColourId, osci::Colours::accentColor());
        button.setColour (juce::TextButton::buttonOnColourId, osci::Colours::accentColor().brighter (0.12f));
        button.setColour (juce::TextButton::textColourOffId, osci::Colours::veryDark());
        button.setColour (juce::TextButton::textColourOnId, osci::Colours::veryDark());
    }

    static void styleSecondaryButton (juce::TextButton& button) {
        button.setColour (juce::TextButton::buttonColourId, osci::Colours::darker());
        button.setColour (juce::TextButton::buttonOnColourId, osci::Colours::dark().brighter (0.08f));
        button.setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.88f));
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }

    class LicenseProfileCard final : public juce::Component {
    public:
        LicenseProfileCard (juce::Image productImageToUse,
                            juce::String copySvg,
                            juce::String eyeSvg,
                            juce::String eyeOffSvg)
            : productImage (std::move (productImageToUse)),
              copyButton ("copyLicenseKey", std::move (copySvg), juce::Colours::white.withAlpha (0.76f)),
              revealButton ("revealLicenseKey",
                            std::move (eyeSvg),
                            juce::Colours::white.withAlpha (0.76f),
                            osci::Colours::accentColor(),
                            nullptr,
                            std::move (eyeOffSvg)) {
            configureLabel (productLabel, juce::Font (juce::FontOptions (16.0f)), juce::Justification::centredLeft);
            productLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.64f));
            addAndMakeVisible (productLabel);

            configureLabel (titleLabel, juce::Font (juce::FontOptions (26.0f, juce::Font::bold)), juce::Justification::centredLeft);
            addAndMakeVisible (titleLabel);

            configureLabel (detailLabel, juce::Font (juce::FontOptions (17.0f)), juce::Justification::centredLeft);
            detailLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.78f));
            detailLabel.setMinimumHorizontalScale (0.78f);
            addAndMakeVisible (detailLabel);

            configureLabel (keyCaptionLabel, juce::Font (juce::FontOptions (15.0f, juce::Font::bold)), juce::Justification::centredLeft);
            keyCaptionLabel.setText ("License key", juce::dontSendNotification);
            keyCaptionLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.58f));
            addAndMakeVisible (keyCaptionLabel);

            configureLabel (keyValueLabel, juce::Font (juce::FontOptions (16.0f)), juce::Justification::centredLeft);
            keyValueLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.88f));
            keyValueLabel.setMinimumHorizontalScale (0.68f);
            addAndMakeVisible (keyValueLabel);

            copyButton.onClick = [this] {
                if (onCopyLicenseKey) {
                    onCopyLicenseKey();
                }
            };
            addAndMakeVisible (copyButton);

            revealButton.onClick = [this] {
                if (onToggleReveal) {
                    onToggleReveal();
                }
            };
            addAndMakeVisible (revealButton);

            licenseKeyEditor.setTextToShowWhenEmpty ("License key", juce::Colours::white.withAlpha (0.45f));
            licenseKeyEditor.setMultiLine (false);
            licenseKeyEditor.setReturnKeyStartsNewLine (false);
            licenseKeyEditor.setFont (juce::Font (juce::FontOptions (20.0f)));
            licenseKeyEditor.setColour (juce::TextEditor::backgroundColourId, osci::Colours::dark().brighter (0.08f));
            licenseKeyEditor.setColour (juce::TextEditor::textColourId, juce::Colours::white);
            licenseKeyEditor.setColour (juce::TextEditor::outlineColourId, osci::Colours::grey().withAlpha (0.38f));
            licenseKeyEditor.setColour (juce::TextEditor::focusedOutlineColourId, osci::Colours::accentColor().withAlpha (0.9f));
            licenseKeyEditor.setColour (juce::CaretComponent::caretColourId, juce::Colours::white);
            licenseKeyEditor.setSelectAllWhenFocused (true);
            licenseKeyEditor.onReturnKey = [this] {
                if (onActivate) {
                    onActivate();
                }
            };
            addAndMakeVisible (licenseKeyEditor);

            activateButton.setButtonText ("Activate");
            activateButton.onClick = [this] {
                if (onActivate) {
                    onActivate();
                }
            };
            stylePrimaryButton (activateButton);
            addAndMakeVisible (activateButton);

            freeVersionButton.setButtonText ("or install free version");
            freeVersionButton.onClick = [this] {
                if (onInstallFreeVersion) {
                    onInstallFreeVersion();
                }
            };
            styleSecondaryButton (freeVersionButton);
            addAndMakeVisible (freeVersionButton);

            configureLabel (noticeLabel, juce::Font (juce::FontOptions (16.0f)), juce::Justification::centred);
            noticeLabel.setMinimumHorizontalScale (0.75f);
            addAndMakeVisible (noticeLabel);
        }

        std::function<void()> onActivate;
        std::function<void()> onCopyLicenseKey;
        std::function<void()> onToggleReveal;
        std::function<void()> onInstallFreeVersion;

        struct State {
            juce::String productName;
            juce::String title;
            juce::String detail;
            juce::String badge;
            juce::String licenseKey;
            juce::String notice;
            NoticeKind noticeKind = NoticeKind::None;
            bool revealLicenseKey = false;
            bool showActivation = false;
            bool showFreeFallback = false;
            bool drawChrome = true;
            int topRightReserve = 0;
            bool busy = false;
        };

        void setState (const State& state) {
            productLabel.setText (state.productName, juce::dontSendNotification);
            productLabel.setVisible (state.productName.isNotEmpty());
            titleLabel.setText (state.title, juce::dontSendNotification);
            detailLabel.setText (state.detail, juce::dontSendNotification);
            detailLabel.setVisible (state.detail.isNotEmpty());
            badgeText = state.badge;
            licenseKey = state.licenseKey;
            drawChrome = state.drawChrome;
            topRightReserve = state.topRightReserve;

            const auto hasLicenseKey = licenseKey.isNotEmpty();
            keyCaptionLabel.setVisible (hasLicenseKey);
            keyValueLabel.setVisible (hasLicenseKey);
            copyButton.setVisible (hasLicenseKey);
            revealButton.setVisible (hasLicenseKey);
            revealButton.setToggleState (state.revealLicenseKey, juce::dontSendNotification);
            keyValueLabel.setText (state.revealLicenseKey ? licenseKey : maskLicenseKey (licenseKey),
                                   juce::dontSendNotification);

            licenseKeyEditor.setVisible (state.showActivation);
            activateButton.setVisible (state.showActivation);
            freeVersionButton.setVisible (state.showFreeFallback);

            licenseKeyEditor.setEnabled(!state.busy && state.showActivation);
            activateButton.setEnabled(!state.busy && state.showActivation);
            freeVersionButton.setEnabled(!state.busy && state.showFreeFallback);
            copyButton.setEnabled(!state.busy && hasLicenseKey);
            revealButton.setEnabled(!state.busy && hasLicenseKey);

            noticeKind = state.noticeKind;
            noticeLabel.setText (state.notice, juce::dontSendNotification);
            noticeLabel.setVisible (state.notice.isNotEmpty());
            noticeLabel.setColour (juce::Label::textColourId, noticeColour (noticeKind));

            resized();
            repaint();
        }

        juce::String getEnteredLicenseKey() const {
            return licenseKeyEditor.getText().trim();
        }

        void clearEnteredLicenseKey() {
            licenseKeyEditor.clear();
        }

        void paint (juce::Graphics& g) override {
            if (drawChrome) {
                drawCardBackground (g, getLocalBounds());
            }

            const auto iconBounds = iconArea.toFloat();
            if (productImage.isValid()) {
                const auto iconWell = iconBounds.expanded (5.0f);
                g.setColour (osci::Colours::veryDark().brighter (0.085f));
                g.fillRoundedRectangle (iconWell, 8.0f);
                g.setColour (juce::Colours::white.withAlpha (0.10f));
                g.drawRoundedRectangle (iconWell.reduced (0.5f), 8.0f, 1.0f);
                g.setOpacity (1.0f);
                g.drawImageWithin (productImage,
                                   iconArea.getX(),
                                   iconArea.getY(),
                                   iconArea.getWidth(),
                                   iconArea.getHeight(),
                                   juce::RectanglePlacement::centred);
            } else {
                g.setColour (osci::Colours::accentColor().withAlpha (0.14f));
                g.fillRoundedRectangle (iconBounds, 8.0f);
            }

            if (badgeBounds.isEmpty() || badgeText.isEmpty()) {
                return;
            }

            g.setColour (osci::Colours::accentColor().withAlpha (0.14f));
            g.fillRoundedRectangle (badgeBounds.toFloat(), 8.0f);
            g.setColour (osci::Colours::accentColor().withAlpha (0.72f));
            g.drawRoundedRectangle (badgeBounds.toFloat().reduced (0.5f), 8.0f, 1.25f);
            g.setColour (osci::Colours::accentColor());
            g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawFittedText (badgeText, badgeBounds.reduced (12, 0), juce::Justification::centred, 1, 0.78f);
        }

        void resized() override {
            auto area = drawChrome ? getLocalBounds().reduced (24)
                                   : getLocalBounds();
            auto header = area.removeFromTop (detailLabel.isVisible() ? 94 : 82);
            iconArea = header.removeFromLeft (98).withSizeKeepingCentre (76, 76);
            header.removeFromLeft (16);

            auto titleRow = header.removeFromTop (34);
            titleRow.removeFromRight (topRightReserve);
            if (badgeText.isNotEmpty()) {
                badgeBounds = titleRow.removeFromRight (juce::jlimit (96, 146, badgeText.length() * 10 + 34));
                badgeBounds = badgeBounds.withHeight (34).withCentre (badgeBounds.getCentre());
                titleRow.removeFromRight (12);
            } else {
                badgeBounds = {};
            }
            titleLabel.setBounds (titleRow);
            if (productLabel.isVisible()) {
                productLabel.setBounds (header.removeFromTop (24));
            } else {
                productLabel.setBounds ({ });
                header.removeFromTop (4);
            }
            if (detailLabel.isVisible()) {
                detailLabel.setBounds (header.removeFromTop (28));
            } else {
                detailLabel.setBounds ({ });
            }

            area.removeFromTop (16);

            if (keyCaptionLabel.isVisible()) {
                auto keyRow = area.removeFromTop (42);
                keyCaptionLabel.setBounds (keyRow.removeFromLeft (110));
                auto iconButtons = keyRow.removeFromRight (76);
                copyButton.setBounds (iconButtons.removeFromLeft (34).withSizeKeepingCentre (24, 24));
                revealButton.setBounds (iconButtons.removeFromRight (34).withSizeKeepingCentre (24, 24));
                keyValueLabel.setBounds (keyRow);
                area.removeFromTop (12);
            } else {
                keyCaptionLabel.setBounds ({ });
                keyValueLabel.setBounds ({ });
                copyButton.setBounds ({ });
                revealButton.setBounds ({ });
            }

            if (licenseKeyEditor.isVisible() || activateButton.isVisible()) {
                auto activationRow = area.removeFromTop (38);
                activateButton.setBounds (activationRow.removeFromRight (136));
                activationRow.removeFromRight (14);
                licenseKeyEditor.setBounds (activationRow);
                area.removeFromTop (12);
            } else {
                licenseKeyEditor.setBounds ({ });
                activateButton.setBounds ({ });
            }

            if (freeVersionButton.isVisible()) {
                freeVersionButton.setBounds (area.removeFromTop (34).withSizeKeepingCentre (220, 34));
                area.removeFromTop (8);
            } else {
                freeVersionButton.setBounds ({ });
            }

            if (noticeLabel.isVisible()) {
                noticeLabel.setBounds (area.removeFromTop (28));
            } else {
                noticeLabel.setBounds ({ });
            }
        }

    private:
        juce::Image productImage;
        juce::Label productLabel;
        juce::Label titleLabel;
        juce::Label detailLabel;
        juce::Label keyCaptionLabel;
        juce::Label keyValueLabel;
        osci::SvgButton copyButton;
        osci::SvgButton revealButton;
        osci::TextEditor licenseKeyEditor;
        juce::TextButton activateButton;
        juce::TextButton freeVersionButton;
        juce::Label noticeLabel;
        juce::String badgeText;
        juce::String licenseKey;
        NoticeKind noticeKind = NoticeKind::None;
        bool drawChrome = true;
        int topRightReserve = 0;
        juce::Rectangle<int> iconArea;
        juce::Rectangle<int> badgeBounds;

        static juce::String maskLicenseKey (const juce::String& key) {
            if (key.isEmpty()) {
                return {};
            }

            if (key.length() <= 10) {
                return "****";
            }

            return key.substring (0, 4) + "..." + key.substring (key.length() - 4);
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseProfileCard)
    };

    class UpdateStatusCard final : public juce::Component {
    public:
        UpdateStatusCard() {
            configureLabel (titleLabel, juce::Font (juce::FontOptions (22.0f, juce::Font::bold)), juce::Justification::centredLeft);
            titleLabel.setText ("Updates", juce::dontSendNotification);
            addAndMakeVisible (titleLabel);

            configureLabel (currentVersionLabel, juce::Font (juce::FontOptions (16.0f)), juce::Justification::centredLeft);
            currentVersionLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.72f));
            addAndMakeVisible (currentVersionLabel);

            configureLabel (statusLabel, juce::Font (juce::FontOptions (16.0f)), juce::Justification::centredLeft);
            statusLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.72f));
            addAndMakeVisible (statusLabel);

            configureLabel (availableVersionLabel, juce::Font (juce::FontOptions (16.0f, juce::Font::bold)), juce::Justification::centredLeft);
            availableVersionLabel.setColour (juce::Label::textColourId, osci::Colours::accentColor());
            addAndMakeVisible (availableVersionLabel);

            checkButton.setButtonText ("Check for update");
            checkButton.onClick = [this] {
                if (onCheckForUpdates) {
                    onCheckForUpdates();
                }
            };
            styleSecondaryButton (checkButton);
            addAndMakeVisible (checkButton);

            updateButton.setButtonText ("Download and install");
            updateButton.onClick = [this] {
                if (onDownloadAndInstall) {
                    onDownloadAndInstall();
                }
            };
            stylePrimaryButton (updateButton);
            addAndMakeVisible (updateButton);

            stableUpdatesButton.setButtonText ("Use stable updates");
            stableUpdatesButton.onClick = [this] {
                if (onUseStableUpdates) {
                    onUseStableUpdates();
                }
            };
            styleSecondaryButton (stableUpdatesButton);
            addAndMakeVisible (stableUpdatesButton);

            addChildComponent (downloadProgress);
        }

        std::function<void()> onCheckForUpdates;
        std::function<void()> onDownloadAndInstall;
        std::function<void()> onUseStableUpdates;

        struct State {
            juce::String currentVersion;
            juce::String statusText;
            juce::String availableVersion;
            bool showCheckButton = true;
            bool showUpdateButton = false;
            bool showStableButton = false;
            bool busy = false;
            bool downloading = false;
        };

        void setState (const State& state) {
            currentVersionLabel.setText (juce::String ("Current version: ") + state.currentVersion,
                                         juce::dontSendNotification);
            statusLabel.setText (state.statusText, juce::dontSendNotification);

            availableVersionLabel.setText (state.availableVersion, juce::dontSendNotification);
            availableVersionLabel.setVisible (state.availableVersion.isNotEmpty());

            checkButton.setVisible (state.showCheckButton);
            updateButton.setVisible (state.showUpdateButton);
            stableUpdatesButton.setVisible (state.showStableButton);

            checkButton.setEnabled(!state.busy && state.showCheckButton);
            updateButton.setEnabled(!state.busy && state.showUpdateButton);
            stableUpdatesButton.setEnabled(!state.busy && state.showStableButton);

            downloadProgress.setVisible (state.downloading);
            if (!state.downloading) {
                downloadProgress.reset();
            }

            resized();
            repaint();
        }

        void resetDownload() {
            downloadProgress.reset();
            downloadProgress.setVisible (true);
        }

        void setDownloadStatus (const juce::String& status) {
            downloadProgress.setStatus (status);
        }

        void setDownloadProgress (double fraction) {
            downloadProgress.setProgress (fraction);
        }

        void hideDownload() {
            downloadProgress.setVisible (false);
            downloadProgress.reset();
        }

        void paint (juce::Graphics& g) override {
            drawCardBackground (g, getLocalBounds());
        }

        void resized() override {
            auto area = getLocalBounds().reduced (22, 18);
            const auto visibleActionCount = (stableUpdatesButton.isVisible() ? 1 : 0)
                + (updateButton.isVisible() ? 1 : 0)
                + (checkButton.isVisible() ? 1 : 0);
            const auto actionWidth = visibleActionCount > 1 ? 352 : 176;
            auto actionArea = area.removeFromRight (juce::jmin (actionWidth, area.getWidth()));
            auto leftArea = area;

            titleLabel.setBounds (leftArea.removeFromTop (30));
            leftArea.removeFromTop (4);

            auto detailsRow = leftArea.removeFromTop (24);
            currentVersionLabel.setBounds (detailsRow.removeFromLeft (190));
            statusLabel.setBounds (detailsRow);

            if (availableVersionLabel.isVisible()) {
                leftArea.removeFromTop (4);
                availableVersionLabel.setBounds (leftArea.removeFromTop (24));
            } else {
                availableVersionLabel.setBounds ({ });
            }

            if (downloadProgress.isVisible()) {
                leftArea.removeFromTop (8);
                downloadProgress.setBounds (leftArea.removeFromTop (46));
            } else {
                downloadProgress.setBounds ({ });
            }

            auto buttonRow = actionArea.withSizeKeepingCentre (actionArea.getWidth(), 38);
            layoutButtonFromRight (buttonRow, stableUpdatesButton, 164);
            layoutButtonFromRight (buttonRow, updateButton, 164);
            layoutButtonFromRight (buttonRow, checkButton, 164);
        }

    private:
        juce::Label titleLabel;
        juce::Label currentVersionLabel;
        juce::Label statusLabel;
        juce::Label availableVersionLabel;
        juce::TextButton checkButton;
        juce::TextButton updateButton;
        juce::TextButton stableUpdatesButton;
        DownloadProgressComponent downloadProgress;

        static void layoutButtonFromRight (juce::Rectangle<int>& row, juce::TextButton& button, int width) {
            if (!button.isVisible()) {
                button.setBounds ({ });
                return;
            }

            button.setBounds (row.removeFromRight (width));
            row.removeFromRight (10);
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpdateStatusCard)
    };

    CommonAudioProcessor& processor;
    LicenseProfileCard licenseCard;
    UpdateStatusCard updateCard;
    juce::TextButton removeLicenseButton;
    osci::SvgButton helpButton;
    std::unique_ptr<osci::LicenseHelpOverlay> helpOverlay;

    std::optional<osci::VersionInfo> availableVersion;
    juce::File downloadedFile;
    bool busy = false;
    bool licenseKeyRevealed = false;
    bool updatesCardVisible = false;
    juce::String currentLicenseKey;
    Notice licenseNotice;
    Notice updateNotice;

    juce::Point<int> getPreferredPanelSize() const override {
        if (requiresPremiumLicense()) {
            return { 560, licenseNotice.text.isNotEmpty() ? 264 : 238 };
        }

        if (!updatesCardVisible) {
            return { 600, 390 };
        }

        const auto licenseCardHeight = licenseNotice.text.isNotEmpty() ? 240 : 210;
        const auto updateCardHeight = availableVersion.has_value() ? 122 : 94;
        return { 620, 86 + licenseCardHeight + 12 + updateCardHeight + 12 + 36 };
    }

    void resizeContent (juce::Rectangle<int> area) override {
        const auto premiumRequired = requiresPremiumLicense();
        auto topBar = getPanelBoundsInPanelLayer().reduced (24).removeFromTop (28);
        if (!premiumRequired) {
            topBar.removeFromRight (28);
            topBar.removeFromRight (12);
        }
        helpButton.setBounds (topBar.removeFromRight (28).withSizeKeepingCentre (26, 26));

        const auto premium = processor.licenseManager.hasPremium();
        auto licenseCardHeight = 286;
        if (premiumRequired) {
            licenseCardHeight = licenseNotice.text.isNotEmpty() ? 216 : 190;
        } else if (premium) {
            licenseCardHeight = licenseNotice.text.isNotEmpty() ? 240 : 210;
        }

        licenseCard.setBounds (area.removeFromTop (juce::jmin (licenseCardHeight, area.getHeight())));

        if (updatesCardVisible) {
            area.removeFromTop (12);
            updateCard.setVisible (true);
            const auto updateCardHeight = availableVersion.has_value() ? 122 : 94;
            updateCard.setBounds (area.removeFromTop (updateCardHeight));
        } else {
            updateCard.setVisible (false);
            updateCard.setBounds ({ });
        }

        area.removeFromTop (12);
        removeLicenseButton.setBounds (removeLicenseButton.isVisible()
            ? area.removeFromBottom (36).removeFromLeft (154)
            : juce::Rectangle<int> {});

        if (helpOverlay != nullptr) {
            helpOverlay->setBounds (getLocalBounds());
        }
    }

    void refreshState() {
        const auto state = processor.licenseManager.getStateForUi();
        const auto status = state.getProperty ("status").toString();
        const auto premium = static_cast<bool> (state.getProperty ("premium"));
        const auto premiumRequired = requiresPremiumLicense();
        const auto productName = juce::String (JucePlugin_Name);
        const auto betaEnabled = osci::UpdateSettings (processor.getProductSlug()).betaUpdatesEnabled();
        currentLicenseKey = state.getProperty ("license_key").toString();
        updatesCardVisible = premium && currentLicenseKey.isNotEmpty();

        setOverlayTitle (premiumRequired ? juce::String() : "Account");
        setDismissible(!premiumRequired);

        LicenseProfileCard::State licenseState;
        licenseState.productName = premium ? juce::String() : productName;
        licenseState.title = premium ? productName
                                     : (premiumRequired ? "Activate premium" : "Free version");
        licenseState.detail = detailTextForState (state, premium, premiumRequired);
        licenseState.badge = badgeTextForState (status, premium, premiumRequired);
        licenseState.licenseKey = currentLicenseKey;
        licenseState.revealLicenseKey = licenseKeyRevealed;
        licenseState.showActivation = premiumRequired || !premium;
        licenseState.showFreeFallback = premiumRequired && hasFreeFallback();
        licenseState.drawChrome = !premiumRequired;
        licenseState.topRightReserve = premiumRequired ? 48 : 0;
        licenseState.busy = busy;
        applyEffectiveLicenseNotice (licenseState, status);
        licenseCard.setState (licenseState);

        UpdateStatusCard::State updateState;
        updateState.currentVersion = JucePlugin_VersionString;
        updateState.statusText = updateStatusText();
        updateState.availableVersion = availableVersion.has_value()
            ? juce::String ("Update available: ") + availableVersion->semver
            : juce::String();
        updateState.showCheckButton = !premiumRequired;
        updateState.showUpdateButton = !premiumRequired && availableVersion.has_value();
        updateState.showStableButton = !premiumRequired && betaEnabled;
        updateState.busy = busy;
        updateState.downloading = busy && updateCard.isVisible() && updateNotice.text.startsWithIgnoreCase ("Downloading");
        updateCard.setState (updateState);
        updateCard.setVisible (updatesCardVisible);

        removeLicenseButton.setVisible(!premiumRequired && status != "free");
        removeLicenseButton.setEnabled(!busy && removeLicenseButton.isVisible());

        relayoutAfterStateChange();
    }

    static juce::String detailTextForState (const juce::ValueTree& state, bool premium, bool premiumRequired) {
        if (premium) {
            const auto email = state.getProperty ("email").toString();
            return email.isNotEmpty() ? email : juce::String ("Premium license active");
        }

        if (premiumRequired) {
            return {};
        }

        return "No premium license activated.";
    }

    static juce::String badgeTextForState (const juce::String& status, bool premium, bool premiumRequired) {
        if (status == "premium_cached_token") {
            return "Offline";
        }

        if (premium) {
            return "Premium";
        }

        if (premiumRequired) {
            return {};
        }

        if (status == "expired_offline") {
            return "Expired";
        }

        return "Free";
    }

    void applyEffectiveLicenseNotice (LicenseProfileCard::State& state, const juce::String& status) const {
        state.notice = licenseNotice.text;
        state.noticeKind = licenseNotice.kind;

        if (state.notice.isNotEmpty()) {
            return;
        }

        if (status == "premium_cached_token") {
            state.notice = "Connect to refresh your license.";
            state.noticeKind = NoticeKind::Warning;
        } else if (status == "expired_offline") {
            state.notice = "License refresh expired. Enter your license key again.";
            state.noticeKind = NoticeKind::Warning;
        }
    }

    juce::String updateStatusText() const {
        if (busy && updateNotice.text.isNotEmpty()) {
            return updateNotice.text;
        }

        if (availableVersion.has_value()) {
            return "Update available";
        }

        if (updateNotice.kind == NoticeKind::Error || updateNotice.kind == NoticeKind::Warning) {
            return updateNotice.text;
        }

        return "No update available";
    }

    bool requiresPremiumLicense() const {
#if OSCI_PREMIUM
        return !processor.licenseManager.hasPremium();
#else
        return false;
#endif
    }

    bool hasFreeFallback() const {
        return processor.getProductSlug() == "osci-render";
    }

    static void styleDangerButton (juce::TextButton& button) {
        const auto danger = juce::Colour::fromRGB (0xD8, 0x56, 0x56);
        button.setColour (juce::TextButton::buttonColourId, danger.withAlpha (0.14f));
        button.setColour (juce::TextButton::buttonOnColourId, danger.withAlpha (0.22f));
        button.setColour (juce::TextButton::textColourOffId, danger.brighter (0.25f));
        button.setColour (juce::TextButton::textColourOnId, danger.brighter (0.35f));
    }

    static juce::Result failWithContext (juce::StringRef context, const juce::Result& result) {
        return juce::Result::fail (juce::String (context) + ": " + result.getErrorMessage());
    }

    static juce::Image loadProductIcon() {
#if defined (SOSCI)
        return juce::ImageFileFormat::loadFrom (BinaryData::sosci_mac_saturated_png,
                                                static_cast<size_t> (BinaryData::sosci_mac_saturated_pngSize));
#else
        return juce::ImageFileFormat::loadFrom (BinaryData::osci_mac_png,
                                                static_cast<size_t> (BinaryData::osci_mac_pngSize));
#endif
    }

    void setNotice (NoticeTarget target, juce::String text, NoticeKind kind) {
        auto& notice = target == NoticeTarget::License ? licenseNotice : updateNotice;
        notice.text = std::move (text);
        notice.kind = notice.text.isEmpty() ? NoticeKind::None : kind;
    }

    void setBusy (bool shouldBeBusy, juce::StringRef text = {}, NoticeTarget target = NoticeTarget::Update) {
        busy = shouldBeBusy;
        if (text.isNotEmpty()) {
            setNotice (target, juce::String (text), NoticeKind::Info);
        }
        refreshState();
    }

    void relayoutAfterStateChange() {
        resized();
        repaint();

        auto* parent = getParentComponent();
        if (parent != nullptr) {
            parent->resized();
            parent->repaint();
        }
    }

    void runAsync (juce::String busyText,
                   NoticeTarget noticeTarget,
                   std::function<juce::Result()> work,
                   std::function<void()> onSuccess = {}) {
        if (busy) {
            return;
        }

        setBusy (true, busyText, noticeTarget);
        auto result = std::make_shared<juce::Result> (juce::Result::ok());
        auto safeThis = juce::Component::SafePointer<LicenseAndUpdatesComponent> (this);

        juce::Thread::launch ([safeThis, result, work = std::move (work), onSuccess = std::move (onSuccess), noticeTarget] () mutable {
            *result = work();

            juce::MessageManager::callAsync ([safeThis, result, onSuccess = std::move (onSuccess), noticeTarget] () mutable {
                if (safeThis == nullptr) {
                    return;
                }

                safeThis->updateCard.hideDownload();
                safeThis->setBusy (false);
                if (result->failed()) {
                    safeThis->setNotice (noticeTarget, result->getErrorMessage(), NoticeKind::Error);
                } else if (onSuccess) {
                    onSuccess();
                }

                safeThis->refreshState();
            });
        });
    }

    void activateLicense() {
        const auto key = licenseCard.getEnteredLicenseKey();
        if (key.isEmpty()) {
            setNotice (NoticeTarget::License, "Enter a license key.", NoticeKind::Error);
            refreshState();
            return;
        }

        auto& manager = processor.licenseManager;
        runAsync ("Activating license...",
                  NoticeTarget::License,
                  [&manager, key] {
                      const auto result = manager.activate (key);
                      return result.failed() ? failWithContext ("Could not activate license", result)
                                             : result;
                  },
                  [this] {
                      licenseKeyRevealed = false;
                      licenseCard.clearEnteredLicenseKey();
                      setNotice (NoticeTarget::License, "License activated.", NoticeKind::Success);
                  });
    }

    void confirmDeactivateLicense() {
        juce::MessageBoxOptions options = juce::MessageBoxOptions()
            .withTitle ("Remove License")
            .withMessage ("This removes the locally cached license from this computer. You can activate again later with your license key.")
            .withButton ("Remove")
            .withButton ("Cancel")
            .withIconType (juce::AlertWindow::WarningIcon)
            .withAssociatedComponent (this);

        auto safeThis = juce::Component::SafePointer<LicenseAndUpdatesComponent> (this);
        juce::AlertWindow::showAsync (options, [safeThis] (int result) {
            if (result != 1 || safeThis == nullptr) {
                return;
            }

            safeThis->deactivateLicense();
        });
    }

    void deactivateLicense() {
        processor.licenseManager.deactivate();
        availableVersion.reset();
        downloadedFile = juce::File();
        currentLicenseKey.clear();
        licenseKeyRevealed = false;
        setNotice (NoticeTarget::License, "License removed.", NoticeKind::Success);
        refreshState();
    }

    void copyLicenseKey() {
        if (currentLicenseKey.isEmpty()) {
            return;
        }

        juce::SystemClipboard::copyTextToClipboard (currentLicenseKey);
        setNotice (NoticeTarget::License, "License key copied.", NoticeKind::Success);
        refreshState();
    }

    void disableBetaUpdates() {
        osci::UpdateSettings (processor.getProductSlug()).useStableTrack();
        availableVersion.reset();
        downloadedFile = juce::File();
        setNotice (NoticeTarget::Update, "Stable updates enabled.", NoticeKind::Success);

        auto* editor = findParentComponentOfClass<CommonPluginEditor>();
        if (editor != nullptr) {
            editor->refreshBetaUpdatesButton();
            editor->resized();
        }

        refreshState();
    }

    void checkForUpdates() {
        auto foundVersion = std::make_shared<std::optional<osci::VersionInfo>>();
        const auto product = processor.getProductSlug();
        const auto currentVersion = juce::String (JucePlugin_VersionString);
        const auto track = selectedReleaseTrack();
        const auto variant = desiredUpdateVariant();

        runAsync ("Checking for updates...",
                  NoticeTarget::Update,
                  [foundVersion, product, currentVersion, track, variant] {
                      osci::UpdateChecker checker;
                      *foundVersion = checker.checkForUpdate (product, currentVersion, track, variant);
                      const auto result = checker.getLastResult();
                      return result.failed() ? failWithContext ("Could not check for updates", result)
                                             : result;
                  },
                  [this, foundVersion] {
                      availableVersion = *foundVersion;
                      downloadedFile = juce::File();
                      if (availableVersion.has_value()) {
                          setNotice (NoticeTarget::Update,
                                     juce::String ("Version ") + availableVersion->semver + " is available.",
                                     NoticeKind::Success);
                      } else {
                          setNotice (NoticeTarget::Update, {}, NoticeKind::None);
                      }
                  });
    }

    void downloadAndInstallUpdate() {
        if (!availableVersion.has_value()) {
            return;
        }

        downloadAndInstallVersion (*availableVersion, processor.licenseManager.getCachedToken(), NoticeTarget::Update);
    }

    void installLatestFreeVersion() {
        auto foundVersion = std::make_shared<std::optional<osci::VersionInfo>>();
        const auto product = processor.getProductSlug();
        const auto noticeTarget = requiresPremiumLicense() ? NoticeTarget::License
                                                           : NoticeTarget::Update;

        runAsync ("Finding free version...",
                  noticeTarget,
                  [foundVersion, product] {
                      osci::UpdateChecker checker;
                      *foundVersion = checker.checkForUpdate (product, "0.0.0.0", osci::ReleaseTrack::Stable, "free");
                      const auto result = checker.getLastResult();
                      return result.failed() ? failWithContext ("Could not find the free installer", result)
                                             : result;
                  },
                  [this, foundVersion, noticeTarget] {
                      if (!foundVersion->has_value()) {
                          setNotice (noticeTarget, "No free download is available.", NoticeKind::Error);
                          return;
                      }

                      downloadAndInstallVersion (**foundVersion, {}, noticeTarget);
                  });
    }

    void downloadAndInstallVersion (const osci::VersionInfo& versionToInstall,
                                    juce::StringRef licenseToken,
                                    NoticeTarget noticeTarget) {
        if (busy) {
            return;
        }

        auto downloaded = std::make_shared<juce::File>();
        const auto version = versionToInstall;
        const auto token = juce::String (licenseToken);
        const auto product = processor.getProductSlug();

        updateCard.resetDownload();
        updateCard.setDownloadStatus (juce::String ("Downloading ") + version.semver + "...");
        updateCard.setDownloadProgress (-1.0);
        setNotice (noticeTarget, juce::String ("Downloading ") + version.semver + "...", NoticeKind::Info);

        auto safeThis = juce::Component::SafePointer<LicenseAndUpdatesComponent> (this);

        runAsync ("Downloading update...",
                  noticeTarget,
                  [downloaded, version, token, product, safeThis] {
                      osci::Downloader::Config config;
                      config.downloadDirectory = osci::HardwareInfo::getDefaultStorageDirectory (product).getChildFile ("downloads");
                      osci::Downloader downloader (config);
                      auto result = downloader.downloadAndVerify (
                          version, token,
                          [safeThis] (double fraction, juce::int64) {
                              juce::MessageManager::callAsync ([safeThis, fraction] {
                                  if (safeThis != nullptr) {
                                      safeThis->updateCard.setDownloadProgress (fraction);
                                  }
                              });
                          });
                      if (result.wasOk()) {
                          *downloaded = downloader.getDownloadedFile();
                      } else {
                          result = failWithContext ("Could not download update", result);
                      }
                      return result;
                  },
                  [this, downloaded, version, noticeTarget] {
                      updateCard.hideDownload();
                      availableVersion = version;
                      downloadedFile = *downloaded;
                      setNotice (noticeTarget, "Update downloaded and verified.", NoticeKind::Success);
                      installUpdate (noticeTarget);
                  });
    }

    void installUpdate (NoticeTarget noticeTarget) {
        if (!downloadedFile.existsAsFile()) {
            setNotice (noticeTarget, "Downloaded installer could not be found.", NoticeKind::Error);
            refreshState();
            return;
        }

        const auto file = downloadedFile;
        const auto version = availableVersion;
        const auto product = processor.getProductSlug();
        const auto currentVersion = juce::String (JucePlugin_VersionString);
        auto safeThis = juce::Component::SafePointer<LicenseAndUpdatesComponent> (this);

        osci::showInstallConfirmation (
            this,
            [safeThis, file, version, product, currentVersion, noticeTarget] {
                const auto launchResult = osci::launchInstallerWithPendingMarkerResult (file, version, product, currentVersion);
                if (safeThis == nullptr) {
                    return;
                }

                if (launchResult.failed()) {
                    safeThis->setNotice (noticeTarget, launchResult.getErrorMessage(), NoticeKind::Error);
                    safeThis->refreshState();
                    return;
                }

                safeThis->setNotice (noticeTarget,
                                     "Installer launched. Reopen the app after installation finishes.",
                                     NoticeKind::Success);
                safeThis->refreshState();
            });
    }

    void showLicenseHelpOverlay() {
        if (helpOverlay != nullptr) {
            helpOverlay->grabKeyboardFocus();
            return;
        }

        helpOverlay = std::make_unique<osci::LicenseHelpOverlay> (
            juce::String::createStringFromData (BinaryData::close_svg, BinaryData::close_svgSize));
        helpOverlay->onDismissRequested = [this] {
            helpOverlay = nullptr;
            resized();
        };

        helpOverlay->captureBackdropFrom (*this);
        addAndMakeVisible (*helpOverlay);
        helpOverlay->setBounds (getLocalBounds());
        helpOverlay->toFront (false);
        helpOverlay->grabKeyboardFocus();
    }

    osci::ReleaseTrack selectedReleaseTrack() const {
        return osci::UpdateSettings (processor.getProductSlug()).releaseTrack();
    }

    static juce::String compiledVariant() {
#if OSCI_PREMIUM
        return "premium";
#else
        return "free";
#endif
    }

    juce::String desiredUpdateVariant() const {
        if (processor.licenseManager.hasPremium()) {
            return "premium";
        }

        return compiledVariant();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseAndUpdatesComponent)
};
