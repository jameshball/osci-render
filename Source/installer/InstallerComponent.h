#pragma once

#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>

#if DEBUG && JUCE_MODULE_AVAILABLE_jucewright
    #include <jucewright/jucewright.h>
#endif

#include "../components/InstallFlowHelpers.h"
#include "InstallCompletionComponent.h"
#include "LinuxInstallLocationsComponent.h"

namespace osci::installer {

    juce::Image externalLaserPlaceholderIcon() {
        juce::Image image(juce::Image::ARGB, 256, 256, true);
        juce::Graphics graphics(image);
        graphics.fillAll(osci::Colours::surfaceRaised());
        graphics.setColour(osci::Colours::outline());
        graphics.drawRoundedRectangle(image.getBounds().toFloat().reduced(8.0f), 32.0f, 6.0f);
        graphics.setColour(osci::Colours::accentColor());
        juce::Path beam;
        beam.startNewSubPath(54.0f, 188.0f);
        beam.lineTo(128.0f, 68.0f);
        beam.lineTo(202.0f, 188.0f);
        graphics.strokePath(beam, juce::PathStrokeType(14.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return image;
    }

#if JUCE_LINUX
    juce::MemoryBlock productIconPng (juce::StringRef product) {
        const auto slug = juce::String(product);
        if (slug == "sosci") {
            return { BinaryData::sosci_mac_saturated_png, static_cast<size_t> (BinaryData::sosci_mac_saturated_pngSize) };
        }
        if (slug == "osci-laser") {
            juce::MemoryOutputStream output;
            juce::PNGImageFormat().writeImageToStream(externalLaserPlaceholderIcon(), output);
            return output.getMemoryBlock();
        }

        return { BinaryData::osci_mac_png, static_cast<size_t> (BinaryData::osci_mac_pngSize) };
    }
#endif

    enum class ProductChoice {
        None,
        OsciRender,
        Sosci,
        OsciLaser,
    };

    enum class InstallPath {
        None,
        OsciRenderFree,
        OsciRenderPremium,
        SosciPremium,
        OsciLaserPremium,
    };

    struct InstallRequest {
        juce::String productSlug;
        juce::String productName;
        juce::String variant;
        juce::String licenseKey;
        osci::LinuxInstallLocations locations;
        bool premium = false;
    };

    juce::String currentInstallerVersionBaseline() {
        return "0.0.0.0";
    }

    juce::Image loadImage (const void* data, int size) {
        return juce::ImageFileFormat::loadFrom (data, static_cast<size_t>(size));
    }

    juce::String productSlug (ProductChoice product) {
        switch (product) {
            case ProductChoice::Sosci: return "sosci";
            case ProductChoice::OsciLaser: return "osci-laser";
            case ProductChoice::OsciRender:
            case ProductChoice::None: return "osci-render";
        }
        jassertfalse;
        return {};
    }

    juce::URL purchaseUrl(ProductChoice product) {
        if (product == ProductChoice::OsciLaser) {
            return juce::URL("https://osci-render.com/osci-laser#purchase");
        }
        return juce::URL("https://osci-render.com/#purchase");
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

class InstallerComponent final : public juce::Component {
public:
    InstallerComponent()
        : osciRenderTile ("osci-render", loadImage (BinaryData::osci_mac_png, BinaryData::osci_mac_pngSize), "osci-render"),
          sosciTile ("sosci", loadImage (BinaryData::sosci_mac_saturated_png, BinaryData::sosci_mac_saturated_pngSize), "sosci"),
          osciLaserTile("osci-laser", externalLaserPlaceholderIcon(), "osci-laser"),
          needLicenseLink ("Need a license key?", juce::URL ("https://osci-render.com/#purchase")),
          progressBar (progressValue) {
        addAndMakeVisible (headingLabel);
        headingLabel.setText ("Choose what to install", juce::dontSendNotification);
        headingLabel.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        headingLabel.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (helpButton);
        helpButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        helpButton.setTitle ("Help");
        helpButton.setTooltip ("Help");
        helpButton.onClick = [this] {
            showSupportOverlay();
        };

        addAndMakeVisible (osciRenderTile);
        osciRenderTile.setTitle ("osci-render");
        osciRenderTile.setWantsKeyboardFocus (true);
        osciRenderTile.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        osciRenderTile.setLayoutMode (osci::GridItemComponent::LayoutMode::IconTile);
        osciRenderTile.onItemSelected = [this] (const juce::String&) {
            selectProduct (ProductChoice::OsciRender);
        };

        addAndMakeVisible (sosciTile);
        sosciTile.setTitle ("sosci");
        sosciTile.setWantsKeyboardFocus (true);
        sosciTile.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        sosciTile.setLayoutMode (osci::GridItemComponent::LayoutMode::IconTile);
        sosciTile.onItemSelected = [this] (const juce::String&) {
            selectProduct (ProductChoice::Sosci);
        };

        addAndMakeVisible(osciLaserTile);
        osciLaserTile.setTitle("osci-laser");
        osciLaserTile.setWantsKeyboardFocus(true);
        osciLaserTile.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        osciLaserTile.setLayoutMode(osci::GridItemComponent::LayoutMode::IconTile);
        osciLaserTile.onItemSelected = [this](const juce::String&) {
            selectProduct(ProductChoice::OsciLaser);
        };

        addAndMakeVisible (panel);

        panel.addAndMakeVisible (choiceLabel);
        choiceLabel.setJustificationType (juce::Justification::centred);

        panel.addAndMakeVisible (freeChoiceButton);
#if JUCE_LINUX
        freeChoiceButton.setButtonText ("Install free");
#else
        freeChoiceButton.setButtonText ("Get osci-render free");
#endif
        freeChoiceButton.onClick = [this] {
            requestInstall (InstallPath::OsciRenderFree);
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
        licenseKeyEditor.onReturnKey = [this] {
            if (premiumInstallButton.isVisible() && premiumInstallButton.isEnabled()) {
                premiumInstallButton.triggerClick();
            }
        };

        panel.addAndMakeVisible (needLicenseLink);
        needLicenseLink.setColour (juce::HyperlinkButton::textColourId, osci::Colours::accentColor());

        panel.addAndMakeVisible (premiumInstallButton);
        premiumInstallButton.onClick = [this] {
            requestInstall (currentPath);
        };

        panel.addAndMakeVisible (progressBar);
        progressBar.setVisible (false);

#if JUCE_LINUX
        locationsPanel.onCancel = [this] {
            if (locationsOverlay != nullptr) {
                locationsOverlay->requestDismiss();
            }
        };
        locationsPanel.onConfirm = [this] (osci::LinuxInstallLocations locations) {
            selectedLocations = std::move (locations);
            installLocationsConfirmed = true;
            if (locationsOverlay != nullptr) {
                locationsOverlay->requestDismiss();
            }
        };

        addChildComponent (completionPanel);
        completionPanel.onInstallAnother = [this] { resetAfterInstall(); };
        completionPanel.onClose = [] { juce::JUCEApplicationBase::quit(); };
#endif

        styleSecondaryButton (freeChoiceButton);
        stylePrimaryButton (premiumChoiceButton);
        stylePrimaryButton (premiumInstallButton);

#if JUCE_LINUX
        setSize (760, 520);
#else
        setSize (720, 560);
#endif
        refreshUi();
    }

    ~InstallerComponent() override {
#if JUCE_LINUX
        if (installThread != nullptr) {
            installThread->stopThread (-1);
        }
#endif
    }

    std::function<void (bool)> onBusyChanged;

    bool installationInProgress() const noexcept {
        return busy;
    }

    void paint (juce::Graphics& g) override {
        g.fillAll (osci::Colours::veryDark());
    }

    void resized() override {
#if JUCE_LINUX
        if (installationComplete) {
            helpButton.setBounds ({});
            headingLabel.setBounds ({});
            osciRenderTile.setBounds ({});
            sosciTile.setBounds ({});
            osciLaserTile.setBounds({});
            locationsPanel.setBounds ({});
            panel.setBounds ({});
            completionPanel.setBounds (getLocalBounds().reduced (72, 52));
            return;
        }
#endif
#if JUCE_LINUX
        auto area = getLocalBounds().withTrimmedLeft (40).withTrimmedRight (40).withTrimmedTop (51);
#else
        auto area = getLocalBounds().reduced (40, 16);
#endif
        helpButton.setBounds (getLocalBounds().reduced (24, 20).removeFromTop (34).removeFromRight (34));

        headingLabel.setBounds (area.removeFromTop (44));
        area.removeFromTop (16);

#if JUCE_LINUX
        constexpr auto tileRowHeight = 180;
#else
        constexpr auto tileRowHeight = 228;
#endif
        auto tilesRow = area.removeFromTop (tileRowHeight);
        const auto tileWidth = 184;
        const auto gap = 20;
        const auto totalWidth = tileWidth * 3 + gap * 2;
        auto tileArea = tilesRow.withSizeKeepingCentre (totalWidth, tilesRow.getHeight());
        osciRenderTile.setBounds (tileArea.removeFromLeft (tileWidth));
        tileArea.removeFromLeft (gap);
        sosciTile.setBounds (tileArea.removeFromLeft (tileWidth));
        tileArea.removeFromLeft(gap);
        osciLaserTile.setBounds(tileArea.removeFromLeft(tileWidth));

#if JUCE_LINUX
        area.removeFromTop (18);
        panel.setBounds (area.removeFromTop (160).withTrimmedLeft (38).withTrimmedRight (38));
#else
        area.removeFromTop (22);
        panel.setBounds (area.removeFromTop (180).withTrimmedLeft (38).withTrimmedRight (38));
#endif
        layoutPanel();

        if (supportOverlay != nullptr) {
            supportOverlay->setBounds (getLocalBounds());
        }
#if JUCE_LINUX
        if (locationsOverlay != nullptr) {
            locationsOverlay->setBounds (getLocalBounds());
        }
#endif
    }

private:
    class ProductTile final : public osci::GridItemComponent {
    public:
        using GridItemComponent::GridItemComponent;

        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
            juce::AccessibilityActions actions;
            actions.addAction (juce::AccessibilityActionType::press, [this] { activate(); });
            return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::button, std::move (actions));
        }

        bool keyPressed (const juce::KeyPress& key) override {
            if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) {
                activate();
                return true;
            }

            return GridItemComponent::keyPressed (key);
        }

    private:
        void activate() {
            if (isInteractive() && onItemSelected != nullptr) {
                onItemSelected (getId());
            }
        }
    };

    class CardPanel final : public juce::Component {
    public:
        void paint (juce::Graphics& g) override {
            const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (osci::Colours::surfaceRaised());
            g.fillRoundedRectangle (bounds, 7.0f);
            g.setColour (osci::Colours::outline());
            g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
        }
    };

    class InstallerProgressBar final : public juce::ProgressBar {
    public:
        explicit InstallerProgressBar (double& progressToUse) : juce::ProgressBar (progressToUse), progress (progressToUse) {
        }

        void paint (juce::Graphics& g) override {
            const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
            const auto indeterminate = progress < 0.0;
            const auto fraction = static_cast<float> (juce::jlimit (0.0, 1.0, progress));
            g.setColour (osci::Colours::veryDark().brighter (0.15f));
            g.fillRoundedRectangle (bounds, 7.0f);
            if (fraction > 0.0f) {
                g.setColour (osci::Colours::accentColor().withAlpha (0.9f));
                g.fillRoundedRectangle (bounds.withWidth (bounds.getWidth() * fraction), 7.0f);
            }
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            const auto text = indeterminate ? juce::String ("...")
                                            : juce::String (juce::roundToInt (progress * 100.0)) + "%";
            g.drawText (text, getLocalBounds(), juce::Justification::centred);
        }

    private:
        double& progress;
    };

#if JUCE_LINUX
    class InstallThread final : public juce::Thread {
    public:
        InstallThread (juce::Component::SafePointer<InstallerComponent> ownerToUse, InstallRequest requestToUse)
            : juce::Thread ("Linux installer"), owner (std::move (ownerToUse)), request (std::move (requestToUse)) {
        }

        void run() override {
            runInstallRequest (owner, std::move (request));
        }

    private:
        juce::Component::SafePointer<InstallerComponent> owner;
        InstallRequest request;
    };
#endif

    osci::SvgButton helpButton { "installerHelp", juce::String (BinaryData::help_svg), juce::Colours::white };
    juce::Label headingLabel;
    ProductTile osciRenderTile;
    ProductTile sosciTile;
    ProductTile osciLaserTile;
    juce::Component panel;
    juce::Label choiceLabel;
    juce::TextButton freeChoiceButton;
    juce::TextButton premiumChoiceButton;
    juce::Label statusLabel;
    osci::TextEditor licenseKeyEditor;
    juce::HyperlinkButton needLicenseLink;
    juce::TextButton premiumInstallButton;
    double progressValue = 0.0;
    InstallerProgressBar progressBar;
    std::unique_ptr<osci::LicenseHelpOverlay> supportOverlay;

#if JUCE_LINUX
    LinuxInstallLocationsComponent locationsPanel;
    juce::Component::SafePointer<osci::OverlayComponent> locationsOverlay;
    osci::LinuxInstallLocations selectedLocations;
    InstallPath pendingInstallPath = InstallPath::None;
    bool installLocationsConfirmed = false;

    InstallCompletionComponent completionPanel;
    bool installationComplete = false;
    std::unique_ptr<InstallThread> installThread;
#endif

    ProductChoice selectedProduct = ProductChoice::None;
    InstallPath currentPath = InstallPath::None;
    bool busy = false;
    bool hasCachedPremiumToken = false;
    bool cachedTokenNeedsRefresh = false;
    juce::String cachedTokenMessage;
    juce::String lastInstalledVersion;

#if DEBUG && JUCE_MODULE_AVAILABLE_jucewright
    jucewright::EnvironmentAutomation automation { *this };
#endif

    void showSupportOverlay() {
        if (supportOverlay != nullptr) {
            supportOverlay->grabKeyboardFocus();
            return;
        }

        supportOverlay = std::make_unique<osci::LicenseHelpOverlay>();
        supportOverlay->onDismissRequested = [this] {
            supportOverlay = nullptr;
        };

        supportOverlay->captureBackdropFrom (*this);
        addAndMakeVisible (*supportOverlay);
        supportOverlay->setBounds (getLocalBounds());
        supportOverlay->toFront (false);
        supportOverlay->grabKeyboardFocus();
    }

#if JUCE_LINUX
    void setInstallerContentHeight (int height) {
        setSize (760, height);
        auto* window = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent());
        if (window != nullptr) {
            auto visibleWidth = 780;
            auto visibleHeight = height;
            const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            if (display != nullptr) {
                const auto displayWidth = juce::roundToInt (display->userBounds.getWidth());
                const auto displayHeight = juce::roundToInt (display->userBounds.getHeight());
                visibleWidth = juce::jmin (visibleWidth, juce::jmax (480, displayWidth - 40));
                visibleHeight = juce::jmin (visibleHeight, juce::jmax (420, displayHeight - 80));
            }
            const auto frameHeight = juce::jmax (0, window->getHeight() - window->getContentComponent()->getHeight());
            window->setContentComponentSize (visibleWidth, visibleHeight + frameHeight);
            if (auto* viewport = dynamic_cast<juce::Viewport*> (window->getContentComponent())) {
                viewport->setViewPosition (0, 0);
                auto safeViewport = juce::Component::SafePointer<juce::Viewport> (viewport);
                juce::MessageManager::callAsync ([safeViewport] {
                    if (safeViewport != nullptr) {
                        safeViewport->setViewPosition (0, 0);
                    }
                });
                juce::Timer::callAfterDelay (100, [safeViewport] {
                    if (safeViewport != nullptr) {
                        safeViewport->setViewPosition (0, 0);
                    }
                });
            }
            window->centreWithSize (window->getWidth(), window->getHeight());
        }
    }

    juce::String installLocationsTitle (InstallPath path) const {
        const auto product = productNameForPath(path);
        const auto variant = path == InstallPath::OsciRenderFree ? juce::String ("free") : "premium";
        return "Install " + product + " " + variant;
    }

    void showInstallLocations (InstallPath path) {
        if (locationsOverlay != nullptr) {
            locationsOverlay->grabKeyboardFocus();
            return;
        }

        pendingInstallPath = path;
        installLocationsConfirmed = false;
        locationsPanel.setSize (620, 210);

        auto overlay = std::make_unique<osci::ComponentOverlay> (
            locationsPanel,
            installLocationsTitle (path),
            juce::Point<int> { 620, 210 },
            true);
        overlay->setDismissible (true);

        auto safeThis = juce::Component::SafePointer<InstallerComponent> (this);
        overlay->onDismissRequested = [safeThis] {
            if (safeThis == nullptr) {
                return;
            }

            const auto confirmed = safeThis->installLocationsConfirmed;
            const auto path = safeThis->pendingInstallPath;
            safeThis->installLocationsConfirmed = false;
            safeThis->pendingInstallPath = InstallPath::None;
            safeThis->locationsOverlay = nullptr;
            if (!confirmed) {
                safeThis->refreshUi();
                return;
            }

            juce::MessageManager::callAsync ([safeThis, path] {
                if (safeThis != nullptr) {
                    safeThis->beginInstall (path);
                }
            });
        };

        locationsOverlay = osci::OverlayComponent::show (*this, std::move (overlay));
    }

    void resetAfterInstall() {
        installationComplete = false;
        selectedProduct = ProductChoice::None;
        currentPath = InstallPath::None;
        progressValue = 0.0;
        completionPanel.setVisible (false);
        setInstallerContentHeight (520);
        refreshUi();
        resized();
    }
#endif

    void layoutPanel() {
        auto area = panel.getLocalBounds().reduced (22, 18);
#if JUCE_LINUX
        constexpr auto choiceHeight = 20;
        constexpr auto editorHeight = 32;
        constexpr auto installButtonHeight = 34;
        constexpr auto linkHeight = 20;
        constexpr auto controlGap = 4;
        constexpr auto actionGap = 4;
#else
        constexpr auto choiceHeight = 24;
        constexpr auto editorHeight = 34;
        constexpr auto installButtonHeight = 38;
        constexpr auto linkHeight = 22;
        constexpr auto controlGap = 12;
        constexpr auto actionGap = 8;
#endif

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
            choiceLabel.setBounds (area.removeFromTop (choiceHeight));
            area.removeFromTop (controlGap);
        } else {
            choiceLabel.setBounds ({});
        }

        if (isPremiumPath (currentPath)) {
            if (licenseKeyEditor.isVisible()) {
                licenseKeyEditor.setBounds (area.removeFromTop (editorHeight).withSizeKeepingCentre (380, editorHeight));
                area.removeFromTop (controlGap);
            } else {
                licenseKeyEditor.setBounds ({});
            }

            if (premiumInstallButton.isVisible()) {
                premiumInstallButton.setBounds (area.removeFromTop (installButtonHeight).withSizeKeepingCentre (260, installButtonHeight));
                area.removeFromTop (actionGap);
            } else {
                premiumInstallButton.setBounds ({});
            }

            if (needLicenseLink.isVisible()) {
                needLicenseLink.setBounds (area.removeFromTop (linkHeight).withSizeKeepingCentre (180, linkHeight));
                area.removeFromTop (controlGap);
            } else {
                needLicenseLink.setBounds ({});
            }
        } else {
            licenseKeyEditor.setBounds ({});
            needLicenseLink.setBounds ({});
            premiumInstallButton.setBounds ({});
        }

        if (progressBar.isVisible()) {
            progressBar.setBounds (area.removeFromTop (22).withSizeKeepingCentre (380, 22));
            area.removeFromTop (8);
        } else {
            progressBar.setBounds ({});
        }

        statusLabel.setBounds (statusLabel.isVisible() ? area.removeFromTop (42) : juce::Rectangle<int> {});
    }

