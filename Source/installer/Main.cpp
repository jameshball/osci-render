#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>

#include "../components/InstallFlowHelpers.h"

namespace {
    enum class ProductChoice {
        None,
        OsciRender,
        Sosci,
    };

    enum class InstallPath {
        None,
        OsciRenderFree,
        OsciRenderPremium,
        SosciPremium,
    };

    struct InstallRequest {
        juce::String productSlug;
        juce::String productName;
        juce::String variant;
        juce::String licenseKey;
        bool premium = false;
    };

    juce::String currentInstallerVersionBaseline() {
        return "0.0.0.0";
    }

    juce::Image loadImage (const void* data, int size) {
        return juce::ImageFileFormat::loadFrom (data, static_cast<size_t> (size));
    }

    osci::LookAndFeel::TypefaceData makeTypefaceData() {
        return {
            BinaryData::FiraSansRegular_ttf,
            BinaryData::FiraSansRegular_ttfSize,
            BinaryData::FiraSansSemiBold_ttf,
            BinaryData::FiraSansSemiBold_ttfSize,
            BinaryData::FiraSansItalic_ttf,
            BinaryData::FiraSansItalic_ttfSize,
        };
    }

    juce::String productSlug (ProductChoice product) {
        return product == ProductChoice::Sosci ? "sosci" : "osci-render";
    }

    juce::String errorWithContext (juce::StringRef context, juce::StringRef detail) {
        auto message = juce::String (context);
        if (detail.isNotEmpty()) {
            message << ": " << detail;
        }

        return message;
    }

    juce::Result failWithContext (juce::StringRef context, const juce::Result& result) {
        return juce::Result::fail (errorWithContext (context, result.getErrorMessage()));
    }

}

class InstallerComponent final : public juce::Component {
public:
    InstallerComponent()
        : osciRenderTile ("osci-render", loadImage (BinaryData::osci_mac_png, BinaryData::osci_mac_pngSize), "osci-render"),
          sosciTile ("sosci", loadImage (BinaryData::sosci_mac_png, BinaryData::sosci_mac_pngSize), "sosci"),
          needLicenseLink ("Need a license key?", juce::URL ("https://osci-render.com/#purchase")),
          progressBar (progressValue) {
        addAndMakeVisible (headingLabel);
        headingLabel.setText ("Choose what to install", juce::dontSendNotification);
        headingLabel.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        headingLabel.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (helpButton);
        helpButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        helpButton.setTooltip ("Help");
        helpButton.onClick = [this] {
            showSupportOverlay();
        };

        addAndMakeVisible (osciRenderTile);
        osciRenderTile.setLayoutMode (osci::GridItemComponent::LayoutMode::IconTile);
        osciRenderTile.onItemSelected = [this] (const juce::String&) {
            selectProduct (ProductChoice::OsciRender);
        };

        addAndMakeVisible (sosciTile);
        sosciTile.setLayoutMode (osci::GridItemComponent::LayoutMode::IconTile);
        sosciTile.onItemSelected = [this] (const juce::String&) {
            selectProduct (ProductChoice::Sosci);
        };

        addAndMakeVisible (panel);

        panel.addAndMakeVisible (choiceLabel);
        choiceLabel.setJustificationType (juce::Justification::centred);

        panel.addAndMakeVisible (freeChoiceButton);
        freeChoiceButton.setButtonText ("Get osci-render free");
        freeChoiceButton.onClick = [this] {
            startInstall (InstallPath::OsciRenderFree);
        };

        panel.addAndMakeVisible (premiumChoiceButton);
        premiumChoiceButton.setButtonText ("Install premium");
        premiumChoiceButton.onClick = [this] {
            choosePremiumPath();
        };

        panel.addAndMakeVisible (statusLabel);
        statusLabel.setJustificationType (juce::Justification::centred);
        statusLabel.setMinimumHorizontalScale (0.75f);

        panel.addAndMakeVisible (licenseKeyEditor);
        licenseKeyEditor.setTextToShowWhenEmpty ("License key", juce::Colours::white.withAlpha (0.5f));
        licenseKeyEditor.setColour (juce::TextEditor::backgroundColourId, osci::Colours::dark().brighter (0.06f));
        licenseKeyEditor.setColour (juce::TextEditor::textColourId, juce::Colours::white.withAlpha (0.92f));
        licenseKeyEditor.setColour (juce::TextEditor::outlineColourId, osci::Colours::grey().withAlpha (0.38f));
        licenseKeyEditor.setColour (juce::TextEditor::focusedOutlineColourId, osci::Colours::accentColor().withAlpha (0.9f));
        licenseKeyEditor.setColour (juce::CaretComponent::caretColourId, juce::Colours::white);
        licenseKeyEditor.setSelectAllWhenFocused (true);

        panel.addAndMakeVisible (needLicenseLink);
        needLicenseLink.setColour (juce::HyperlinkButton::textColourId, osci::Colours::accentColor());

        panel.addAndMakeVisible (premiumInstallButton);
        premiumInstallButton.onClick = [this] {
            startInstall (currentPath);
        };

        panel.addAndMakeVisible (progressBar);
        progressBar.setVisible (false);

        styleSecondaryButton (freeChoiceButton);
        stylePrimaryButton (premiumChoiceButton);
        stylePrimaryButton (premiumInstallButton);

        setSize (720, 560);
        refreshUi();
    }

