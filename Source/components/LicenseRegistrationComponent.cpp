#include "LicenseRegistrationComponent.h"

LicenseRegistrationComponent::LicenseRegistrationComponent(CommonAudioProcessor& processor, std::function<void(bool)> onLicenseVerified)
    : audioProcessor(processor), onLicenseVerified(onLicenseVerified)
{
    setupComponents();
    
    auto showComponent = [this] {
        // If validated within the last week, show immediately
        juce::WeakReference<LicenseRegistrationComponent> weakThis(this);
        juce::MessageManager::callAsync([weakThis]() {
            if (auto* strongThis = weakThis.get()) {
                strongThis->setVisible(true);
            }
        });
        audioProcessor.licenseVerified = false;
    };
    
    audioProcessor.reloadGlobalSettings();
    auto savedKey = audioProcessor.getGlobalStringValue("license_key");
    if (savedKey.isNotEmpty())
    {
        // Pre-populate the license key field
        licenseKeyEditor.setText(savedKey, false);
        
        auto lastValidated = audioProcessor.getGlobalStringValue("license_last_validated");
        if (lastValidated.isNotEmpty())
        {
            auto lastValidationTime = juce::Time::fromISO8601(lastValidated);
            auto weekAgo = juce::Time::getCurrentTime() - juce::RelativeTime::weeks(1);
            auto hourAgo = juce::Time::getCurrentTime() - juce::RelativeTime::hours(1);
            
            if (lastValidationTime > weekAgo)
            {
                if (onLicenseVerified != nullptr) {
                    onLicenseVerified(true);
                }
                
                audioProcessor.licenseVerified = true;
            } else {
                showComponent();
            }
            
            if (lastValidationTime < hourAgo) {
                // Validate the license key in the background
                validateSavedLicense();
            }
            
            // Start periodic checks every hour
            startTimer(1000 * 60 * 60);
        }
    } else {
        showComponent();
    }
}

LicenseRegistrationComponent::~LicenseRegistrationComponent()
{
    stopTimer();
}

void LicenseRegistrationComponent::setupComponents()
{
    titleLabel.setText("License Registration Required", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(24.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);
    
    instructionsLabel.setText("Please enter your license key to continue", juce::dontSendNotification);
    instructionsLabel.setJustificationType(juce::Justification::centred);
    instructionsLabel.setFont(juce::Font(18.0f));
    addAndMakeVisible(instructionsLabel);
    
    licenseKeyEditor.setTextToShowWhenEmpty("XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX", juce::Colours::grey);
    licenseKeyEditor.setInputRestrictions(35, "0123456789ABCDEFabcdef-"); // Allow both upper and lowercase hex digits and hyphens
    licenseKeyEditor.onReturnKey = [this] { verifyButton.triggerClick(); };
    licenseKeyEditor.setFont(juce::Font(24.0f));
    licenseKeyEditor.onTextChange = [this] {
        auto currentText = licenseKeyEditor.getText();
        auto upperText = currentText.toUpperCase();
        if (currentText != upperText)
        {
            auto cursorPos = licenseKeyEditor.getCaretPosition();
            licenseKeyEditor.setText(upperText, false);
            licenseKeyEditor.setCaretPosition(cursorPos);
        }
    };
    addAndMakeVisible(licenseKeyEditor);

    verifyButton.setButtonText("Verify License");
    verifyButton.onClick = [this] {
        if (!isVerifying)
        {
            verifyLicense(licenseKeyEditor.getText());
        }
    };
    addAndMakeVisible(verifyButton);
}

void LicenseRegistrationComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.9f));
    
    auto bounds = getLocalBounds().toFloat();
}

void LicenseRegistrationComponent::resized()
{
    auto bounds = getLocalBounds().reduced(20);
    
    titleLabel.setBounds(bounds.removeFromTop(40));
    bounds.removeFromTop(20);
    
    instructionsLabel.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(20);
    
    auto row = bounds.removeFromTop(35);
    licenseKeyEditor.setBounds(row.reduced(50, 0));
    bounds.removeFromTop(20);
    
    auto buttonBounds = bounds.removeFromTop(40);
    verifyButton.setBounds(buttonBounds.withSizeKeepingCentre(120, 40));
}