    void selectProduct (ProductChoice product) {
        if (busy) {
            return;
        }

        selectedProduct = product;
        needLicenseLink.setURL(purchaseUrl(product));
        if (product == ProductChoice::Sosci) {
            currentPath = InstallPath::SosciPremium;
        } else if (product == ProductChoice::OsciLaser) {
            currentPath = InstallPath::OsciLaserPremium;
        } else {
            currentPath = InstallPath::None;
        }
        licenseKeyEditor.clear();
        loadCachedLicenseState();
#if JUCE_LINUX
        locationsPanel.load (productSlug (selectedProduct));
        selectedLocations = locationsPanel.getLocations();
#endif
        refreshUi();
#if JUCE_LINUX
        resized();
#endif
    }

    void choosePremiumPath() {
        if (busy || selectedProduct == ProductChoice::None) {
            return;
        }

        if (selectedProduct == ProductChoice::Sosci) {
            currentPath = InstallPath::SosciPremium;
        } else if (selectedProduct == ProductChoice::OsciLaser) {
            currentPath = InstallPath::OsciLaserPremium;
        } else {
            currentPath = InstallPath::OsciRenderPremium;
        }
        loadCachedLicenseState();
        refreshUi();
#if JUCE_LINUX
        resized();
#endif
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
        const auto selectedOsciLaser = selectedProduct == ProductChoice::OsciLaser;
        const auto premiumPath = isPremiumPath (currentPath);
        const auto showOsciChoice = selectedOsciRender && currentPath == InstallPath::None;
        const auto showPanel = selectedProduct != ProductChoice::None;
        const auto showKeyEntry = premiumPath && !hasCachedPremiumToken;
        const auto showChoiceLabel = showKeyEntry && !busy;

        osciRenderTile.setSelected (selectedOsciRender);
        sosciTile.setSelected (selectedSosci);
        osciLaserTile.setSelected(selectedOsciLaser);
        osciRenderTile.setInteractive(!busy);
        sosciTile.setInteractive(!busy);
        osciLaserTile.setInteractive(!busy);

        panel.setVisible (showPanel);
        choiceLabel.setVisible (showChoiceLabel);
        freeChoiceButton.setVisible (showOsciChoice);
        premiumChoiceButton.setVisible (showOsciChoice);
        statusLabel.setVisible (showPanel && !showOsciChoice && (!showKeyEntry || busy));
        licenseKeyEditor.setVisible (showKeyEntry && !busy);
        needLicenseLink.setVisible (showKeyEntry && !busy);
        premiumInstallButton.setVisible (premiumPath && !busy);
        progressBar.setVisible (busy || progressValue > 0.0);

#if JUCE_LINUX
        const auto showInstaller = !installationComplete;
        headingLabel.setVisible (showInstaller);
        helpButton.setVisible (showInstaller);
        osciRenderTile.setVisible (showInstaller);
        sosciTile.setVisible (showInstaller);
        osciLaserTile.setVisible(showInstaller);
        panel.setVisible (showInstaller && showPanel);
        completionPanel.setVisible (installationComplete);
        locationsPanel.setBusy (busy);
#endif

        freeChoiceButton.setEnabled(!busy);
        premiumChoiceButton.setEnabled(!busy);
        licenseKeyEditor.setEnabled(!busy);
        premiumInstallButton.setEnabled(!busy);

        if (showKeyEntry) {
            choiceLabel.setText ("Enter your license key", juce::dontSendNotification);
        } else if (showOsciChoice) {
            choiceLabel.setText ("Choose an edition", juce::dontSendNotification);
        }

        if (!busy) {
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
        return path == InstallPath::OsciRenderPremium || path == InstallPath::SosciPremium
            || path == InstallPath::OsciLaserPremium;
    }

    static juce::String productNameForPath(InstallPath path) {
        if (path == InstallPath::SosciPremium) {
            return "sosci";
        }
        if (path == InstallPath::OsciLaserPremium) {
            return "osci-laser";
        }
        return "osci-render";
    }

    InstallRequest makeRequest (InstallPath path) const {
        InstallRequest request;
        request.productSlug = productNameForPath(path);
        request.productName = request.productSlug;
        request.variant = path == InstallPath::OsciRenderFree ? "free" : "premium";
        request.licenseKey = licenseKeyEditor.getText().trim();
        request.premium = path != InstallPath::OsciRenderFree;
#if JUCE_LINUX
        request.locations = selectedLocations;
#endif
        return request;
    }

    void requestInstall (InstallPath path) {
        if (busy || path == InstallPath::None) {
            return;
        }

        const auto request = makeRequest (path);
        if (request.premium && request.licenseKey.isEmpty() && !hasCachedPremiumToken) {
            currentPath = path;
            statusLabel.setText ("Enter your license key to install premium.", juce::dontSendNotification);
            refreshUi();
            return;
        }

#if JUCE_LINUX
        showInstallLocations (path);
#else
        beginInstall (path);
#endif
    }

    void beginInstall (InstallPath path) {
        if (busy || path == InstallPath::None) {
            return;
        }

        currentPath = path;
        const auto request = makeRequest (path);

        progressValue = 0.0;
        setBusy (true, request.premium ? "Preparing premium install..." : "Preparing free install...");

        auto safeThis = juce::Component::SafePointer<InstallerComponent> (this);
#if JUCE_LINUX
        installThread = std::make_unique<InstallThread> (safeThis, request);
        if (!installThread->startThread()) {
            installThread = nullptr;
            const auto message = juce::String ("Could not start the installation worker.");
            setBusy (false, message);
            osci::InstallPrompt::showError (this, "Install failed", message);
        }
#else
        juce::Thread::launch ([safeThis, request] {
            runInstallRequest (safeThis, request);
        });
#endif
    }

    void setBusy (bool shouldBeBusy, const juce::String& message) {
        busy = shouldBeBusy;
        if (onBusyChanged != nullptr) {
            onBusyChanged (busy);
        }
        refreshUi();
        statusLabel.setText (message, juce::dontSendNotification);
    }

    static void runInstallRequest (juce::Component::SafePointer<InstallerComponent> safeThis,
                                   InstallRequest request) {
#if DEBUG
        const auto automationResult = juce::SystemStats::getEnvironmentVariable ("OSCI_INSTALLER_AUTOMATION_RESULT", {});
        if (automationResult.isNotEmpty()) {
            runAutomationInstallRequest (safeThis, std::move (request), automationResult);
            return;
        }
#endif

        juce::Result result = juce::Result::ok();
        juce::String token;
        std::optional<osci::VersionInfo> version;
        juce::File installerFile;
        osci::LinuxInstaller::Report installReport;

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

            if (!version.has_value()) {
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

                    safeThis->progressValue = fraction >= 0.0 ? fraction * 0.7 : -1.0;
                    const auto status = fraction >= 1.0
                        ? juce::String ("Verifying package...")
                        : "Downloading " + juce::File::descriptionOfSizeInBytes (downloadedBytes);
                    safeThis->statusLabel.setText (status, juce::dontSendNotification);
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

#if JUCE_LINUX
        if (result.wasOk()) {
            juce::MessageManager::callAsync ([safeThis] {
                if (safeThis != nullptr) {
                    safeThis->progressValue = 0.7;
                    safeThis->statusLabel.setText ("Installing application and plugins...", juce::dontSendNotification);
                    safeThis->refreshUi();
                }
            });

            osci::LinuxInstaller::Request installRequest;
            installRequest.manifest = osci::linuxInstallManifest (request.productSlug);
            installRequest.archive = installerFile;
            installRequest.locations = request.locations;
            installRequest.iconPng = productIconPng (request.productSlug);
            installRequest.progress = [safeThis] (double fraction, juce::StringRef stage) {
                juce::MessageManager::callAsync ([safeThis, fraction, stage = juce::String (stage)] {
                    if (safeThis != nullptr) {
                        safeThis->progressValue = 0.7 + fraction * 0.3;
                        safeThis->statusLabel.setText (stage, juce::dontSendNotification);
                        safeThis->refreshUi();
                    }
                });
            };
            result = osci::LinuxInstaller().install (installRequest, installReport);
            if (result.failed()) {
                result = failWithContext ("Could not install " + request.productName, result);
            }
        }
#endif

        juce::MessageManager::callAsync ([safeThis, request, result, version, installerFile, installReport] {
            if (safeThis == nullptr) {
                return;
            }

            safeThis->handleInstallResult (request, result, version, installerFile, installReport);
        });
    }

#if DEBUG
    static void runAutomationInstallRequest (juce::Component::SafePointer<InstallerComponent> safeThis,
                                             InstallRequest request,
                                             juce::StringRef requestedResult) {
        const auto postProgress = [safeThis] (double progress, juce::String message) {
            juce::MessageManager::callAsync ([safeThis, progress, message = std::move (message)] {
                if (safeThis != nullptr) {
                    safeThis->progressValue = progress;
                    safeThis->statusLabel.setText (message, juce::dontSendNotification);
                    safeThis->refreshUi();
                }
            });
            juce::Thread::sleep (120);
        };

        postProgress (0.2, "Downloading verified test package...");
        postProgress (0.55, "Validating test package...");
        postProgress (0.85, "Installing application and plugins...");

        auto result = juce::Result::ok();
        osci::LinuxInstaller::Report report;
        report.standalonePath = request.locations.standaloneDirectory.getChildFile (request.productSlug);
        for (const auto& bundle : osci::linuxInstallManifest (request.productSlug).vst3Bundles) {
            report.vst3Paths.add (request.locations.vst3Directory.getChildFile (bundle));
        }
        if (juce::String (requestedResult) == "failure") {
            result = juce::Result::fail ("The test installation could not write to the selected directory.");
        } else if (juce::String (requestedResult) == "warning") {
            report.warnings.add ("The previous test plugin copy could not be removed.");
        }

        osci::VersionInfo version;
        version.product = request.productSlug;
        version.variant = request.variant;
        version.semver = "9.9.9.9";
        const auto testArtifact = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("osci-installer-test.zip");
        testArtifact.replaceWithText ("test");
        juce::MessageManager::callAsync ([safeThis, request = std::move (request), result, version, testArtifact, report] {
            if (safeThis != nullptr) {
                safeThis->handleInstallResult (request, result, version, testArtifact, report);
            }
            testArtifact.deleteFile();
        });
    }
#endif

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
        } else if (!licenseManager.hasPremium()) {
            return juce::Result::fail ("Enter a license key or activate this product from the plugin first.");
        }

        if (!licenseManager.hasPremium()) {
            return juce::Result::fail ("This license is not valid for premium downloads.");
        }

        token = licenseManager.getCachedToken();
        return token.isNotEmpty() ? juce::Result::ok()
                                  : juce::Result::fail ("No premium license token is available.");
    }