    void paint (juce::Graphics& g) override {
        g.fillAll (osci::Colours::veryDark());
    }

    void resized() override {
        auto area = getLocalBounds().reduced (40, 34);
        helpButton.setBounds (getLocalBounds().reduced (24, 20).removeFromTop (34).removeFromRight (34));

        headingLabel.setBounds (area.removeFromTop (44));
        area.removeFromTop (28);

        auto tilesRow = area.removeFromTop (228);
        const auto tileWidth = 214;
        const auto gap = 34;
        const auto totalWidth = tileWidth * 2 + gap;
        auto tileArea = tilesRow.withSizeKeepingCentre (totalWidth, tilesRow.getHeight());
        osciRenderTile.setBounds (tileArea.removeFromLeft (tileWidth));
        tileArea.removeFromLeft (gap);
        sosciTile.setBounds (tileArea.removeFromLeft (tileWidth));

        area.removeFromTop (22);
        panel.setBounds (area.removeFromTop (180).withTrimmedLeft (38).withTrimmedRight (38));
        layoutPanel();

        if (supportOverlay != nullptr) {
            supportOverlay->setBounds (getLocalBounds());
        }
    }

private:
    osci::SvgButton helpButton { "installerHelp", juce::String (BinaryData::help_svg), juce::Colours::white };
    juce::Label headingLabel;
    osci::GridItemComponent osciRenderTile;
    osci::GridItemComponent sosciTile;
    juce::Component panel;
    juce::Label choiceLabel;
    juce::TextButton freeChoiceButton;
    juce::TextButton premiumChoiceButton;
    juce::Label statusLabel;
    osci::TextEditor licenseKeyEditor;
    juce::HyperlinkButton needLicenseLink;
    juce::TextButton premiumInstallButton;
    double progressValue = 0.0;
    juce::ProgressBar progressBar;
    std::unique_ptr<osci::LicenseHelpOverlay> supportOverlay;

    ProductChoice selectedProduct = ProductChoice::None;
    InstallPath currentPath = InstallPath::None;
    bool busy = false;
    bool hasCachedPremiumToken = false;
    bool cachedTokenNeedsRefresh = false;
    juce::String cachedTokenMessage;

    void showSupportOverlay() {
        if (supportOverlay != nullptr) {
            supportOverlay->grabKeyboardFocus();
            return;
        }

        supportOverlay = std::make_unique<osci::LicenseHelpOverlay> (
            juce::String::createStringFromData (BinaryData::close_svg, BinaryData::close_svgSize));
        supportOverlay->onDismissRequested = [this] {
            supportOverlay = nullptr;
        };

        addAndMakeVisible (*supportOverlay);
        supportOverlay->setBounds (getLocalBounds());
        supportOverlay->toFront (false);
        supportOverlay->grabKeyboardFocus();
    }