void LicenseRegistrationComponent::verifyLicense(const juce::String& licenseKey, bool showErrorDialog)
{
    if (licenseKey.isEmpty())
        return;
        
    isVerifying = true;
    verifyButton.setEnabled(false);
    verifyButton.setVisible(false);
    
    juce::URL url("https://api.osci-render.com/api/verify-license");
    
    auto jsonObj = std::make_unique<juce::DynamicObject>();
    jsonObj->setProperty("license_key", licenseKey);
    juce::var jsonData(jsonObj.release());
    
    url = url.withPOSTData(juce::JSON::toString(jsonData));
    
    auto webStream = url.createInputStream(false, nullptr, nullptr,
        "Content-Type: application/json",
        10000);
    
    bool successfullyVerified = false;
        
    if (webStream != nullptr)
    {
        auto response = webStream->readEntireStreamAsString();
        DBG(response);
        auto json = juce::JSON::parse(response);
        
        if (json.hasProperty("success")) {
            bool success = json["success"];
            
            if (success && json.hasProperty("valid") && json.hasProperty("purchase")) {
                bool valid = json["valid"];
                
                auto purchase = json["purchase"].getDynamicObject();
                auto productId = purchase->getProperty("product_id").toString();
                
                if (success && valid && productId == SOSCI_PRODUCT_ID)
                {
                    // Save the license key and validation timestamp
                    audioProcessor.setGlobalValue("license_key", licenseKey);
                    audioProcessor.setGlobalValue("license_last_validated", juce::Time::getCurrentTime().toISO8601(true));
                    audioProcessor.saveGlobalSettings();

                    audioProcessor.licenseVerified = true;
                    
                    successfullyVerified = true;
                    
                    juce::WeakReference<LicenseRegistrationComponent> weakThis(this);
                    juce::MessageManager::callAsync([weakThis]() {
                        if (auto* strongThis = weakThis.get()) {
                            strongThis->setVisible(false);
                        }
                    });
                    startTimer(1000 * 60 * 60); // Check every hour
                }
                else if (showErrorDialog)
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                           "Invalid License",
                                                           "The license key you entered is not valid. Please check and try again.");
                }
                else
                {
                    // Background check failed, clear the license
                    clearLicense();
                }
            } else if (showErrorDialog && json.hasProperty("message")) {
                auto message = json["message"].toString();
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", message);
            }
        }
    }
    else if (showErrorDialog)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Connection Error",
            "Could not connect to the license server. Please check your internet connection and try again.");
    }
    
    isVerifying = false;
    verifyButton.setEnabled(true);
    verifyButton.setVisible(true);
    
    if (onLicenseVerified != nullptr) {
        onLicenseVerified(successfullyVerified);
    }
}

bool LicenseRegistrationComponent::validateSavedLicense()
{
    auto savedKey = audioProcessor.getGlobalStringValue("license_key");
    if (savedKey.isNotEmpty())
    {
        auto lastValidated = audioProcessor.getGlobalStringValue("license_last_validated");
        if (lastValidated.isNotEmpty())
        {
            // Verify in the background without showing error dialogs
            verifyLicense(savedKey, false);
            return true;
        }
    }
    return false;
}

void LicenseRegistrationComponent::clearLicense()
{
    audioProcessor.removeGlobalValue("license_key");
    audioProcessor.removeGlobalValue("license_last_validated");
    audioProcessor.saveGlobalSettings();
    setVisible(true);
    audioProcessor.licenseVerified = false;
}

void LicenseRegistrationComponent::timerCallback()
{
    auto savedKey = audioProcessor.getGlobalStringValue("license_key");
    if (savedKey.isNotEmpty()) {
        verifyLicense(savedKey, false);
    }
}