    void handleInstallResult (const InstallRequest& request,
                              const juce::Result& result,
                              const std::optional<osci::VersionInfo>& version,
                              const juce::File& installerFile,
                              const osci::LinuxInstaller::Report& installReport) {
#if JUCE_LINUX
        if (installThread != nullptr) {
            installThread->stopThread (-1);
            installThread = nullptr;
        }
#endif
        if (result.failed()) {
            progressValue = 0.0;
            setBusy (false, result.getErrorMessage());
            osci::InstallPrompt::showError (this, "Install failed", result.getErrorMessage());
            return;
        }

        if (!version.has_value() || !installerFile.existsAsFile()) {
            progressValue = 0.0;
            const auto message = "The installer downloaded, but the installer file was not found on disk.";
            setBusy (false, message);
            osci::InstallPrompt::showError (this, "Install failed", message);
            return;
        }

#if JUCE_LINUX
        progressValue = 1.0;
        lastInstalledVersion = version->semver;
        setBusy (false, "Installation complete");
        installationComplete = true;
        completionPanel.showResult (request.productName, request.variant, lastInstalledVersion, installReport);
        completionPanel.setVisible (true);
        setInstallerContentHeight (520);
        refreshUi();
        resized();
        return;
#endif

        progressValue = 1.0;
        setBusy (true, "Downloaded and verified. Confirm to launch the installer.");

        auto safeThis = juce::Component::SafePointer<InstallerComponent> (this);
        osci::InstallPrompt::showConfirmation ({
            this,
            [safeThis, request, version, installerFile] {
                if (safeThis != nullptr) {
                    safeThis->statusLabel.setText ("Launching installer...", juce::dontSendNotification);
                }

                const auto launchResult = osci::UpdateInstaller::launchWithPendingMarker (
                    { installerFile, version, request.productSlug, currentInstallerVersionBaseline() });
                if (launchResult.failed()) {
                    if (safeThis != nullptr) {
                        safeThis->progressValue = 0.0;
                        safeThis->setBusy (false, launchResult.getErrorMessage());
                        osci::InstallPrompt::showError (safeThis.getComponent(), "Install failed",
                                                       launchResult.getErrorMessage());
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
            }
        });
    }

};

} // namespace osci::installer