    void layoutPanel() {
        auto area = panel.getLocalBounds().reduced (22, 18);

        if (currentPath == InstallPath::None && selectedProduct == ProductChoice::OsciRender) {
            auto row = area.removeFromTop (38).withSizeKeepingCentre (420, 38);
            freeChoiceButton.setBounds (row.removeFromLeft (200));
            row.removeFromLeft (20);
            premiumChoiceButton.setBounds (row.removeFromLeft (200));
            choiceLabel.setBounds ({});
            statusLabel.setBounds ({});
            progressBar.setBounds ({});
            licenseKeyEditor.setBounds ({});
            needLicenseLink.setBounds ({});
            premiumInstallButton.setBounds ({});
            return;
        }

        if (choiceLabel.isVisible()) {
            choiceLabel.setBounds (area.removeFromTop (24));
            area.removeFromTop (12);
        } else {
            choiceLabel.setBounds ({});
        }

        if (isPremiumPath (currentPath)) {
            const auto showKeyEntry = ! hasCachedPremiumToken || licenseKeyEditor.isVisible();
            if (showKeyEntry) {
                licenseKeyEditor.setBounds (area.removeFromTop (34).withSizeKeepingCentre (380, 34));
                area.removeFromTop (12);
            } else {
                licenseKeyEditor.setBounds ({});
            }

            premiumInstallButton.setBounds (area.removeFromTop (38).withSizeKeepingCentre (260, 38));
            area.removeFromTop (8);
            needLicenseLink.setBounds (showKeyEntry ? area.removeFromTop (22).withSizeKeepingCentre (180, 22)
                                                    : juce::Rectangle<int> {});
            area.removeFromTop (12);
        } else {
            licenseKeyEditor.setBounds ({});
            needLicenseLink.setBounds ({});
            premiumInstallButton.setBounds ({});
        }

        progressBar.setBounds (area.removeFromTop (22).withSizeKeepingCentre (380, 22));
        area.removeFromTop (8);
        statusLabel.setBounds (area.removeFromTop (42));
    }

    void selectProduct (ProductChoice product) {
        if (busy) {
            return;
        }

        selectedProduct = product;
        currentPath = product == ProductChoice::Sosci ? InstallPath::SosciPremium : InstallPath::None;
        licenseKeyEditor.clear();
        loadCachedLicenseState();
        refreshUi();
    }

    void choosePremiumPath() {
        if (busy || selectedProduct == ProductChoice::None) {
            return;
        }

        currentPath = selectedProduct == ProductChoice::Sosci ? InstallPath::SosciPremium : InstallPath::OsciRenderPremium;
        loadCachedLicenseState();
        refreshUi();
    }

    void loadCachedLicenseState() {
        hasCachedPremiumToken = false;
        cachedTokenNeedsRefresh = false;
        cachedTokenMessage.clear();

        if (selectedProduct == ProductChoice::None) {
            return;
        }

        osci::LicenseManager::Config config;
        config.productSlug = productSlug (selectedProduct);
        osci::LicenseManager manager (std::move (config));
        const auto loadResult = manager.loadCachedToken();
        const auto status = manager.status();

        if (manager.hasPremium()) {
            hasCachedPremiumToken = true;
            cachedTokenNeedsRefresh = status == osci::LicenseManager::Status::PremiumCachedToken;
            cachedTokenMessage = cachedTokenNeedsRefresh
                ? "Cached premium license found. It will refresh before download."
                : "Cached premium license found.";
            return;
        }

        if (loadResult.failed() && status == osci::LicenseManager::Status::ExpiredOffline) {
            cachedTokenMessage = "Cached premium license expired. Enter your license key.";
            return;
        }

        cachedTokenMessage = "Enter your license key to install premium.";
    }

    void refreshUi() {
        const auto selectedOsciRender = selectedProduct == ProductChoice::OsciRender;
        const auto selectedSosci = selectedProduct == ProductChoice::Sosci;
        const auto premiumPath = isPremiumPath (currentPath);
        const auto showOsciChoice = selectedOsciRender && currentPath == InstallPath::None;
        const auto showPanel = selectedProduct != ProductChoice::None;
        const auto showKeyEntry = premiumPath && ! hasCachedPremiumToken;
        const auto showChoiceLabel = showKeyEntry && ! busy;

        osciRenderTile.setSelected (selectedOsciRender);
        sosciTile.setSelected (selectedSosci);
        osciRenderTile.setInteractive (! busy);
        sosciTile.setInteractive (! busy);

        panel.setVisible (showPanel);
        choiceLabel.setVisible (showChoiceLabel);
        freeChoiceButton.setVisible (showOsciChoice);
        premiumChoiceButton.setVisible (showOsciChoice);
        statusLabel.setVisible (showPanel && ! showOsciChoice);
        licenseKeyEditor.setVisible (showKeyEntry);
        needLicenseLink.setVisible (showKeyEntry);
        premiumInstallButton.setVisible (premiumPath);
        progressBar.setVisible (busy || progressValue > 0.0);

        freeChoiceButton.setEnabled (! busy);
        premiumChoiceButton.setEnabled (! busy);
        licenseKeyEditor.setEnabled (! busy);
        premiumInstallButton.setEnabled (! busy);

        if (showKeyEntry) {
            choiceLabel.setText ("Enter your license key", juce::dontSendNotification);
        } else if (showOsciChoice) {
            choiceLabel.setText ("Choose an edition", juce::dontSendNotification);
        }

        if (! busy) {
            statusLabel.setText (statusTextForCurrentState(), juce::dontSendNotification);
        }

        premiumInstallButton.setButtonText (buttonTextForPremiumPath());
        layoutPanel();
        repaint();
    }

    juce::String statusTextForCurrentState() const {
        if (selectedProduct == ProductChoice::None) {
            return {};
        }

        if (currentPath == InstallPath::OsciRenderFree) {
            return "Installs the latest stable free build.";
        }

        if (currentPath == InstallPath::None) {
            return {};
        }

        return cachedTokenMessage;
    }

    juce::String buttonTextForPremiumPath() const {
        if (hasCachedPremiumToken) {
            return cachedTokenNeedsRefresh ? "Refresh and install premium" : "Install premium";
        }

        return "Activate and install premium";
    }

    void stylePrimaryButton (juce::TextButton& button) {
        button.setColour (juce::TextButton::buttonColourId, osci::Colours::accentColor());
        button.setColour (juce::TextButton::buttonOnColourId, osci::Colours::accentColor().brighter (0.12f));
        button.setColour (juce::TextButton::textColourOffId, osci::Colours::veryDark());
        button.setColour (juce::TextButton::textColourOnId, osci::Colours::veryDark());
    }

    void styleSecondaryButton (juce::TextButton& button) {
        button.setColour (juce::TextButton::buttonColourId, osci::Colours::veryDark().brighter (0.14f));
        button.setColour (juce::TextButton::buttonOnColourId, osci::Colours::dark().brighter (0.12f));
        button.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }

    static bool isPremiumPath (InstallPath path) {
        return path == InstallPath::OsciRenderPremium || path == InstallPath::SosciPremium;
    }

    InstallRequest makeRequest (InstallPath path) const {
        InstallRequest request;
        request.productSlug = path == InstallPath::SosciPremium ? "sosci" : "osci-render";
        request.productName = path == InstallPath::SosciPremium ? "sosci" : "osci-render";
        request.variant = path == InstallPath::OsciRenderFree ? "free" : "premium";
        request.licenseKey = licenseKeyEditor.getText().trim();
        request.premium = path != InstallPath::OsciRenderFree;
        return request;
    }

    void startInstall (InstallPath path) {
        if (busy || path == InstallPath::None) {
            return;
        }

        currentPath = path;
        const auto request = makeRequest (path);

        if (request.premium && request.licenseKey.isEmpty() && ! hasCachedPremiumToken) {
            statusLabel.setText ("Enter your license key to install premium.", juce::dontSendNotification);
            refreshUi();
            return;
        }

        progressValue = 0.0;
        setBusy (true, request.premium ? "Preparing premium install..." : "Preparing free install...");

        auto safeThis = juce::Component::SafePointer<InstallerComponent> (this);
        juce::Thread::launch ([safeThis, request] {
            runInstallRequest (safeThis, request);
        });
    }

    void setBusy (bool shouldBeBusy, const juce::String& message) {
        busy = shouldBeBusy;
        refreshUi();
        statusLabel.setText (message, juce::dontSendNotification);
    }

    static void runInstallRequest (juce::Component::SafePointer<InstallerComponent> safeThis,
                                   InstallRequest request) {
        juce::Result result = juce::Result::ok();
        juce::String token;
        std::optional<osci::VersionInfo> version;
        juce::File installerFile;

        osci::LicenseManager::Config licenseConfig;
        licenseConfig.productSlug = request.productSlug;
        osci::LicenseManager licenseManager (std::move (licenseConfig));
        const auto cachedLoadResult = licenseManager.loadCachedToken();
        juce::ignoreUnused (cachedLoadResult);

        if (request.premium) {
            result = preparePremiumToken (licenseManager, request.licenseKey, token);
        }

        if (result.wasOk()) {
            juce::MessageManager::callAsync ([safeThis, request] {
                if (safeThis == nullptr) {
                    return;
                }

                safeThis->statusLabel.setText ("Checking latest stable " + request.productName + " " + request.variant + " release...",
                                               juce::dontSendNotification);
            });

            osci::UpdateChecker checker;
            version = checker.checkForUpdate (request.productSlug,
                                              currentInstallerVersionBaseline(),
                                              osci::ReleaseTrack::Stable,
                                              request.variant);

            if (! version.has_value()) {
                const auto checkResult = checker.getLastResult();
                result = checkResult.failed()
                    ? failWithContext ("Could not check for the latest stable " + request.productName + " " + request.variant + " installer",
                                       checkResult)
                    : juce::Result::fail ("No stable " + request.productName + " " + request.variant
                                          + " installer is available for " + osci::HardwareInfo::getCurrentPlatform() + ".");
            }
        }

        if (result.wasOk()) {
            osci::Downloader::Config config;
            config.downloadDirectory = osci::HardwareInfo::getDefaultStorageDirectory ("osci-installer")
                .getChildFile ("downloads");
            osci::Downloader downloader (std::move (config));
            result = downloader.downloadAndVerify (*version, token, [safeThis] (double fraction, juce::int64 downloadedBytes) {
                juce::MessageManager::callAsync ([safeThis, fraction, downloadedBytes] {
                    if (safeThis == nullptr) {
                        return;
                    }

                    safeThis->progressValue = fraction >= 0.0 ? fraction : -1.0;
                    safeThis->statusLabel.setText ("Downloading " + juce::File::descriptionOfSizeInBytes (downloadedBytes),
                                                   juce::dontSendNotification);
                    safeThis->refreshUi();
                });
            });

            if (result.wasOk()) {
                installerFile = downloader.getDownloadedFile();
            } else {
                result = failWithContext ("Could not download " + request.productName + " " + request.variant
                                          + " " + version->semver,
                                          result);
            }
        }

        juce::MessageManager::callAsync ([safeThis, request, result, version, installerFile] {
            if (safeThis == nullptr) {
                return;
            }

            safeThis->handleInstallResult (request, result, version, installerFile);
        });
    }

    static juce::Result preparePremiumToken (osci::LicenseManager& licenseManager,
                                             const juce::String& licenseKey,
                                             juce::String& token) {
        if (licenseKey.isNotEmpty()) {
            const auto activationResult = licenseManager.activate (licenseKey);
            if (activationResult.failed()) {
                return failWithContext ("Could not activate license", activationResult);
            }
        } else if (licenseManager.status() == osci::LicenseManager::Status::PremiumCachedToken) {
            const auto refreshResult = licenseManager.refreshNow();
            if (refreshResult.failed()) {
                return failWithContext ("Could not refresh cached license", refreshResult);
            }
        } else if (licenseManager.status() == osci::LicenseManager::Status::ExpiredOffline) {
            return juce::Result::fail ("Cached premium license has expired. Paste your license key and try again.");
        } else if (! licenseManager.hasPremium()) {
            return juce::Result::fail ("Enter a license key or activate this product from the plugin first.");
        }

        if (! licenseManager.hasPremium()) {
            return juce::Result::fail ("This license is not valid for premium downloads.");
        }

        token = licenseManager.getCachedToken();
        return token.isNotEmpty() ? juce::Result::ok()
                                  : juce::Result::fail ("No premium license token is available.");
    }

    void handleInstallResult (const InstallRequest& request,
                              const juce::Result& result,
                              const std::optional<osci::VersionInfo>& version,
                              const juce::File& installerFile) {
        if (result.failed()) {
            progressValue = 0.0;
            if (request.premium) {
                hasCachedPremiumToken = false;
                cachedTokenNeedsRefresh = false;
                licenseKeyEditor.setVisible (true);
            }

            setBusy (false, result.getErrorMessage());
            showErrorDialog ("Install failed", result.getErrorMessage());
            return;
        }

        if (! version.has_value() || ! installerFile.existsAsFile()) {
            progressValue = 0.0;
            const auto message = "The installer downloaded, but the installer file was not found on disk.";
            setBusy (false, message);
            showErrorDialog ("Install failed", message);
            return;
        }

        progressValue = 1.0;
        setBusy (true, "Downloaded and verified. Confirm to launch the installer.");

        auto safeThis = juce::Component::SafePointer<InstallerComponent> (this);
        osci::showInstallConfirmation (
            this,
            [safeThis, request, version, installerFile] {
                if (safeThis != nullptr) {
                    safeThis->statusLabel.setText ("Launching installer...", juce::dontSendNotification);
                }

                const auto launchResult = osci::launchInstallerWithPendingMarkerResult (installerFile,
                                                                                        version,
                                                                                        request.productSlug,
                                                                                        currentInstallerVersionBaseline());
                if (launchResult.failed()) {
                    if (safeThis != nullptr) {
                        safeThis->progressValue = 0.0;
                        safeThis->setBusy (false, launchResult.getErrorMessage());
                        safeThis->showErrorDialog ("Install failed", launchResult.getErrorMessage());
                    }
                    return;
                }

                if (safeThis != nullptr) {
                    safeThis->statusLabel.setText ("Installer launched.", juce::dontSendNotification);
                }
            },
            [safeThis] {
                if (safeThis != nullptr) {
                    safeThis->setBusy (false, "Install cancelled.");
                }
            });
    }

    void showErrorDialog (juce::StringRef title, juce::StringRef message) {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                title,
                                                juce::String (message));
    }
};

class InstallerWindow final : public juce::DocumentWindow {
public:
    explicit InstallerWindow (juce::String name)
        : DocumentWindow (std::move (name),
                          osci::Colours::veryDark(),
                          juce::DocumentWindow::allButtons) {
        setUsingNativeTitleBar (true);
        setResizable (false, false);
        setContentOwned (new InstallerComponent(), true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class OsciInstallerApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override {
        return ProjectInfo::projectName;
    }

    const juce::String getApplicationVersion() override {
        return ProjectInfo::versionString;
    }

    bool moreThanOneInstanceAllowed() override {
        return true;
    }

    void initialise (const juce::String&) override {
        lookAndFeel = std::make_unique<osci::LookAndFeel> (makeTypefaceData());
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
        mainWindow = std::make_unique<InstallerWindow> (getApplicationName());
    }

    void shutdown() override {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel = nullptr;
    }

    void systemRequestedQuit() override {
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {
    }

private:
    std::unique_ptr<osci::LookAndFeel> lookAndFeel;
    std::unique_ptr<InstallerWindow> mainWindow;
};

START_JUCE_APPLICATION (OsciInstallerApplication)
